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
| **05** | `05-mount-share.command` | **Shared folder only** — no build (after reboot) |

Default guest login: `melon` / `melon` (fixed; scripts do not prompt)

At the Ubuntu login screen choose **Ubuntu on Xorg** (not Wayland-only) for XInput2 raw aim.

## After reboot — mount shared folder (no build)

Inside Ubuntu terminal:

```bash
bash ~/mount-mp.sh
```

From Mac (Terminal.app, VM logged in):

```bash
/Users/admin/git/MelonPrimeDS/tools/linux-vm/05-mount-share.command
```

`~/mount-mp.sh` is installed by step **02** (guest finish). If missing, run step 05 once from Mac or mount manually:

```bash
sudo modprobe vboxsf
sudo mkdir -p /mnt/mp
sudo mount -t vboxsf MelonPrimeDS /mnt/mp
ls /mnt/mp
```

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
bash ~/mount-mp.sh
```

Build (optional):

```bash
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
  05-mount-share.{command,sh}       → 05-mount-share-from-host.sh
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

CI Ubuntu workflow uses the same dependency set; see [overview.md](overview.md) Linux platform notes for aim/input behavior.

## Linux runtime notes

- Aim path: `MelonPrimeRawInputLinuxFilter` (XInput2 `XI_RawMotion` on X11/xcb).
- At the Ubuntu login screen, click the gear and choose **Ubuntu on Xorg**. Check inside the guest:
  ```bash
  echo "$XDG_SESSION_TYPE"
  ```
  Expected for raw aim testing: `x11`.
- Launch from a terminal so backend logs are visible:
  ```bash
  cd /mnt/mp/build-linux
  ./melonPrimeDS 2>&1 | tee /tmp/melonprime-linux.log
  ```
- Good XInput2 startup log:
  ```text
  [MelonPrime] linux input: XInput2 RawMotion active
  ```
- That log means XInput2 selection succeeded, but Linux aim currently uses Qt mouse-move events
  accumulated by `ScreenPanel` as the source of truth. RawMotion is drained/logged for diagnostics only because
  `XWarpPointer` can generate RawMotion on some X11/VM stacks.
- On X11 the recenter must go through `MelonPrime::LinuxWarpCursorGlobal` (`XWarpPointer`), not
  `QCursor::setPos`.
- Escape should call `ScreenPanel::unfocus()` and `unclip()` on Linux, restoring the arrow cursor.
- Wayland falls back to QCursor center-delta and may be compositor-limited. Treat Wayland aim as
  best-effort; use Xorg for reliable testing.
- Japanese UI: `IsJapaneseSystemLocale()` + `LANG=ja_JP.UTF-8`; see locale fixes in `MelonPrimeLocalization.cpp`

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `VBoxManage: Failed to create VirtualBox object` | Open VirtualBox.app from Finder; use Terminal.app |
| VM state `poweroff` not starting | Fixed in scripts — re-run step 01 or 02 |
| Shared folder gone after reboot | `bash ~/mount-mp.sh` in guest, or Mac: `05-mount-share.command` |
| Shared folder icon won't open | Step 03 or 05, then `guest/guest-mount-share.sh` in guest |
| `guestcontrol` waits forever | Log into Ubuntu **desktop** first |
| CMake embed build info error | Use `guest/guest-build-only.sh` (clears stale cache, passes `-D` flags) |
| `Protocol error` on `/mnt/mp` | Share is already mounted; build can still proceed |
| Aim does not move | **Turn VirtualBox mouse integration OFF** (Mac host key = Left ⌘ → **Left ⌘ + I**, then click into the VM to capture). See the section below — verified 2026-07-03 |
| Aim spins or drifts | Suspect failed recenter in fallback mode; on X11 all recenter paths must use `LinuxWarpCursorGlobal` / `XWarpPointer` |
| Cursor stays hidden after Escape | `ScreenPanel::unfocus()` must call `unclip()` on Linux, not only on Windows |
| RawMotion never appears in VM | Run with `MELONPRIME_INPUT_DEBUG=1` and read the `[MelonPrime] linux input:` lines; use **Ubuntu on Xorg** and test after clicking the game panel so Qt focus is active |

## Mouse integration and aim testing (verified 2026-07-03)

With VirtualBox **mouse integration ON**, the guest pointer is an absolute tablet device
(`MELONPRIME_INPUT_DEBUG=1` shows `raw source N axis modes: X=abs Y=abs`). FPS mouse-look
cannot work in that mode: the guest only receives events while the **host** cursor is over the
VM window, and the guest cannot recenter the host cursor — the (hidden) pointer leaves the
window or pegs at a screen edge within a second and input stops. The runtime handles the abs
device as well as possible (pixel scaling from the XI axis range, ±300px teleport guard, warp
re-seeding), but sustained turning is structurally impossible.

For aim testing, disable integration so VBox exposes a **relative** mouse:

1. Focus the VM window and press **Host + I** (Mac host: **Left ⌘ + I**), then click into the
   VM to capture the mouse (release with the Host key). Permanent alternative:
   `VBoxManage modifyvm "melonprime-ubuntu" --mouse ps2`.
2. Verify with `MELONPRIME_INPUT_DEBUG=1 ./melonPrimeDS 2>&1 | grep MelonPrime`:
   a new `raw source N axis modes: X=rel Y=rel` line appears, `raw 1s:` lines keep flowing
   while moving, and `linux aim: src=raw sum60=(...)` shows nonzero consumption in-game.

Real Linux hardware mice are relative devices from the start; this constraint is VM-only.

## Alternative: git clone inside guest

If shared folders never work:

```bash
git clone https://github.com/ag-advania/melonPrimeDS.git ~/MelonPrimeDS
bash ~/MelonPrimeDS/tools/linux-vm/guest/guest-setup-and-build.sh ~/MelonPrimeDS
```

This does not see uncommitted Mac-side changes.
