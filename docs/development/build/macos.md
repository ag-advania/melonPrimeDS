# macOS Build

Use this when an AI agent needs to build MelonPrimeDS on macOS and should not reconstruct the command by hand.

## Dev Build (canonical)

Fixed dev configure + build. Run from anywhere:

```zsh
./tools/build/macos/build-macos-dev.sh
```

Finder / double-click (macOS):

```zsh
open tools/build/macos/build-macos-dev.command
```

Incremental rebuild only (existing `build-mac` tree):

```zsh
open tools/build/macos/build-macos-dev-existing.command
```

This is the checked-in form of:

```zsh
cd /Users/admin/git/melonPrimeDS && cmake -B build-mac -G Ninja -DCMAKE_BUILD_TYPE=Release -DMELONPRIME_ENABLE_DEVELOPER_FEATURES=ON -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix libarchive)" -DUSE_QT6=ON && cmake --build build-mac --parallel 4 2>&1
```

The script resolves the repo root from its own path instead of hard-coding the path above.

Build-only when `build-mac` is already configured:

```zsh
./tools/build/macos/build-macos-dev-existing.sh
```

Raw equivalent:

```zsh
cd /Users/admin/git/melonPrimeDS && cmake --build build-mac --parallel 4 2>&1
```

## Options Wrapper

For non-default jobs, release builds, or `--open`:

```zsh
./tools/build/macos/build-macos.sh
./tools/build/macos/build-macos.sh --jobs 8
./tools/build/macos/build-macos.sh --release
./tools/build/macos/build-macos.sh --build-only
./tools/build/macos/build-macos.sh --open
```

Prefer `build-macos-dev.sh` for the normal local dev build unless the user asks for another configuration.

## Dependencies

Install Homebrew packages once:

```zsh
brew install cmake ninja pkgconf sdl2 qt libarchive enet zstd faad2 libslirp
```

Use Homebrew dependencies directly; do not use vcpkg for the local macOS build.

## Output

`build-mac/melonPrimeDS.app`

Launch:

```zsh
open build-mac/melonPrimeDS.app
```

## GitHub Actions

CI uses `.github/workflows/build-macos.yml` with
`MELONPRIME_ENABLE_DEVELOPER_FEATURES=OFF`.
