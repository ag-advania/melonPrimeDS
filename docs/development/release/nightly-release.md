# Nightly Release

Every push to `develop` builds all four platforms and republishes a single
fixed prerelease. It can also be started by hand from the Actions tab
(`workflow_dispatch`).

- Tag: `nightly-release` (moved to the `develop` commit that was built)
- Release name: `Nightly Build`
- Always `prerelease: true`
- URL: <https://github.com/ag-advania/melonPrimeDS/releases/tag/nightly-release>

Formal release tags and the `main` branch are untouched by this workflow.

## Shape

`.github/workflows/nightly-release.yml` builds nothing itself. It calls the four
existing build workflows through `workflow_call`, so nightly binaries come from
exactly the same steps (and the same audit gates) as normal CI:

```text
nightly-release.yml
  ├─ build-windows.yml   →  melonPrimeDS.exe
  ├─ build-macos.yml     →  melonPrimeDS-macOS-all.zip
  ├─ build-ubuntu.yml    →  melonPrimeDS-linux-all.zip
  ├─ build-bsd.yml       →  melonPrimeDS-bsd-all.zip
  └─ publish  (needs all four)
```

The build workflows keep their own `push` / `pull_request` triggers; the added
`workflow_call` is purely additive. They do not run on `develop` by themselves,
so a develop push produces one nightly run, not five.

## Published assets

| Asset | Origin |
|---|---|
| `melonPrimeDS-windows-x86_64.zip` | Created by the publish job from the `melonPrimeDS.exe` artifact |
| `melonPrimeDS-macOS-all.zip` | Published unchanged from build-macos.yml |
| `melonPrimeDS-linux-all.zip` | Published unchanged from build-ubuntu.yml |
| `melonPrimeDS-bsd-all.zip` | Published unchanged from build-bsd.yml |
| `SHA256SUMS.txt` | Generated over the four ZIPs above |

Bundling stays owned by the per-OS workflows. The publish job downloads each
`*-all.zip` with `skip-decompress` and attaches the identical file — it never
unpacks or repacks a bundle. Windows is the one exception, because
build-windows.yml uploads the bare executable.

## Why the payload is verified before publishing

The Linux and BSD build jobs, and both `all-artifacts` bundlers, run with
`continue-on-error: true` so that one flaky VM does not fail the whole matrix.
That means a run where, say, the NetBSD VM died still reports success and still
uploads a `melonPrimeDS-bsd-all.zip` — one containing two binaries instead of
three. `needs:` alone cannot catch this.

`tools/ci/release/verify-nightly-assets.py` is the gate, and it runs before any
tag, release, or asset is touched:

- each of the four ZIPs exists and is non-empty
- each ZIP opens, passes a CRC check, and contains no zero-length member
- each `*-all.zip` contains every architecture it is supposed to
- nothing unexpected is staged for upload
- `SHA256SUMS.txt` lists every published file with a matching digest, and
  nothing else

A non-zero exit fails the job before the publish steps, so the previous good
Nightly release stays exactly as it was.

Run it locally against a downloaded payload with:

```bash
python tools/ci/release/verify-nightly-assets.py --dist dist --write-sums
```

## Publish order

Only after verification passes:

1. `nightly-release` tag is force-moved to the built commit (created if absent)
2. The `Nightly Build` release is updated, or created if absent
3. Assets are uploaded with `--clobber`, replacing same-named files in place
4. Assets left over from an older naming scheme are deleted — last, so a
   failure part-way through never strips a working release of its downloads

The publish job carries `permissions: contents: write` (the workflow default is
`contents: read`) and is guarded by `github.repository == 'ag-advania/melonPrimeDS'`
so a fork cannot publish.
