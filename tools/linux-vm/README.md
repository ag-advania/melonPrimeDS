# Linux VM tools (VirtualBox + Ubuntu 22.04)

Run from **Terminal.app** on the Mac host (not Cursor's integrated terminal).
VirtualBox needs the macOS GUI session.

## Execution order

| Step | Mac (double-click or Terminal) | Purpose |
|------|----------------------------------|---------|
| **01** | `01-install-ubuntu.command` | Create VM, unattended Ubuntu 22.04 install |
| **02** | `02-guest-finish.command` | Guest Additions + shared-folder prep |
| **03** | `03-fix-shared-folder.command` | Optional — if shared folder won't open |
| **04** | `04-guest-build.command` | Build `melonPrimeDS` inside the guest |

Default VM: `MelonPrimeDS-Ubuntu2204`  
Default guest user: `melon` / `melon` (fixed in scripts)  
Log in with **Ubuntu on Xorg** for XInput2 aim testing.

## Inside Ubuntu (manual fallback)

```bash
bash /mnt/mp/tools/linux-vm/guest/guest-build-only.sh
/mnt/mp/build-linux/melonPrimeDS
```

## Layout

```
tools/linux-vm/
  01-install-ubuntu.{command,sh}
  02-guest-finish.{command,sh}
  03-fix-shared-folder.{command,sh}
  04-guest-build.{command,sh}
  lib/          # host helpers (vbox-guest-common.sh, …)
  guest/        # scripts executed inside Ubuntu
```

Full documentation: [.claude/rules/linux-vm-build.md](../../.claude/rules/linux-vm-build.md)
