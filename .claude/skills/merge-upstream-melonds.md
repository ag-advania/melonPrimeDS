# Merge Latest Commits from melonDS Upstream

## Scope

Use this skill when the user asks to "merge upstream", "pull from melonDS", or wants to incorporate the latest fixes from `melonDS-emu/melonDS:master` into this fork.

## Remote Setup (one-time)

The `upstream` remote should already be configured. Verify with `git remote -v`:

```
origin    https://github.com/ag-advania/melonPrimeDS.git
upstream  https://github.com/melonDS-emu/melonDS.git
```

If `upstream` is missing, add it:

```bash
git remote add upstream https://github.com/melonDS-emu/melonDS.git
```

## Procedure

### 1. Verify clean working tree and fetch upstream

```bash
git status --short
git fetch upstream master
```

If the working tree has uncommitted changes, stop and ask the user.

### 2. List the new upstream commits

```bash
git log --oneline HEAD..upstream/master
```

If empty, there is nothing to merge — report and stop.

### 3. Identify upstream's actual code changes

Filter out fork-only paths to see only the shared files that upstream changed. Adjust the exclusion list when new fork-only directories are added.

```bash
OLDEST=$(git log --oneline HEAD..upstream/master | tail -1 | cut -d' ' -f1)
git diff --stat ${OLDEST}^..upstream/master \
  -- ':!.claude' ':!.codex' ':!CLAUDE.md' ':!.gitignore' ':!res/' \
     ':!src/frontend/qt_sdl/MelonPrime*' \
     ':!src/frontend/qt_sdl/InputConfig/MelonPrime*'
```

This shows only the non-MelonPrime, non-fork-asset files that upstream actually touched. These are the conflict candidates.

### 4. Check if the fork has modified those same files recently

For each shared file from step 3, check whether the fork has divergent edits. If both sides modified the same file, expect a merge conflict.

```bash
git diff --stat HEAD~50..HEAD -- src/path/to/SharedFile.cpp
```

### 5. Run the merge

Use `-X ours` so deleted-in-fork files (CI workflows, fork-stripped assets) stay deleted instead of resurrecting upstream's version.

```bash
git merge upstream/master -X ours --no-edit \
  -m "Merge upstream melonDS master (<one-line summary of upstream commits>)"
```

Compose the commit message summary from the actual upstream commit log (e.g., "SPU buffer leak fix, Platform.cpp QFile" or "RTC sync, console attach, FreeBIOS").

### 6. Resolve `modify/delete` conflicts (CI workflows)

Upstream's `.github/workflows/build-bsd.yml`, `build-macos.yml`, `build-ubuntu.yml` are deleted in this fork. If the merge surfaces them as conflicts, keep the deletion:

```bash
git rm .github/workflows/build-bsd.yml \
       .github/workflows/build-macos.yml \
       .github/workflows/build-ubuntu.yml
```

### 7. Verify upstream changes were applied

`-X ours` preserves the fork side for content conflicts in shared files, which can silently drop small upstream additions when the auto-merger picks the fork's version. Spot-check that the new upstream changes are present:

```bash
git log --oneline -3
grep -n "<a symbol added by upstream>" src/path/to/SharedFile.cpp
```

If a small upstream addition (single-line config default, single-line function call, etc.) is missing, apply it manually with `Edit` and amend or follow up with another commit.

Common manual reapplications observed historically:

- `Config.cpp` `DefaultBools` — single-line additions land near the end of the list
- `EmuThread.cpp` — single-line function calls inside the run loop
- `main.cpp` — Windows `#ifdef _WIN32` blocks where the fork has its own preceding `#if QT_VERSION_MAJOR == ...` block

### 8. Build verification (if user requests)

Run the standard MinGW build to confirm the merge compiles. See [build-windows-mingw.md](build-windows-mingw.md).

```
.\.claude\skills\build-mingw.bat
```

If a previous fork commit added member access from a non-member function, the merge can re-expose that as a private-access error after upstream changes class layout. Add a public accessor or move the call inside a member function.

### 9. Finalize the commit

The merge commit was created by `git merge` itself in step 5. Confirm with:

```bash
git log --oneline -1
```

Do not push without the user explicitly asking.

## Common Pitfalls

- **`#ifdef _WIN32` block dropped by auto-merge in `main.cpp`** — when both sides edit nearby Windows-specific code, the auto-merger has dropped the `#ifdef _WIN32` opener while keeping the `#endif`. After merging, grep `main.cpp` for orphan `#endif` lines or build it.
- **`ARM9InstructionHookMaxAddresses` and `FindDispatchMask` switch coverage** — if upstream changes invalidate the JIT cache or change hook behavior, ensure both switches in `MelonPrimeArm9Hook.cpp` and `MelonPrimeArm9InstructionHook.inc` still cover the current address count.
- **CI files (`build-bsd.yml` etc.)** — always re-delete after upstream resurrects them; the fork is Windows MinGW only.
- **`build-windows.yml`** — preserve the fork's MSYS2 cache configuration; merge only the trigger-list and action-version updates from upstream. See the prior merge commit `2375253d` for an example of cherry-picking only the workflow improvements.

## What NOT to take from upstream

- The full `.github/workflows/build-windows.yml` rewrite (upstream switched to Windows SDK + Clang; the fork uses MinGW)
- Ubuntu / macOS / BSD CI workflows
- Any `.codex/` or upstream `.claude/` configuration

## Reference: prior merges

| Commit | What it pulled in |
|---|---|
| `e77cb43f` | Segfault fix, audio skew, FreeBIOS IRQ fix, RTC sync, console attach |
| `d44fe08f` | SPU output buffer leak fix, Platform.cpp QFile abstraction |
