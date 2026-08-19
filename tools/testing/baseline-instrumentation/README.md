# b4aec Software frame-dump provenance

This directory records the instrumentation used to obtain the independent
Software baseline dumps. The instrumentation is developer-only and read-only:
it copies the already-produced 256x192 Top/Bottom framebuffer to the
`MP2DDUMP` file format and does not consume VRAM dirty state, alter emulation,
or generate a golden frame.

The OpenGL baseline uses the separate `b4aec-opengl-frame-dump.patch`. It
performs the same developer-only readback from the final Top/Bottom framebuffer
attachments; it is not part of the baseline production renderer. The
`b4aec-savestate-frame-dump-trigger.patch` adds only a developer-build marker
after a successful F-key state load, so the first dump is synchronized to the
requested post-savestate frame rather than an earlier startup VBlank.

Baseline source:

```text
parent SHA: b4aecb3869d0f983a073cfa8b1ee28567b5ab8d4
instrumentation patch: b4aec-software-frame-dump.patch
patch SHA-256: 8e134ec5b1ef6ba1fb1872b90e0c6278e343b3313f38e450abe4d4617371967e
git diff --binary SHA-256: 8e134ec5b1ef6ba1fb1872b90e0c6278e343b3313f38e450abe4d4617371967e
OpenGL instrumentation patch: b4aec-opengl-frame-dump.patch
OpenGL patch SHA-256: 9e86a9e592c0113ac78f42c838e8ee611deab1ccce57c4922fe1fab7b28a37d6
OpenGL git diff --binary SHA-256: 9e86a9e592c0113ac78f42c838e8ee611deab1ccce57c4922fe1fab7b28a37d6
Savestate trigger patch: b4aec-savestate-frame-dump-trigger.patch
Savestate trigger patch SHA-256: 85d6ba5ff991565f01373c62da607758717556929fd5331502106239ddad2c16
Savestate trigger git diff --binary --unified=0 SHA-256: 85d6ba5ff991565f01373c62da607758717556929fd5331502106239ddad2c16
```

Each patch hash and its corresponding `git diff --binary` hash are identical;
the savestate trigger uses zero context so the generated patch passes the
repository whitespace audit without carrying an otherwise meaningless blank
context line.
The patches were generated from and pass `git apply --check` in the clean
detached baseline worktree at
`build/gpu2d-baseline-worktree-20260820-clean` against that parent source.
The recorded binary below was built from the equivalent detached checkout at
`build/gpu2d-baseline-worktree-20260820`.

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
binary SHA-256: 0b24bbc34888bb9f5592019c1fb517c39411fd2bb340e59f80d81fc85866c987
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
environment (`MELONPRIME_TEST_GPU2D_FRAME_DUMP`, its frame limit, and
`MELONPRIME_TEST_GPU2D_FRAME_DUMP_AFTER_SAVESTATE`). The physical runner's
`-SkipDiagnosticStartupSavestate` option disables the separate diagnostic
startup load; the F-key action remains the real post-start state load and the
trigger marker gates the first dump. Any
physical run that used a dirty checkout or an unverified binary is diagnostic
evidence only and is not final acceptance evidence.
