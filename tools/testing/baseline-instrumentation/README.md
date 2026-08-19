# b4aec Software frame-dump provenance

This directory records the instrumentation used to obtain the independent
Software baseline dumps. The instrumentation is developer-only and read-only:
it copies the already-produced 256x192 Top/Bottom framebuffer to the
`MP2DDUMP` file format and does not consume VRAM dirty state, alter emulation,
or generate a golden frame.

The OpenGL baseline uses the separate `b4aec-opengl-frame-dump.patch`. It
performs the same developer-only readback from the final Top/Bottom framebuffer
attachments; it is not part of the baseline production renderer.

Baseline source:

```text
parent SHA: b4aecb3869d0f983a073cfa8b1ee28567b5ab8d4
instrumentation patch: b4aec-software-frame-dump.patch
patch SHA-256: a62f06d41040c2c312f66579b211331fb2b48c0ab494a5806e316feb0647386e
git diff --binary SHA-256: a62f06d41040c2c312f66579b211331fb2b48c0ab494a5806e316feb0647386e
OpenGL instrumentation patch: b4aec-opengl-frame-dump.patch
OpenGL patch SHA-256: e4fa7514b6c8acafc4f07100b9c59b76fb689a0fdcc647d57961e2fb437eebed
OpenGL git diff --binary SHA-256: e4fa7514b6c8acafc4f07100b9c59b76fb689a0fdcc647d57961e2fb437eebed
```

Each patch hash and its corresponding `git diff --binary` hash are identical.
The patches were generated from the detached baseline worktree at
`build/gpu2d-baseline-worktree-20260820` and passes `git apply --check` against
that parent source.

Build evidence for the binary used by the baseline Software dumps:

```text
build directory: build/gpu2d-baseline-build-20260820d
generator: Ninja 1.12.1
compiler: MSYS2 MinGW-w64 GCC 14.2.0 (C:/msys64/mingw64/bin/c++.exe)
build type: Release
developer features: ON
renderer perf telemetry: ON
Vulkan: ON
DX12: ON
build provider: physical-ab-baseline
embedded source SHA: b4aecb3869d0f983a073cfa8b1ee28567b5ab8d4
embedded git_dirty: false
binary SHA-256: 788d7d8641b9bd5e7e89991375dcffdf1b8a58af991917f00e7fd381fc154e14
```

Reproduction command (the remaining cache options are visible in the recorded
CMake cache):

```powershell
cmake -S C:\Users\Admin\Documents\git\melonPrimeDS\build\gpu2d-baseline-worktree-20260820 `
  -B C:\Users\Admin\Documents\git\melonPrimeDS\build\gpu2d-baseline-build-20260820d `
  -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DMELONDS_BUILD_PROVIDER=physical-ab-baseline `
  -DMELONDS_EMBED_BUILD_INFO=ON `
  -DMELONDS_GIT_HASH=b4aecb3869d0f983a073cfa8b1ee28567b5ab8d4 `
  -DMELONDS_GIT_BRANCH=gpu2d-baseline -DMELONDS_GIT_DIRTY=false `
  -DMELONPRIME_ENABLE_DEVELOPER_FEATURES=ON `
  -DMELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY=ON `
  -DMELONPRIME_ENABLE_VULKAN=ON -DMELONPRIME_ENABLE_DX12=ON
ninja -C C:\Users\Admin\Documents\git\melonPrimeDS\build\gpu2d-baseline-build-20260820d melonDS
```

The resulting baseline binary was run only with the explicit developer dump
environment (`MELONPRIME_TEST_GPU2D_FRAME_DUMP` and its frame limit). Any
physical run that used a dirty checkout or an unverified binary is diagnostic
evidence only and is not final acceptance evidence.
