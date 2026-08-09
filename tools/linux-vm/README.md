# Linux VM tools (VirtualBox + Ubuntu 22.04)

Run from **Terminal.app** on the Mac host (not Cursor's integrated terminal).
VirtualBox needs the macOS GUI session.

## Execution order

| Step | Mac (double-click or Terminal) | Purpose |
|------|----------------------------------|---------|
| **01** | `01-install-ubuntu.command` | Create VM, unattended Ubuntu 22.04 install |
| **02** | `02-guest-finish.command` | Guest Additions + shared-folder prep |
| **03** | `03-fix-shared-folder.command` | Optional — if shared folder won't open |
| **04** | `04-guest-build.command` | Full cmake configure + Vulkan-enabled build |
| **04b** | `04-guest-build-existing.command` | **Incremental only** — skip configure, requires a Vulkan-enabled tree |
| **05** | `05-mount-share.command` | **Shared folder only** — no build (use after reboot) |

Default VM: `MelonPrimeDS-Ubuntu2204`  
Default guest user: `melon` / `melon` (fixed in scripts)  
Log in with **Ubuntu on Xorg** for XInput2 aim testing.

## Inside Ubuntu (manual fallback)

**After reboot** (when `/mnt/mp` is gone):

```bash
bash ~/mount-mp.sh
```

If `~/mount-mp.sh` does not exist yet, run once from Mac: `05-mount-share.command`, or:

```bash
sudo modprobe vboxsf && sudo mkdir -p /mnt/mp && sudo mount -t vboxsf MelonPrimeDS /mnt/mp
```

Build (optional):

```bash
bash /mnt/mp/tools/linux-vm/guest/guest-build-only.sh
/mnt/mp/build-linux/melonPrimeDS
```

The full and build-only commands configure Vulkan explicitly
(`MELONPRIME_ENABLE_VULKAN=ON`) and fetch the same pinned Khronos
`Vulkan-Headers` revision used by Ubuntu CI. Runtime packages
(`libvulkan-dev`, `mesa-vulkan-drivers`, `vulkan-validationlayers`) are
installed by the full setup command. Native Wayland pointer lock is left in
CMake's `AUTO` mode so missing `wayland-protocols` does not block an Xorg VM
Vulkan build.

**Incremental rebuild** (skip cmake configure — like `build-mingw-existing.bat`):

```bash
bash /mnt/mp/tools/linux-vm/guest/guest-build-existing.sh
# or: bash /mnt/mp/tools/linux-vm/guest/guest-build-existing.sh --jobs 2 --tail 60
```

## Layout

```
tools/linux-vm/
  01-install-ubuntu.{command,sh}
  02-guest-finish.{command,sh}
  03-fix-shared-folder.{command,sh}
  04-guest-build.{command,sh}
  05-mount-share.{command,sh}         → mount only, no build
  lib/          # host helpers (vbox-guest-common.sh, …)
  guest/        # scripts executed inside Ubuntu
```

Full documentation: [docs/development/build/linux-vm.md](../../docs/development/build/linux-vm.md)
