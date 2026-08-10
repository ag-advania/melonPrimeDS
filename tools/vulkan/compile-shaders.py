#!/usr/bin/env python3
"""Compile the MelonPrimeDS Vulkan compute shaders to SPIR-V and emit C++ headers.

Every `.comp` under src/GPU3D_Vulkan_shaders/ is compiled once per pipeline
variant and once per tile-geometry bucket, targeting vulkan1.1, and the
resulting SPIR-V words are written into
src/GPU3D_Vulkan_shaders/generated/ as a committed header plus a JSON manifest.

Why buckets instead of one module per internal resolution: the OpenGL compute
renderer bakes every geometry constant into the shader text and recompiles the
whole set whenever the resolution changes. Vulkan compiles offline, so the
constants split in two:

  * TileSize, CoarseTileCountY, CoarseTileArea and ClearCoarseBinMaskLocalSize
    select workgroup sizes or must otherwise be literal at SPIR-V generation
    time, so they are injected as `-D` defines. All four derive only from
    `range = (scale>=5) + (scale>=9)`, so scales 1..16 collapse into exactly
    three compiled buckets.
  * ScreenWidth, ScreenHeight and MaxWorkTiles are plain scalars, so they are
    specialization constants (ids 0/1/2) and one module per bucket covers every
    scale in that bucket. Everything derived from them folds into
    OpSpecConstantOp and is resolved at pipeline creation.

Identical SPIR-V is emitted once and shared: several stages (FinalPass,
InterpSpans, SortWork, ...) do not depend on the bucket defines at all.

The output is deterministic -- stable ordering, no timestamps, no absolute
paths -- so regenerating without a source change produces no diff.

Usage:
    python tools/vulkan/compile-shaders.py [--glslang PATH]
                                           [--check | --check-source-sync]
                                           [--verbose]

--check recompiles and fails if the committed output is stale, without
touching the working tree.

--check-source-sync is compiler-version independent. It verifies that the
committed manifest was generated from the current GLSL/generator inputs and
that every committed SPIR-V blob still matches the manifest. CI uses this mode
because different supported glslang releases can emit different, equally valid
SPIR-V for the same GLSL; shader compilation and spirv-val remain covered by
tools/ci/audits/check-vulkan-shaders.py.
"""

from __future__ import annotations

import argparse
import glob
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vulkan_shader_set as vss  # noqa: E402

HEADER_NAME = "GPU3D_Vulkan_ShaderModules.h"
BLOBS_HEADER_NAME = "GPU3D_Vulkan_ShaderBlobs.h"
TABLE_SOURCE_NAME = "GPU3D_Vulkan_ShaderModules.cpp"
MANIFEST_NAME = "manifest.json"

LICENSE_HEADER = """/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/
"""


def compile_one(glslang: str, source: str, defines: dict[str, object],
                out_path: str) -> tuple[bool, str]:
    args = [glslang, "--target-env", "vulkan1.1", "-I" + vss.SHADER_DIR]
    for name in sorted(defines):
        value = defines[name]
        args.append(f"-D{name}" if value is None else f"-D{name}={value}")
    args += ["-o", out_path, source]

    result = subprocess.run(args, capture_output=True, text=True)
    diagnostics = "\n".join(
        part.strip() for part in (result.stdout, result.stderr) if part.strip()
    )
    return result.returncode == 0, diagnostics


def read_spirv(path: str) -> list[int]:
    with open(path, "rb") as handle:
        blob = handle.read()
    if len(blob) % 4 != 0:
        raise SystemExit(f"{path}: SPIR-V size {len(blob)} is not a multiple of 4")
    words = list(struct.unpack(f"<{len(blob) // 4}I", blob))
    if not words or words[0] != 0x07230203:
        raise SystemExit(f"{path}: bad SPIR-V magic number")
    return words


def format_blob(name: str, words: list[int]) -> str:
    lines = [f"extern const uint32_t {name}[{len(words)}];",
             f"const uint32_t {name}[{len(words)}] = {{"]
    for start in range(0, len(words), 8):
        chunk = words[start:start + 8]
        lines.append("    " + " ".join(f"0x{word:08x}," for word in chunk))
    lines.append("};")
    return "\n".join(lines)


GENERATED_BANNER = """
/*
    GENERATED FILE -- DO NOT EDIT.

    Produced by tools/vulkan/compile-shaders.py from the GLSL sources in
    src/GPU3D_Vulkan_shaders/. Edit the .comp/.glsl sources and rerun the
    generator; commit source and generated output together.

    Holds every compute pipeline of the Vulkan rasterizer, compiled for
    SPIR-V 1.3 (vulkan1.1), for each of the three tile-geometry buckets.
    Pipeline indices match ComputeRenderer3D::ShaderCompileStep() in
    src/GPU3D_Compute.cpp one for one.

    The SPIR-V words live in per-stage translation units rather than in the
    header so that including the public header stays cheap: the full module set
    is a few megabytes of integer literals and would otherwise be re-parsed by
    every consumer.
*/
"""


def build_header(pipeline_names: list[str], blobs: list[list[int]],
                 table: list[list[int]]) -> str:
    out: list[str] = [LICENSE_HEADER, GENERATED_BANNER.lstrip("\n")]
    out.append("""
#ifndef GPU3D_VULKAN_SHADERMODULES_H
#define GPU3D_VULKAN_SHADERMODULES_H

#include <cstdint>

namespace melonDS
{
namespace VulkanShaders
{
""".lstrip("\n"))

    out.append("// Descriptor set 0 bindings (see src/GPU3D_Vulkan_shaders/Common.glsl).")
    out.append("enum Set0Binding : uint32_t\n{")
    for name, binding in vss.SET0_BINDINGS:
        out.append(f"    Set0Binding_{name} = {binding},")
    out.append(f"    Set0Binding_Count = {len(vss.SET0_BINDINGS)},")
    out.append("};\n")

    out.append("// Descriptor set 1 bindings (sampled images).")
    out.append("enum Set1Binding : uint32_t\n{")
    for name, binding in vss.SET1_BINDINGS:
        out.append(f"    Set1Binding_{name} = {binding},")
    out.append(f"    Set1Binding_Count = {len(vss.SET1_BINDINGS)},")
    out.append("};\n")

    out.append("// Specialization constant ids declared by Common.glsl.")
    out.append("enum SpecConstantId : uint32_t\n{")
    for name, cid in vss.SPEC_CONSTANTS:
        out.append(f"    SpecConstantId_{name} = {cid},")
    out.append("};\n")

    out.append("// Push constant block, 32 bytes, VK_SHADER_STAGE_COMPUTE_BIT.")
    out.append("""struct PushConstants
{
    uint32_t CurVariant;
    uint32_t TexWidth;
    uint32_t TexHeight;
    uint32_t TexWrapS;
    uint32_t TexWrapT;
    uint32_t TexIsCapture;
    int32_t  CaptureYOffset;
    uint32_t Pad;
};
static_assert(sizeof(PushConstants) == %d, "push constant block must stay %d bytes");
""" % (vss.PUSH_CONSTANT_SIZE, vss.PUSH_CONSTANT_SIZE))

    out.append("enum Pipeline : uint32_t\n{")
    for index, name in enumerate(pipeline_names):
        out.append(f"    Pipeline_{name} = {index},")
    out.append(f"    Pipeline_Count = {len(pipeline_names)},")
    out.append("};\n")

    out.append("""// Tile geometry bucket. Mirrors `range` in
// ComputeRenderer3D::SetRenderSettings(): the four constants below must be
// compile-time literals, so one module set exists per bucket.
struct TileGeometry
{
    uint32_t TileSize;
    uint32_t CoarseTileCountY;
    uint32_t CoarseTileArea;
    uint32_t ClearCoarseBinMaskLocalSize;
};
""")
    out.append(f"inline constexpr uint32_t TileGeometryBucketCount = {vss.BUCKET_COUNT};")
    out.append("inline constexpr TileGeometry TileGeometryBuckets[TileGeometryBucketCount] =\n{")
    for bucket in range(vss.BUCKET_COUNT):
        geo = vss.tile_geometry(bucket)
        out.append("    {{ {TileSize}, {CoarseTileCountY}, {CoarseTileArea}, "
                   "{ClearCoarseBinMaskLocalSize} }},".format(**geo))
    out.append("};\n")

    out.append("""inline constexpr uint32_t TileGeometryBucketForScale(uint32_t scale)
{
    return (scale >= 5 ? 1u : 0u) + (scale >= 9 ? 1u : 0u);
}

struct ShaderModule
{
    const uint32_t* Words;
    uint32_t WordCount;
};
""")

    out.append("""// [tile geometry bucket][pipeline]. Defined in
// generated/GPU3D_Vulkan_ShaderModules.cpp; identical modules are shared, so
// two entries may point at the same words.
extern const ShaderModule Modules[TileGeometryBucketCount][Pipeline_Count];
extern const char* const PipelineNames[Pipeline_Count];
""")

    out.append("""} // namespace VulkanShaders
} // namespace melonDS

#endif // GPU3D_VULKAN_SHADERMODULES_H""")

    return "\n".join(out) + "\n"


def build_blobs_header(blobs: list[list[int]]) -> str:
    """Internal header: declares every SPIR-V blob and its word count."""
    out: list[str] = [LICENSE_HEADER, GENERATED_BANNER.lstrip("\n")]
    out.append("""
#ifndef GPU3D_VULKAN_SHADERBLOBS_H
#define GPU3D_VULKAN_SHADERBLOBS_H

#include <cstdint>

// Internal to the generated shader module set. Consumers should include
// GPU3D_Vulkan_ShaderModules.h instead.

namespace melonDS
{
namespace VulkanShaders
{
namespace detail
{
""".lstrip("\n"))
    for index, words in enumerate(blobs):
        out.append(f"extern const uint32_t Blob{index:03d}[{len(words)}];")
        out.append(f"inline constexpr uint32_t Blob{index:03d}_Words = {len(words)}u;")
    out.append("""
} // namespace detail
} // namespace VulkanShaders
} // namespace melonDS

#endif // GPU3D_VULKAN_SHADERBLOBS_H""")
    return "\n".join(out) + "\n"


def build_blob_source(stem: str, entries: list[tuple[int, list[int]]]) -> str:
    """One translation unit per .comp stem holding that stage's SPIR-V words."""
    out: list[str] = [LICENSE_HEADER, GENERATED_BANNER.lstrip("\n")]
    out.append(f'// SPIR-V for src/GPU3D_Vulkan_shaders/{stem}.comp.\n')
    out.append('#include "GPU3D_Vulkan_ShaderBlobs.h"\n')
    out.append("namespace melonDS\n{\nnamespace VulkanShaders\n{\nnamespace detail\n{\n")
    for index, words in entries:
        out.append(format_blob(f"Blob{index:03d}", words))
        out.append("")
    out.append("} // namespace detail\n} // namespace VulkanShaders\n} // namespace melonDS")
    return "\n".join(out) + "\n"


def build_table_source(pipeline_names: list[str], blobs: list[list[int]],
                       table: list[list[int]]) -> str:
    out: list[str] = [LICENSE_HEADER, GENERATED_BANNER.lstrip("\n")]
    out.append('#include "GPU3D_Vulkan_ShaderModules.h"')
    out.append('#include "GPU3D_Vulkan_ShaderBlobs.h"\n')
    out.append("namespace melonDS\n{\nnamespace VulkanShaders\n{\n")
    out.append("const ShaderModule Modules[TileGeometryBucketCount][Pipeline_Count] =\n{")
    for bucket in range(vss.BUCKET_COUNT):
        geo = vss.tile_geometry(bucket)
        out.append(f"    // bucket {bucket}: TileSize {geo['TileSize']}, "
                   f"CoarseTileCountY {geo['CoarseTileCountY']}, "
                   f"CoarseTileArea {geo['CoarseTileArea']}, "
                   f"ClearCoarseBinMaskLocalSize {geo['ClearCoarseBinMaskLocalSize']}")
        out.append("    {")
        for pipeline in range(len(pipeline_names)):
            blob = table[bucket][pipeline]
            out.append(f"        {{ detail::Blob{blob:03d}, detail::Blob{blob:03d}_Words }},"
                       f" // {pipeline_names[pipeline]}")
        out.append("    },")
    out.append("};\n")

    out.append("const char* const PipelineNames[Pipeline_Count] =\n{")
    for name in pipeline_names:
        out.append(f"    \"{name}\",")
    out.append("};\n")
    out.append("} // namespace VulkanShaders\n} // namespace melonDS")
    return "\n".join(out) + "\n"


def write_if_changed(path: str, content: str, check_only: bool) -> bool:
    """Returns True when the file on disk already matched."""
    existing = None
    if os.path.isfile(path):
        with open(path, "r", encoding="utf-8", newline="") as handle:
            existing = handle.read()
    if existing == content:
        return True
    if not check_only:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(content)
    return False


def source_set_sha256() -> str:
    """Hash every input that can change which shader modules are generated.

    Line endings are normalized so Windows and Unix checkouts produce the same
    fingerprint. Including this generator and vulkan_shader_set.py catches
    changes to pipeline ordering, injected defines and emitted module layout in
    addition to edits of the GLSL itself.
    """
    generator_dir = os.path.dirname(os.path.abspath(__file__))
    paths = [
        os.path.abspath(__file__),
        os.path.join(generator_dir, "vulkan_shader_set.py"),
    ]
    for extension in ("*.comp", "*.glsl"):
        paths.extend(glob.glob(os.path.join(vss.SHADER_DIR, extension)))

    digest = hashlib.sha256()
    for path in sorted(set(paths)):
        relative = os.path.relpath(path, vss.REPO_ROOT).replace(os.sep, "/")
        with open(path, "rb") as handle:
            content = handle.read().replace(b"\r\n", b"\n").replace(b"\r", b"\n")
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(content)
        digest.update(b"\0")
    return digest.hexdigest()


def expected_variant_metadata() -> list[dict[str, object]]:
    result: list[dict[str, object]] = []
    for bucket in range(vss.BUCKET_COUNT):
        geometry = vss.tile_geometry(bucket)
        for index, (name, stem, defines) in enumerate(vss.pipelines()):
            result.append({
                "pipeline": index,
                "name": name,
                "bucket": bucket,
                "source": f"src/GPU3D_Vulkan_shaders/{stem}.comp",
                "entryPoint": "main",
                "defines": sorted(defines),
                "geometryDefines": geometry,
            })
    return result


def verify_committed_source_sync(expected_source_hash: str) -> int:
    """Verify generated-source freshness without depending on glslang output."""
    manifest_path = os.path.join(vss.GENERATED_DIR, MANIFEST_NAME)
    failures: list[str] = []
    try:
        with open(manifest_path, "r", encoding="utf-8") as handle:
            manifest = json.load(handle)
    except (OSError, json.JSONDecodeError) as error:
        print(f"FAIL: cannot read {manifest_path}: {error}")
        return 1

    if manifest.get("sourceSetSha256") != expected_source_hash:
        failures.append(
            "shader/generator inputs changed after the committed output was generated"
        )

    expected_contract = {
        "generator": "tools/vulkan/compile-shaders.py",
        "targetEnv": "vulkan1.1",
        "entryPoint": "main",
        "pipelineCount": len(vss.pipelines()),
        "bucketCount": vss.BUCKET_COUNT,
        "bucketRepresentativeScale": vss.BUCKET_REPRESENTATIVE_SCALE,
        "specializationConstants": [
            {"name": name, "id": cid} for name, cid in vss.SPEC_CONSTANTS
        ],
        "pushConstantBytes": vss.PUSH_CONSTANT_SIZE,
        "set0Bindings": [
            {"name": name, "binding": binding}
            for name, binding in vss.SET0_BINDINGS
        ],
        "set1Bindings": [
            {"name": name, "binding": binding}
            for name, binding in vss.SET1_BINDINGS
        ],
    }
    for key, expected in expected_contract.items():
        if manifest.get(key) != expected:
            failures.append(f"manifest field {key!r} does not match the generator contract")

    variants = manifest.get("variants")
    expected_metadata = expected_variant_metadata()
    if not isinstance(variants, list) or len(variants) != len(expected_metadata):
        failures.append("manifest variant list has the wrong size")
        variants = []
    else:
        metadata_keys = tuple(expected_metadata[0])
        actual_metadata = [
            {key: variant.get(key) for key in metadata_keys}
            for variant in variants
        ]
        if actual_metadata != expected_metadata:
            failures.append("manifest pipeline/bucket metadata is stale")

    listed_files = manifest.get("generatedFiles")
    if not isinstance(listed_files, list):
        failures.append("manifest generatedFiles is missing or invalid")
        listed_files = []
    actual_files = sorted(
        os.path.basename(path)
        for path in glob.glob(os.path.join(vss.GENERATED_DIR, "GPU3D_Vulkan_Shader*"))
        if os.path.isfile(path)
    )
    actual_files.append(MANIFEST_NAME)
    actual_files.sort()
    if sorted(listed_files) != actual_files:
        failures.append("manifest generatedFiles does not match the committed file set")

    expected_blob_hashes: dict[int, str] = {}
    for variant in variants:
        blob = variant.get("blob")
        digest = variant.get("sha256")
        if not isinstance(blob, int) or not isinstance(digest, str):
            failures.append("manifest contains an invalid blob reference")
            continue
        previous = expected_blob_hashes.setdefault(blob, digest)
        if previous != digest:
            failures.append(f"manifest assigns conflicting hashes to blob {blob}")

    blob_pattern = re.compile(
        r"const uint32_t Blob(\d{3})\[(\d+)\] = \{(.*?)\};",
        re.DOTALL,
    )
    word_pattern = re.compile(r"0x[0-9a-fA-F]{8}")
    committed_blobs: dict[int, list[int]] = {}
    for path in glob.glob(os.path.join(
            vss.GENERATED_DIR, "GPU3D_Vulkan_ShaderBlobs_*.cpp")):
        with open(path, "r", encoding="utf-8", newline="") as handle:
            source = handle.read()
        for match in blob_pattern.finditer(source):
            blob = int(match.group(1))
            declared_words = int(match.group(2))
            words = [int(token, 16) for token in word_pattern.findall(match.group(3))]
            if len(words) != declared_words:
                failures.append(
                    f"blob {blob} declares {declared_words} words but contains {len(words)}"
                )
            if blob in committed_blobs:
                failures.append(f"blob {blob} is defined more than once")
            committed_blobs[blob] = words

    unique_modules = manifest.get("uniqueModules")
    if not isinstance(unique_modules, int):
        failures.append("manifest uniqueModules is missing or invalid")
        unique_modules = -1
    expected_blob_ids = set(range(max(unique_modules, 0)))
    if set(committed_blobs) != expected_blob_ids:
        failures.append("committed blob definitions do not match manifest uniqueModules")
    if set(expected_blob_hashes) != expected_blob_ids:
        failures.append("manifest variants do not reference every committed blob")

    total_bytes = 0
    for blob, words in committed_blobs.items():
        total_bytes += len(words) * 4
        if not words or words[0] != 0x07230203:
            failures.append(f"blob {blob} has an invalid SPIR-V magic number")
            continue
        packed = struct.pack(f"<{len(words)}I", *words)
        actual_digest = hashlib.sha256(packed).hexdigest()
        if expected_blob_hashes.get(blob) != actual_digest:
            failures.append(f"blob {blob} does not match its manifest SHA-256")
    if manifest.get("totalSpirvBytes") != total_bytes:
        failures.append("manifest totalSpirvBytes does not match committed blobs")

    if failures:
        print("FAIL: committed Vulkan shader output is not source-synchronized:")
        for failure in failures:
            print(f"  {failure}")
        print("Rerun tools/vulkan/compile-shaders.py with the canonical glslang toolchain.")
        return 1

    print(f"PASS: source fingerprint {expected_source_hash}")
    print(f"PASS: {len(committed_blobs)} committed SPIR-V modules match manifest hashes")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--glslang", default=None, help="path to glslangValidator")
    check_group = parser.add_mutually_exclusive_group()
    check_group.add_argument("--check", action="store_true",
                             help="recompile and byte-compare committed output")
    check_group.add_argument(
        "--check-source-sync", action="store_true",
        help="verify source fingerprint and committed blob integrity without recompiling")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    source_hash = source_set_sha256()
    if args.check_source_sync:
        return verify_committed_source_sync(source_hash)

    glslang = vss.find_tool("glslangValidator", args.glslang)
    if not glslang:
        print("ERROR: glslangValidator was not found. Install the Vulkan SDK, or "
              "pass --glslang PATH.")
        return 1
    print(f"glslangValidator: {glslang}")

    pipeline_list = vss.pipelines()
    pipeline_names = [name for name, _, _ in pipeline_list]

    blobs: list[list[int]] = []
    blob_stem: list[str] = []
    blob_index_by_hash: dict[str, int] = {}
    table = [[0] * len(pipeline_list) for _ in range(vss.BUCKET_COUNT)]
    manifest_entries: list[dict[str, object]] = []
    failures = 0

    with tempfile.TemporaryDirectory(prefix="melonprime-vk-spv-") as tmpdir:
        out_path = os.path.join(tmpdir, "shader.spv")

        for bucket in range(vss.BUCKET_COUNT):
            geometry = vss.tile_geometry(bucket)
            for index, (name, stem, defines) in enumerate(pipeline_list):
                source = os.path.join(vss.SHADER_DIR, stem + ".comp")
                all_defines: dict[str, object] = dict(geometry)
                for define in defines:
                    all_defines[define] = None

                ok, diagnostics = compile_one(glslang, source, all_defines, out_path)
                if not ok:
                    failures += 1
                    print(f"[FAIL] bucket={bucket} {name}")
                    print(diagnostics)
                    continue

                words = read_spirv(out_path)
                digest = hashlib.sha256(
                    struct.pack(f"<{len(words)}I", *words)).hexdigest()
                blob = blob_index_by_hash.get(digest)
                if blob is None:
                    blob = len(blobs)
                    blob_index_by_hash[digest] = blob
                    blobs.append(words)
                    # A blob can only ever be produced by one .comp, so the
                    # first-encounter stem owns its translation unit.
                    blob_stem.append(stem)
                table[bucket][index] = blob

                manifest_entries.append({
                    "pipeline": index,
                    "name": name,
                    "bucket": bucket,
                    "source": f"src/GPU3D_Vulkan_shaders/{stem}.comp",
                    "entryPoint": "main",
                    "defines": sorted(defines),
                    "geometryDefines": geometry,
                    "blob": blob,
                    "words": len(words),
                    "sha256": digest,
                })

                if args.verbose:
                    print(f"[ ok ] bucket={bucket} {name:34s} "
                          f"{len(words) * 4:7d} bytes  blob={blob:3d}")

    if failures:
        print(f"\nFAIL: {failures} of {vss.BUCKET_COUNT * len(pipeline_list)} "
              f"shader variants failed to compile")
        return 1

    outputs: dict[str, str] = {
        HEADER_NAME: build_header(pipeline_names, blobs, table),
        BLOBS_HEADER_NAME: build_blobs_header(blobs),
        TABLE_SOURCE_NAME: build_table_source(pipeline_names, blobs, table),
    }
    for stem in sorted(set(blob_stem)):
        entries = [(index, words) for index, words in enumerate(blobs)
                   if blob_stem[index] == stem]
        outputs[f"GPU3D_Vulkan_ShaderBlobs_{stem}.cpp"] = build_blob_source(stem, entries)

    manifest = {
        "generator": "tools/vulkan/compile-shaders.py",
        "sourceSetSha256": source_hash,
        "targetEnv": "vulkan1.1",
        "entryPoint": "main",
        "pipelineCount": len(pipeline_list),
        "bucketCount": vss.BUCKET_COUNT,
        "bucketRepresentativeScale": vss.BUCKET_REPRESENTATIVE_SCALE,
        "specializationConstants": [
            {"name": name, "id": cid} for name, cid in vss.SPEC_CONSTANTS
        ],
        "pushConstantBytes": vss.PUSH_CONSTANT_SIZE,
        "set0Bindings": [{"name": n, "binding": b} for n, b in vss.SET0_BINDINGS],
        "set1Bindings": [{"name": n, "binding": b} for n, b in vss.SET1_BINDINGS],
        "uniqueModules": len(blobs),
        "totalSpirvBytes": sum(len(words) for words in blobs) * 4,
        "generatedFiles": sorted(outputs) + [MANIFEST_NAME],
        "variants": manifest_entries,
    }
    outputs[MANIFEST_NAME] = json.dumps(manifest, indent=2, sort_keys=False) + "\n"

    stale: list[str] = []
    for name in sorted(outputs):
        path = os.path.join(vss.GENERATED_DIR, name)
        if not write_if_changed(path, outputs[name], args.check):
            stale.append(name)

    total = vss.BUCKET_COUNT * len(pipeline_list)
    print(f"compiled {total} variants "
          f"({len(pipeline_list)} pipelines x {vss.BUCKET_COUNT} tile geometry buckets)")
    print(f"unique SPIR-V modules: {len(blobs)}  "
          f"({sum(len(w) for w in blobs) * 4} bytes)")

    if args.check:
        if stale:
            print("\nFAIL: generated output is stale, rerun "
                  "tools/vulkan/compile-shaders.py:")
            for name in stale:
                print(f"  src/GPU3D_Vulkan_shaders/generated/{name}")
            return 1
        print(f"PASS: all {len(outputs)} committed generated files are up to date")
        return 0

    for name in sorted(outputs):
        marker = "updated" if name in stale else "unchanged"
        print(f"  {marker:9s} src/GPU3D_Vulkan_shaders/generated/{name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
