#!/usr/bin/env python3
"""Generate the Sapphire Vulkan SPIR-V headers from one manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MANIFEST = Path(__file__).with_name("sapphire_shader_manifest.json")
DEFAULT_OUTPUT_ROOT = REPO_ROOT / "build" / "generated" / "sapphire"
SPIRV_MAGIC = 0x07230203


@dataclass(frozen=True)
class Shader:
    source: Path
    stage: str
    entry_point: str
    generated_header: Path
    symbol: str
    defines: tuple[tuple[str, str], ...]


def normalized_bytes(path: Path) -> bytes:
    return path.read_bytes().replace(b"\r\n", b"\n").replace(b"\r", b"\n")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def canonical_json(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def repository_path(value: object, label: str) -> Path:
    path = (REPO_ROOT / str(value)).resolve()
    if path != REPO_ROOT and REPO_ROOT not in path.parents:
        raise ValueError(f"{label} is outside the repository: {value}")
    return path


def output_path(root: Path, value: object) -> Path:
    path = (root / str(value)).resolve()
    if path != root and root not in path.parents:
        raise ValueError(f"generated header is outside output root: {value}")
    return path


def load_manifest(path: Path, output_root: Path) -> tuple[dict[str, object], list[Shader]]:
    manifest = json.loads(path.read_text(encoding="utf-8"))
    if manifest.get("schema_version") != 1:
        raise ValueError("unsupported Sapphire shader manifest schema")
    namespace = str(manifest.get("namespace", ""))
    if not namespace or any(not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", part) for part in namespace.split("::")):
        raise ValueError(f"invalid generated C++ namespace: {namespace}")
    rows = manifest.get("shaders")
    if not isinstance(rows, list) or not rows:
        raise ValueError("manifest shaders must be a non-empty array")

    shaders: list[Shader] = []
    symbols: set[str] = set()
    headers: set[Path] = set()
    valid_stages = {"comp", "vert", "frag"}
    for row in rows:
        if not isinstance(row, dict):
            raise ValueError("shader rows must be objects")
        source = repository_path(row["source"], "shader source")
        if not source.is_file():
            raise ValueError(f"shader source does not exist: {source}")
        stage = str(row["stage"])
        if stage not in valid_stages:
            raise ValueError(f"unsupported shader stage: {stage}")
        symbol = str(row["symbol"])
        if not symbol.isidentifier() or symbol in symbols:
            raise ValueError(f"invalid or duplicate shader symbol: {symbol}")
        header = output_path(output_root, row["generated_header"])
        if header in headers:
            raise ValueError(f"duplicate generated header: {header}")
        defines = row.get("defines", {})
        if not isinstance(defines, dict):
            raise ValueError(f"{symbol}: defines must be an object")
        for required_reference_key in ("repository", "commit_or_tag", "path"):
            reference = row.get("reference")
            if not isinstance(reference, dict) or not reference.get(required_reference_key):
                raise ValueError(f"{symbol}: reference.{required_reference_key} is required")
        shaders.append(
            Shader(
                source=source,
                stage=stage,
                entry_point=str(row.get("entry_point", "main")),
                generated_header=header,
                symbol=symbol,
                defines=tuple(sorted((str(key), str(value)) for key, value in defines.items())),
            )
        )
        symbols.add(symbol)
        headers.add(header)
    return manifest, shaders


def resolve_compiler(explicit: Path | None) -> Path:
    candidates: list[Path] = []
    if explicit:
        candidates.append(explicit)
    if os.environ.get("GLSLANG_VALIDATOR"):
        candidates.append(Path(os.environ["GLSLANG_VALIDATOR"]))
    if os.environ.get("VULKAN_SDK"):
        sdk = Path(os.environ["VULKAN_SDK"])
        candidates.extend((sdk / "Bin" / "glslangValidator.exe", sdk / "bin" / "glslangValidator"))
    discovered = shutil.which("glslangValidator") or shutil.which("glslangValidator.exe")
    if discovered:
        candidates.append(Path(discovered))
    for candidate in candidates:
        resolved = candidate.expanduser().resolve()
        if resolved.is_file():
            return resolved
    raise RuntimeError("glslangValidator was not found; pass --compiler or set GLSLANG_VALIDATOR")


def compiler_version(compiler: Path) -> str:
    result = subprocess.run(
        [str(compiler), "--version"], capture_output=True, text=True, encoding="utf-8",
        errors="replace", check=False,
    )
    if result.returncode:
        raise RuntimeError(f"failed to query compiler version:\n{result.stdout}{result.stderr}")
    return " | ".join(line.strip() for line in result.stdout.splitlines() if line.strip())


def manifest_abi_hash(manifest: dict[str, object]) -> str:
    abi_sources = manifest.get("abi_sources", [])
    if not isinstance(abi_sources, list):
        raise ValueError("abi_sources must be an array")
    abi_files = []
    for value in abi_sources:
        path = repository_path(value, "ABI source")
        if not path.is_file():
            raise ValueError(f"ABI source does not exist: {path}")
        abi_files.append({"path": str(value).replace("\\", "/"), "sha256": sha256(normalized_bytes(path))})
    contract = {
        "schema_version": manifest["schema_version"],
        "target_environment": manifest.get("target_environment", "vulkan1.1"),
        "namespace": manifest.get("namespace", "melonDS::Vulkan::GeneratedShaders"),
        "abi_sources": abi_files,
        "shaders": manifest["shaders"],
    }
    return sha256(canonical_json(contract))


def compile_shader(compiler: Path, target: str, shader: Shader, destination: Path) -> bytes:
    command = [
        str(compiler), "-V", "--target-env", target, "-S", shader.stage,
        "-e", shader.entry_point, f"-I{shader.source.parent}", f"-I{REPO_ROOT / 'src'}",
    ]
    command.extend(f"-D{name}={value}" for name, value in shader.defines)
    command.extend(("-o", str(destination), str(shader.source)))
    result = subprocess.run(
        command, capture_output=True, text=True, encoding="utf-8", errors="replace", check=False,
        cwd=REPO_ROOT,
    )
    if result.returncode:
        details = (result.stdout + result.stderr).strip()
        raise RuntimeError(f"shader compile failed for {shader.symbol}:\n{details}")
    data = destination.read_bytes()
    if len(data) < 20 or len(data) % 4:
        raise RuntimeError(f"invalid SPIR-V byte size for {shader.symbol}: {len(data)}")
    if int.from_bytes(data[:4], "little") != SPIRV_MAGIC:
        raise RuntimeError(f"invalid SPIR-V magic for {shader.symbol}")
    return data


def cpp_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def render_header(
    shader: Shader, data: bytes, namespace: str, abi_hash: str, compiler: str
) -> bytes:
    source_hash = sha256(normalized_bytes(shader.source))
    spirv_hash = sha256(data)
    words = [int.from_bytes(data[offset : offset + 4], "little") for offset in range(0, len(data), 4)]
    lines = [
        "// Generated by tools/vulkan/generate_sapphire_spirv.py. DO NOT EDIT.",
        "#pragma once", "", "#include <cstddef>", "#include <cstdint>", "",
        f"namespace {namespace}", "{", "",
        f'inline constexpr char {shader.symbol}_source_sha256[] = "{source_hash}";',
        f'inline constexpr char {shader.symbol}_spirv_sha256[] = "{spirv_hash}";',
        f'inline constexpr char {shader.symbol}_abi_sha256[] = "{abi_hash}";',
        f'inline constexpr char {shader.symbol}_compiler[] = "{cpp_string(compiler)}";',
        f"alignas(4) inline constexpr std::uint32_t {shader.symbol}[] = {{",
    ]
    for offset in range(0, len(words), 8):
        lines.append("    " + ", ".join(f"0x{word:08x}u" for word in words[offset : offset + 8]) + ",")
    lines.extend(
        [
            "};",
            f"inline constexpr std::size_t {shader.symbol}_word_count = {len(words)}u;",
            f"inline constexpr std::size_t {shader.symbol}_byte_size = {len(data)}u;",
            f"inline constexpr std::size_t {shader.symbol}_len = {shader.symbol}_byte_size;",
            f"static_assert(sizeof({shader.symbol}) == {shader.symbol}_byte_size);",
            "", f"}} // namespace {namespace}", "",
        ]
    )
    return "\n".join(lines).encode("utf-8")


def generate(manifest_path: Path, output_root: Path, compiler: Path) -> dict[Path, bytes]:
    manifest, shaders = load_manifest(manifest_path, output_root)
    target = str(manifest.get("target_environment", "vulkan1.1"))
    namespace = str(manifest.get("namespace", "melonDS::Vulkan::GeneratedShaders"))
    abi_hash = manifest_abi_hash(manifest)
    version = compiler_version(compiler)
    outputs: dict[Path, bytes] = {}
    with tempfile.TemporaryDirectory(prefix="melonprime-sapphire-spirv-") as directory:
        temporary_root = Path(directory)
        for index, shader in enumerate(shaders):
            data = compile_shader(compiler, target, shader, temporary_root / f"{index}.spv")
            outputs[shader.generated_header] = render_header(shader, data, namespace, abi_hash, version)
    return outputs


def write_outputs(outputs: dict[Path, bytes], output_root: Path, check: bool) -> bool:
    changed = []
    for path, data in outputs.items():
        if not path.is_file() or normalized_bytes(path) != data:
            changed.append(path)
            if not check:
                path.parent.mkdir(parents=True, exist_ok=True)
                temporary = path.with_suffix(path.suffix + ".tmp")
                temporary.write_bytes(data)
                temporary.replace(path)
    expected = {path.resolve() for path in outputs}
    if output_root.is_dir():
        for path in output_root.glob("*.h"):
            if path.resolve() in expected:
                continue
            if not path.read_bytes().startswith(
                b"// Generated by tools/vulkan/generate_sapphire_spirv.py. DO NOT EDIT."
            ):
                continue
            changed.append(path)
            if not check:
                path.unlink()
    if changed:
        action = "out of date" if check else "generated"
        for path in changed:
            print(f"{action}: {path}")
        return not check
    print(f"Sapphire SPIR-V headers are current ({len(outputs)} shaders)")
    return True


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument("--compiler", type=Path)
    parser.add_argument("--check", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    manifest = args.manifest.resolve()
    output_root = args.output_root.resolve()
    compiler = resolve_compiler(args.compiler)
    return 0 if write_outputs(generate(manifest, output_root, compiler), output_root, args.check) else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
