# Build and Validation

## Supported entry points

- Windows AI builds must use `tools/build/windows/build-mingw.bat` from the repository root. Use `tools/build/windows/build-mingw-existing.bat` only for an already configured tree and only when CMake, presets, dependencies, toolchains, and feature flags did not change.
- Keep Windows AI builds at one job unless the user explicitly requests otherwise; parallel C++ compilation can exhaust memory. Do not bootstrap or reinstall `vcpkg` unless requested.
- Development builds configure `MELONPRIME_ENABLE_DEVELOPER_FEATURES=ON`; release/distribution builds explicitly use `OFF`.
- Platform-specific procedures live under [`docs/development/build/`](../../docs/development/build/).

## Standing audits

Run the checks relevant to the touched surface. The canonical CI audit paths are:

- `tools/ci/audits/audit-config-defaults.ps1`
- `tools/ci/audits/audit-hud-key-parity.ps1 -Strict`
- `tools/ci/audits/check-inc-ownership.ps1`
- `tools/ci/audits/audit-metroid-literal-budget.ps1 -Budget 1`
- `tools/ci/audits/audit-platform-scatter-budget.ps1 -Budget 22`
- `tools/ci/audits/audit-color-dialog-prefs.ps1`
- `tools/ci/audits/audit-melonprime-srp-performance.ps1`

After HUD schema changes, run `python tools/codegen/hud/generate-hud-prop-schema.py` and verify the generated `.inc` files and `docs/generated/hud/MelonPrimeHudPropSchemaPhase2a.md` are stable. Always run `git diff --check` before handoff.

Additional focused audits such as `audit-melonprime-thread-boundary.ps1 -Strict` and `audit-melonprime-instance-state.ps1 -Strict` apply when their ownership surface changes.

## Validation claims

- Windows, Ubuntu x86_64/aarch64, and macOS x86_64/arm64 CI results are separate claims. Passing one does not imply another.
- Runtime/manual smoke testing is distinct from compilation and static audits.
- HUD refactors claiming visual identity should use the developer golden harness and must not change the golden hashes. Intentional visual changes require explicit before/after evidence.
