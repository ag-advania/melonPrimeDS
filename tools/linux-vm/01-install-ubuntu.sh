#!/usr/bin/env bash
# Fully unattended Ubuntu 22.04 Desktop install in VirtualBox (MelonPrimeDS Linux VM).
#
# Run from Terminal.app (not Cursor) — VirtualBox needs the macOS GUI session:
#   ./tools/linux-vm/01-install-ubuntu.sh
#
# Optional:
#   RECREATE=1  ... delete existing VM and start fresh
#   ISO=...     ... override installer ISO path
set -euo pipefail

VM_NAME="${VM_NAME:-MelonPrimeDS-Ubuntu2204}"
VM_RAM_MB="${VM_RAM_MB:-4096}"
VM_CPUS="${VM_CPUS:-2}"
VM_DISK_GB="${VM_DISK_GB:-40}"
HOST_REPO="${HOST_REPO:-/Users/admin/git/MelonPrimeDS}"
SHARE_NAME="${SHARE_NAME:-MelonPrimeDS}"
ISO_PATH="${ISO:-${1:-$HOME/Downloads/ubuntu-22.04.5-desktop-amd64.iso}}"

GUEST_USER="${GUEST_USER:-melon}"
GUEST_PASS="${GUEST_PASS:-melon}"
GUEST_FULL_NAME="${GUEST_FULL_NAME:-Melon Tester}"
GUEST_HOST="${GUEST_HOST:-melonprime-ubuntu.local}"
LOCALE="${LOCALE:-ja_JP}"
COUNTRY="${COUNTRY:-JP}"
TIME_ZONE="${TIME_ZONE:-Asia/Tokyo}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VM_DIR="${HOME}/VirtualBox VMs/${VM_NAME}"
DISK_PATH="${VM_DIR}/${VM_NAME}.vdi"
LOG="${HOME}/melonprime-vbox-install.log"

log() { printf '[%s] %s\n' "$(date '+%H:%M:%S')" "$*" | tee -a "$LOG"; }

require_vbox() {
  if ! command -v VBoxManage >/dev/null 2>&1; then
    echo "VBoxManage not found. Install VirtualBox from https://www.virtualbox.org/" >&2
    exit 1
  fi
  if ! VBoxManage list vms >/dev/null 2>&1; then
    log "Starting VirtualBox..."
    open -a VirtualBox || open /Applications/VirtualBox.app
    for _ in $(seq 1 30); do
      if VBoxManage list vms >/dev/null 2>&1; then
        return 0
      fi
      sleep 2
    done
    echo "VirtualBox service did not start. Open VirtualBox.app manually, then re-run." >&2
    exit 1
  fi
}

vm_exists() {
  VBoxManage list vms 2>/dev/null | grep -F "\"${VM_NAME}\"" >/dev/null 2>&1
}

vm_state() {
  VBoxManage showvminfo "$VM_NAME" --machinereadable 2>/dev/null \
    | grep '^VMState=' | cut -d= -f2 | tr -d '"'
}

power_off_vm() {
  local state
  state="$(vm_state || echo absent)"
  case "$state" in
    running)
      log "Powering off ${VM_NAME}..."
      VBoxManage controlvm "$VM_NAME" poweroff || true
      sleep 3
      ;;
    paused)
      VBoxManage controlvm "$VM_NAME" resume || true
      VBoxManage controlvm "$VM_NAME" poweroff || true
      sleep 3
      ;;
  esac
}

delete_vm() {
  power_off_vm
  if vm_exists; then
    log "Removing existing VM: ${VM_NAME}"
    VBoxManage unregistervm "$VM_NAME" --delete || true
  fi
}

create_vm() {
  log "Creating VM: ${VM_NAME} (${VM_RAM_MB}MB RAM, ${VM_CPUS} CPUs, ${VM_DISK_GB}GB disk)"
  mkdir -p "$VM_DIR"
  VBoxManage createvm --name "$VM_NAME" --ostype Ubuntu_64 --register
  VBoxManage modifyvm "$VM_NAME" \
    --memory "$VM_RAM_MB" \
    --cpus "$VM_CPUS" \
    --vram 128 \
    --graphicscontroller vmsvga \
    --nic1 nat \
    --audio none \
    --usb off \
    --clipboard-mode bidirectional \
    --draganddrop bidirectional \
    --ioapic on \
    --pae on \
    --longmode on \
    --largepages on \
    --rtcuseutc on

  VBoxManage createmedium disk --filename "$DISK_PATH" --size $((VM_DISK_GB * 1024)) --format VDI
  VBoxManage storagectl "$VM_NAME" --name SATA --add sata --controller IntelAhci --portcount 2 --bootable on
  VBoxManage storageattach "$VM_NAME" --storagectl SATA --port 0 --device 0 --type hdd --medium "$DISK_PATH"
  VBoxManage storageattach "$VM_NAME" --storagectl SATA --port 1 --device 0 --type dvddrive --medium emptydrive
}

vm_disk_actual_bytes() {
  if [[ ! -f "$DISK_PATH" ]]; then
    echo 0
    return
  fi
  VBoxManage showmediuminfo "$DISK_PATH" --machinereadable 2>/dev/null \
    | grep '^UUID=' >/dev/null || { echo 0; return; }
  # "Actual size: on disk" line, e.g. "Actual size:            12.34 GiB"
  local line
  line="$(VBoxManage showmediuminfo "$DISK_PATH" 2>/dev/null | grep -i 'Actual size' | head -1 || true)"
  if [[ "$line" =~ ([0-9]+(\.[0-9]+)?)[[:space:]]*GiB ]]; then
    awk -v g="${BASH_REMATCH[1]}" 'BEGIN { printf "%.0f\n", g * 1024 * 1024 * 1024 }'
  elif [[ "$line" =~ ([0-9]+)[[:space:]]*MiB ]]; then
    awk -v m="${BASH_REMATCH[1]}" 'BEGIN { printf "%.0f\n", m * 1024 * 1024 }'
  else
    echo 0
  fi
}

looks_installed() {
  local bytes
  bytes="$(vm_disk_actual_bytes)"
  # Fresh empty VDI is ~2 MiB; installed Ubuntu desktop is usually several GiB.
  [[ "$bytes" -gt $((4 * 1024 * 1024 * 1024)) ]]
}

finish_existing_install() {
  log "VM disk looks populated — skipping unattended install."
  configure_shared_folder
  power_off_vm
  attach_guest_additions_iso
  log "Starting VM..."
  VBoxManage startvm "$VM_NAME" --type gui
  print_guest_steps
  exit 0
}

configure_shared_folder() {
  log "Shared folder: ${SHARE_NAME} -> ${HOST_REPO}"
  if VBoxManage showvminfo "$VM_NAME" --machinereadable 2>/dev/null \
    | grep -F "SharedFolderNameMachineMapping" | grep -F "${SHARE_NAME}" >/dev/null 2>&1; then
    VBoxManage sharedfolder remove "$VM_NAME" --name "$SHARE_NAME" || true
  fi
  VBoxManage sharedfolder add "$VM_NAME" \
    --name "$SHARE_NAME" \
    --hostpath "$HOST_REPO" \
    --automount
}

wait_for_install() {
  log "Waiting for unattended install to finish (VM will reboot; ~15–40 min)..."
  local saw_running=0
  local saw_powered_off=0
  local stable_running_polls=0
  local start_ts
  start_ts="$(date +%s)"
  local max_wait_sec=$((60 * 60))
  local min_elapsed_for_running_done=$((20 * 60))

  while true; do
    local state now elapsed
    state="$(vm_state || echo absent)"
    now="$(date +%s)"
    elapsed=$((now - start_ts))

    case "$state" in
      running)
        saw_running=1
        if [[ "$saw_powered_off" -eq 1 ]]; then
          stable_running_polls=$((stable_running_polls + 1))
          if [[ "$stable_running_polls" -ge 8 ]]; then
            log "Install complete (VM at desktop/login after reboot)."
            return 0
          fi
        elif [[ "$elapsed" -ge "$min_elapsed_for_running_done" ]]; then
          log "Install likely complete (VM still running after ${min_elapsed_for_running_done}s)."
          return 0
        fi
        ;;
      poweredoff|poweroff)
        stable_running_polls=0
        if [[ "$saw_running" -eq 1 ]]; then
          saw_powered_off=1
          log "VM powered off (install reboot step)..."
          if [[ "$elapsed" -gt 120 ]]; then
            log "Install finished (VM powered off after running)."
            return 0
          fi
        fi
        ;;
    esac

    if [[ "$elapsed" -gt "$max_wait_sec" ]]; then
      log "Timed out after ${max_wait_sec}s (state=${state}). Continuing anyway."
      return 0
    fi

    sleep 15
  done
}

attach_guest_additions_iso() {
  log "Attaching Guest Additions ISO..."
  local state
  state="$(vm_state || echo absent)"
  if [[ "$state" == "running" ]]; then
    log "VM is running — eject installer ISO from guest if the CD icon is still visible."
  fi
  VBoxManage storageattach "$VM_NAME" \
    --storagectl SATA --port 1 --device 0 \
    --type dvddrive --medium emptydrive
  VBoxManage storageattach "$VM_NAME" \
    --storagectl SATA --port 1 --device 0 \
    --type dvddrive \
    --medium "/Applications/VirtualBox.app/Contents/MacOS/VBoxGuestAdditions.iso"
}

finish_after_install() {
  attach_guest_additions_iso
  local state
  state="$(vm_state || echo absent)"
  if [[ "$state" != "running" ]]; then
    log "Starting VM with Guest Additions ISO mounted..."
    VBoxManage startvm "$VM_NAME" --type gui
  else
    log "VM already running — Guest Additions ISO attached in the virtual DVD drive."
  fi
  print_guest_steps
}

print_guest_steps() {
  cat <<EOF

================================================================================
Ubuntu unattended install triggered.

  VM name : ${VM_NAME}
  User    : ${GUEST_USER}
  Password: ${GUEST_PASS}
  Locale  : ${LOCALE} (${TIME_ZONE})

Log: ${LOG}

When the VM window shows the desktop (after auto-reboot):

  1. Log in with the credentials above.
  2. At login, choose **Ubuntu on Xorg** (gear icon) for aim/XInput2 testing.
  3. Guest Additions CD should already be inserted. If not, run on the Mac:
       ${SCRIPT_DIR}/lib/vbox-mount-guest-additions.sh

  In the Ubuntu terminal (guest):

    sudo apt update
    sudo apt install -y build-essential dkms linux-headers-\$(uname -r)
    sudo mount /dev/cdrom /mnt
    sudo /mnt/VBoxLinuxAdditions.run
    sudo reboot

  After reboot on Mac, run: ${SCRIPT_DIR}/02-guest-finish.command
  Then: ${SCRIPT_DIR}/04-guest-build.command

  Or inside Ubuntu:

    bash tools/linux-vm/guest/guest-build-only.sh
    ./build-linux/melonPrimeDS

Japanese UI smoke test:
  export LANG=ja_JP.UTF-8
  ./melonPrimeDS

================================================================================
EOF
}

main() {
  : >"$LOG"
  log "MelonPrimeDS VirtualBox Ubuntu install"

  if [[ ! -f "$ISO_PATH" ]]; then
    echo "ISO not found: $ISO_PATH" >&2
    echo "Download Ubuntu 22.04.5 Desktop amd64 to ~/Downloads/ or pass a path." >&2
    exit 1
  fi
  if [[ ! -d "$HOST_REPO" ]]; then
    echo "Repo not found: $HOST_REPO" >&2
    exit 1
  fi

  require_vbox

  if [[ "${RECREATE:-0}" == "1" ]]; then
    delete_vm
  fi

  if vm_exists; then
    power_off_vm
    if [[ "${RECREATE:-0}" != "1" ]] && looks_installed; then
      finish_existing_install
    fi
    log "Using existing VM shell: ${VM_NAME}"
  else
    create_vm
  fi

  if ! vm_exists; then
    echo "Failed to create VM: ${VM_NAME}" >&2
    exit 1
  fi

  configure_shared_folder

  log "Starting unattended install from: $ISO_PATH"
  VBoxManage unattended install "$VM_NAME" \
    --iso="$ISO_PATH" \
    --user="$GUEST_USER" \
    --password="$GUEST_PASS" \
    --full-user-name="$GUEST_FULL_NAME" \
    --hostname="$GUEST_HOST" \
    --locale="$LOCALE" \
    --country="$COUNTRY" \
    --time-zone="$TIME_ZONE" \
    --install-additions \
    --start-vm=gui

  wait_for_install
  finish_after_install
}

main "$@"
