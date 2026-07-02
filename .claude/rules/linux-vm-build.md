# Linux VM Build (VirtualBox + Ubuntu 22.04)

Native Linux build and runtime testing on an Intel Mac host using VirtualBox.
Matches CI dependencies from `.github/workflows/build-ubuntu.yml`.

## Prerequisites (Mac host)

- [VirtualBox](https://www.virtualbox.org/) 7.x + Extension Pack
- Ubuntu 22.04.5 Desktop amd64 ISO (e.g. `~/Downloads/ubuntu-22.04.5-desktop-amd64.iso`)
- Repo checked out at `/Users/admin/git/MelonPrimeDS` (shared into the guest)
- Run scripts from **Terminal.app** (VirtualBox COM server does not work from Cursor's shell)

## Quick start — execution order

Scripts live in `tools/linux-vm/`. Run in order:

| Step | Script | Notes |
|------|--------|-------|
| **01** | `01-install-ubuntu.command` | Creates `MelonPrimeDS-Ubuntu2204`, unattended install (~15–40 min) |
| **02** | `02-guest-finish.command` | Guest Additions; VM may reboot |
| **03** | `03-fix-shared-folder.command` | **Optional** — if MelonPrimeDS share won't open |
| **04** | `04-guest-build.command` | cmake + ninja → `build-linux/melonPrimeDS` |

Default guest login: `melon` / `melon` (fixed; scripts do not prompt)

At the Ubuntu login screen choose **Ubuntu on Xorg** (not Wayland-only) for XInput2 raw aim.

## After build — run in guest

```bash
cd /mnt/mp/build-linux
./melonPrimeDS
```

Japanese UI smoke test:

```bash
export LANG=ja_JP.UTF-8
./melonPrimeDS
```

Shared folder paths (first match wins):

- `/mnt/mp` — manual vboxsf mount (Mac build scripts use this)
- `/media/sf_MelonPrimeDS` — VirtualBox automount
- `/media/melon/MelonPrimeDS` — custom automount (legacy)

## Manual guest commands (paste-friendly)

If Mac-side `guestcontrol` automation fails, run inside Ubuntu (Ctrl+Alt+T):

```bash
sudo modprobe vboxsf
sudo mkdir -p /mnt/mp
sudo mount -t vboxsf MelonPrimeDS /mnt/mp
bash /mnt/mp/tools/linux-vm/guest/guest-build-only.sh
```

Full deps + build (first time or after clean):

```bash
bash /mnt/mp/tools/linux-vm/guest/guest-setup-and-build.sh /mnt/mp
```

Smoke checks:

```bash
bash /mnt/mp/tools/linux-vm/guest/guest-run-smoke.sh /mnt/mp
```

## Directory layout

```
tools/linux-vm/
  README.md
  01-install-ubuntu.{command,sh}
  02-guest-finish.{command,sh}      → 02-guest-finish-from-host.sh
  03-fix-shared-folder.{command,sh}
  04-guest-build.{command,sh}       → 04-guest-build-from-host.sh
  lib/
    vbox-guest-common.sh            # shared host helpers
    vbox-create-ubuntu.sh           # manual VM create (no unattended)
    vbox-mount-guest-additions.sh
    vbox-post-install-host.sh
  guest/
    guest-build-only.sh             # cmake + build (no apt)
    guest-setup-and-build.sh        # apt deps + full build
    guest-finish-noninteractive.sh
    guest-finish-vbox-setup.sh
    guest-mount-share.sh
    guest-run-smoke.sh
    guest-build.sh                  # wrapper → guest-setup-and-build
```

## Build flags (guest)

Local VM builds use:

- `-DMELONPRIME_ENABLE_DEVELOPER_FEATURES=ON`
- `-DMELONDS_EMBED_BUILD_INFO=ON` with git branch/hash and `MELONDS_BUILD_PROVIDER=LinuxVM`
- Output: `build-linux/melonPrimeDS` (standalone binary, not AppDir)

CI Ubuntu workflow uses the same dependency set; see [build.md](build.md) Linux platform notes for aim/input behavior.

## Linux runtime notes

- Aim path: `MelonPrimeRawInputLinuxFilter` (XInput2 on X11)
- Wayland falls back to QCursor center-delta
- Japanese UI: `IsJapaneseSystemLocale()` + `LANG=ja_JP.UTF-8`; see locale fixes in `MelonPrimeLocalization.cpp`

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `VBoxManage: Failed to create VirtualBox object` | Open VirtualBox.app from Finder; use Terminal.app |
| VM state `poweroff` not starting | Fixed in scripts — re-run step 01 or 02 |
| Shared folder icon won't open | Step 03, then `guest/guest-mount-share.sh` in guest |
| `guestcontrol` waits forever | Log into Ubuntu **desktop** first |
| CMake embed build info error | Use `guest/guest-build-only.sh` (clears stale cache, passes `-D` flags) |
| `Protocol error` on `/mnt/mp` | Share is already mounted; build can still proceed |

## Alternative: git clone inside guest

If shared folders never work:

```bash
git clone https://github.com/ag-advania/melonPrimeDS.git ~/MelonPrimeDS
bash ~/MelonPrimeDS/tools/linux-vm/guest/guest-setup-and-build.sh ~/MelonPrimeDS
```

This does not see uncommitted Mac-side changes.
