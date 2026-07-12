#!/usr/bin/env python3
"""Generate deterministic C++ SPIR-V headers from the Vulkan shader manifest."""

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
SHADER_ROOT = REPO_ROOT / "src" / "Vulkan_shaders"
MANIFEST_PATH = SHADER_ROOT / "manifest.json"
GENERATED_ROOT = SHADER_ROOT / "generated"
MASTER_HEADER = GENERATED_ROOT / "VulkanShaders.h"
LOCK_PATH = GENERATED_ROOT / "manifest.lock.json"
SPIRV_MAGIC = 0x07230203


@dataclass(frozen=True)
class Variant:
    shader_name: str
    source: Path
    stage: str
    entry_point: str
    variant_name: str
    defines: tuple[tuple[str, str], ...]

    @property
    def symbol(self) -> str:
        suffix = "" if self.variant_name == "default" else self.variant_name
        return sanitize_identifier(self.shader_name + suffix)

    @property
    def output_name(self) -> str:
        return f"{self.symbol}.spv.h"


def sanitize_identifier(value: str) -> str:
    result = re.sub(r"[^A-Za-z0-9_]", "_", value)
    if not result or result[0].isdigit():
        result = "Shader_" + result
    return result


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


# MELONPRIME_VULKAN_TEXT_HASH_NORMALIZATION_V1
# Git may materialize committed text as CRLF on Windows. Shader metadata tracks
# semantic UTF-8 text, so normalize line endings before hashing or comparing.
def normalize_text_bytes(data: bytes) -> bytes:
    return data.replace(b"\r\n", b"\n").replace(b"\r", b"\n")


def read_normalized_text(path: Path) -> bytes:
    return normalize_text_bytes(path.read_bytes())


def sha256_text_file(path: Path) -> str:
    return sha256_bytes(read_normalized_text(path))


def canonical_json(value: object) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")


def load_manifest() -> tuple[dict[str, object], list[Variant]]:
    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    if manifest.get("schema_version") != 1:
        raise ValueError("unsupported Vulkan shader manifest schema")
    shaders = manifest.get("shaders")
    if not isinstance(shaders, list) or not shaders:
        raise ValueError("manifest must contain at least one shader")

    variants: list[Variant] = []
    seen_symbols: set[str] = set()
    for shader in shaders:
        if not isinstance(shader, dict):
            raise ValueError("shader rows must be objects")
        source = SHADER_ROOT / str(shader["source"])
        if not source.is_file() or SHADER_ROOT not in source.resolve().parents:
            raise ValueError(f"shader source is missing or outside shader root: {source}")
        variant_rows = shader.get("variants", [{"name": "default", "defines": {}}])
        if not isinstance(variant_rows, list) or not variant_rows:
            raise ValueError(f"{shader['name']}: variants must be a non-empty list")
        for row in variant_rows:
            if not isinstance(row, dict):
                raise ValueError(f"{shader['name']}: variant rows must be objects")
            defines_value = row.get("defines", {})
            if not isinstance(defines_value, dict):
                raise ValueError(f"{shader['name']}: defines must be an object")
            variant = Variant(
                shader_name=str(shader["name"]),
                source=source,
                stage=str(shader["stage"]),
                entry_point=str(shader.get("entry_point", "main")),
                variant_name=str(row.get("name", "default")),
                defines=tuple(sorted((str(k), str(v)) for k, v in defines_value.items())),
            )
            if variant.symbol in seen_symbols:
                raise ValueError(f"duplicate generated shader symbol: {variant.symbol}")
            seen_symbols.add(variant.symbol)
            variants.append(variant)
    return manifest, variants


def find_compiler(explicit: Path | None) -> Path | None:
    candidates: list[Path] = []
    if explicit:
        candidates.append(explicit)
    env_compiler = os.environ.get("GLSLANG_VALIDATOR")
    if env_compiler:
        candidates.append(Path(env_compiler))
    vulkan_sdk = os.environ.get("VULKAN_SDK")
    if vulkan_sdk:
        candidates.extend(
            [Path(vulkan_sdk) / "Bin" / "glslangValidator.exe",
             Path(vulkan_sdk) / "bin" / "glslangValidator"]
        )
    discovered = shutil.which("glslangValidator") or shutil.which("glslangValidator.exe")
    if discovered:
        candidates.append(Path(discovered))
    for candidate in candidates:
        resolved = candidate.expanduser().resolve()
        if resolved.is_file():
            return resolved
    return None


def compiler_version(compiler: Path) -> str:
    result = subprocess.run(
        [str(compiler), "--version"],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"failed to query {compiler}:\n{result.stdout}{result.stderr}".rstrip()
        )
    return "\n".join(line.rstrip() for line in result.stdout.splitlines()).strip()


def compile_variant(
    compiler: Path, target_environment: str, variant: Variant, output: Path
) -> bytes:
    command = [
        str(compiler),
        "-V",
        "--target-env",
        target_environment,
        "-S",
        variant.stage,
        "-e",
        variant.entry_point,
        "-o",
        str(output),
    ]
    command.extend(f"-D{name}={value}" for name, value in variant.defines)
    command.append(str(variant.source))
    result = subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode != 0:
        details = (result.stdout + result.stderr).strip()
        raise RuntimeError(
            f"shader compile failed: {variant.shader_name}/{variant.variant_name}\n"
            f"command: {subprocess.list2cmdline(command)}\n{details}"
        )
    data = output.read_bytes()
    if len(data) < 20 or len(data) % 4:
        raise RuntimeError(f"invalid SPIR-V size for {variant.symbol}: {len(data)}")
    if int.from_bytes(data[:4], "little") != SPIRV_MAGIC:
        raise RuntimeError(f"invalid SPIR-V magic for {variant.symbol}")
    return data


def render_spirv_header(variant: Variant, data: bytes, source_hash: str) -> bytes:
    words = [int.from_bytes(data[i : i + 4], "little") for i in range(0, len(data), 4)]
    lines = [
        "#pragma once",
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "namespace melonDS::Vulkan::Shaders",
        "{",
        "",
        f'inline constexpr char k{variant.symbol}SourceSha256[] = "{source_hash}";',
        f'inline constexpr char k{variant.symbol}SpirvSha256[] = "{sha256_bytes(data)}";',
        f"alignas(4) inline constexpr std::uint32_t k{variant.symbol}Spirv[] = {{",
    ]
    for offset in range(0, len(words), 8):
        chunk = ", ".join(f"0x{word:08x}u" for word in words[offset : offset + 8])
        lines.append(f"    {chunk},")
    lines.extend(
        [
            "};",
            f"inline constexpr std::size_t k{variant.symbol}SpirvSize = sizeof(k{variant.symbol}Spirv);",
            "",
            "} // namespace melonDS::Vulkan::Shaders",
        ]
    )
    return ("\n".join(lines) + "\n").encode("utf-8")


def render_master_header(
    variants: list[Variant], manifest_hash: str, compiler_text: str
) -> bytes:
    lines = ["#pragma once", ""]
    lines.extend(f'#include "{variant.output_name}"' for variant in variants)
    lines.extend(
        [
            "",
            "namespace melonDS::Vulkan::Shaders",
            "{",
            "",
            f"inline constexpr std::size_t kShaderCount = {len(variants)};",
            f'inline constexpr char kManifestSha256[] = "{manifest_hash}";',
            f'inline constexpr char kCompilerVersion[] = "{compiler_text}";',
            "",
            "} // namespace melonDS::Vulkan::Shaders",
        ]
    )
    return ("\n".join(lines) + "\n").encode("utf-8")


def generate(compiler: Path) -> dict[Path, bytes]:
    manifest, variants = load_manifest()
    target_environment = str(manifest.get("target_environment", "vulkan1.1"))
    version = compiler_version(compiler)
    compiler_text = " | ".join(version.splitlines()).replace("\\", "\\\\").replace('"', '\\"')
    source_records = []
    outputs: dict[Path, bytes] = {}
    spirv_records = []
    with tempfile.TemporaryDirectory(prefix="melonprime-spirv-") as directory:
        temp = Path(directory)
        for variant in variants:
            source_hash = sha256_text_file(variant.source)
            data = compile_variant(
                compiler, target_environment, variant, temp / f"{variant.symbol}.spv"
            )
            outputs[GENERATED_ROOT / variant.output_name] = render_spirv_header(
                variant, data, source_hash
            )
            source_records.append(
                {
                    "path": variant.source.relative_to(REPO_ROOT).as_posix(),
                    "sha256": source_hash,
                    "stage": variant.stage,
                    "entry_point": variant.entry_point,
                    "variant": variant.variant_name,
                    "defines": dict(variant.defines),
                }
            )
            spirv_records.append(
                {
                    "header": f"src/Vulkan_shaders/generated/{variant.output_name}",
                    "sha256": sha256_bytes(data),
                    "size": len(data),
                    "symbol": variant.symbol,
                }
            )

    manifest_hash_input = {
        "manifest": manifest,
        "sources": source_records,
        "compiler_version": version,
        "spirv": spirv_records,
    }
    manifest_hash = sha256_bytes(canonical_json(manifest_hash_input))
    outputs[MASTER_HEADER] = render_master_header(variants, manifest_hash, compiler_text)
    lock = {
        "schema_version": 1,
        "compiler_version": version,
        "manifest_sha256": sha256_text_file(MANIFEST_PATH),
        "generation_sha256": manifest_hash,
        "sources": source_records,
        "spirv": spirv_records,
    }
    outputs[LOCK_PATH] = canonical_json(lock)
    return outputs


def check_metadata_only() -> bool:
    manifest, variants = load_manifest()
    if not LOCK_PATH.is_file() or not MASTER_HEADER.is_file():
        print("error: generated Vulkan shader metadata is missing", file=sys.stderr)
        return False
    lock = json.loads(LOCK_PATH.read_text(encoding="utf-8"))
    expected_manifest_hash = sha256_text_file(MANIFEST_PATH)
    if lock.get("manifest_sha256") != expected_manifest_hash:
        print("error: Vulkan shader manifest changed without regeneration", file=sys.stderr)
        return False
    locked_sources = {
        (row["path"], row["variant"]): row["sha256"] for row in lock.get("sources", [])
    }
    locked_spirv = {row["symbol"]: row for row in lock.get("spirv", [])}
    master_text = MASTER_HEADER.read_text(encoding="utf-8")
    if lock.get("generation_sha256") not in master_text:
        print("error: Vulkan master header does not match the generation lock", file=sys.stderr)
        return False
    for variant in variants:
        key = (variant.source.relative_to(REPO_ROOT).as_posix(), variant.variant_name)
        if locked_sources.get(key) != sha256_text_file(variant.source):
            print(f"error: Vulkan shader source drift: {key[0]} ({key[1]})", file=sys.stderr)
            return False
        generated_header = GENERATED_ROOT / variant.output_name
        if not generated_header.is_file():
            print(f"error: missing generated header: {variant.output_name}", file=sys.stderr)
            return False
        if f'#include "{variant.output_name}"' not in master_text:
            print(f"error: master header does not include {variant.output_name}", file=sys.stderr)
            return False
        spirv_record = locked_spirv.get(variant.symbol)
        if not spirv_record:
            print(f"error: lock is missing SPIR-V record: {variant.symbol}", file=sys.stderr)
            return False
        header_text = generated_header.read_text(encoding="utf-8")
        declared_source = re.search(
            rf'k{re.escape(variant.symbol)}SourceSha256\[\] = "([0-9a-f]{{64}})"',
            header_text,
        )
        declared_spirv = re.search(
            rf'k{re.escape(variant.symbol)}SpirvSha256\[\] = "([0-9a-f]{{64}})"',
            header_text,
        )
        words = re.findall(r"0x([0-9a-fA-F]{8})u", header_text)
        embedded_spirv = b"".join(int(word, 16).to_bytes(4, "little") for word in words)
        expected_source_hash = locked_sources[key]
        expected_spirv_hash = spirv_record["sha256"]
        if not declared_source or declared_source.group(1) != expected_source_hash:
            print(f"error: generated source hash mismatch: {variant.output_name}", file=sys.stderr)
            return False
        if not declared_spirv or declared_spirv.group(1) != expected_spirv_hash:
            print(f"error: generated SPIR-V hash mismatch: {variant.output_name}", file=sys.stderr)
            return False
        if len(embedded_spirv) != spirv_record["size"] or sha256_bytes(embedded_spirv) != expected_spirv_hash:
            print(f"error: embedded SPIR-V payload drift: {variant.output_name}", file=sys.stderr)
            return False
    expected_headers = {variant.output_name for variant in variants}
    actual_headers = {path.name for path in GENERATED_ROOT.glob("*.spv.h")}
    if actual_headers != expected_headers:
        print("error: generated Vulkan shader header set does not match manifest", file=sys.stderr)
        return False
    print("Vulkan shader metadata check: PASS (compiler unavailable; source/hash integrity only)")
    return True


def write_or_check(outputs: dict[Path, bytes], check: bool) -> bool:
    changed: list[Path] = []
    for path, data in outputs.items():
        if not path.is_file() or read_normalized_text(path) != normalize_text_bytes(data):
            changed.append(path)
            if not check:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
    expected = {path.resolve() for path in outputs}
    if GENERATED_ROOT.is_dir():
        for path in GENERATED_ROOT.iterdir():
            if path.is_file() and path.resolve() not in expected:
                changed.append(path)
                if not check:
                    path.unlink()
    if changed:
        action = "out of date" if check else "updated"
        for path in sorted(set(changed)):
            print(f"{action}: {path.relative_to(REPO_ROOT)}")
        return not check
    print("Vulkan SPIR-V generated files: PASS")
    return True


def run_line_ending_self_test() -> bool:
    samples = [
        b'{"schema_version": 1}\n',
        b"#version 450\nvoid main() {}\n",
    ]
    with tempfile.TemporaryDirectory(prefix="melonprime-line-ending-test-") as directory:
        root = Path(directory)
        for index, lf_data in enumerate(samples):
            lf_path = root / f"sample-{index}-lf.txt"
            crlf_path = root / f"sample-{index}-crlf.txt"
            lf_path.write_bytes(lf_data)
            crlf_path.write_bytes(lf_data.replace(b"\n", b"\r\n"))
            if sha256_text_file(lf_path) != sha256_text_file(crlf_path):
                print("error: Vulkan text hash line-ending self-test failed", file=sys.stderr)
                return False
            if read_normalized_text(lf_path) != read_normalized_text(crlf_path):
                print("error: Vulkan text comparison line-ending self-test failed", file=sys.stderr)
                return False
    print("Vulkan shader text line-ending normalization: PASS")
    return True


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--allow-missing-compiler", action="store_true")
    parser.add_argument("--self-test-line-endings", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.self_test_line_endings:
        return 0 if run_line_ending_self_test() else 1
    compiler = find_compiler(args.compiler)
    if compiler is None:
        if args.check and args.allow_missing_compiler:
            return 0 if check_metadata_only() else 1
        raise RuntimeError(
            "glslangValidator was not found; pass --compiler, set GLSLANG_VALIDATOR, "
            "or enable the vcpkg Vulkan feature"
        )
    outputs = generate(compiler)
    return 0 if write_or_check(outputs, args.check) else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
