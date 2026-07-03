#!/usr/bin/env bash
# Mount MelonPrimeDS shared folder inside Ubuntu (no build).
# After reboot: bash ~/mount-mp.sh
set -euo pipefail

SHARE="${1:-MelonPrimeDS}"
MNT="${2:-/mnt/mp}"

SCRIPT_SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")"

install_local_copy() {
  local dest="${HOME}/mount-mp.sh"
  if [[ -f "$SCRIPT_SRC" && "$SCRIPT_SRC" != "$dest" ]]; then
    cp "$SCRIPT_SRC" "$dest"
    chmod +x "$dest"
    echo "==> Installed reboot helper: ${dest}"
  fi
}

diagnose_share() {
  echo "==> diagnose..."
  echo "    kernel: $(uname -r)"
  if lsmod | grep -q vboxsf; then
    echo "    vboxsf: loaded"
  else
    echo "    vboxsf: NOT loaded"
  fi
  if groups | grep -qw vboxsf; then
    echo "    vboxsf group: yes (current shell)"
  else
    echo "    vboxsf group: no in this shell — sudo mount still OK; log out/in for /media/sf_*"
  fi
  if command -v VBoxService >/dev/null 2>&1; then
    echo "    VBoxService: present"
  else
    echo "    VBoxService: missing — Guest Additions broken?"
  fi
  for p in "$MNT" "/media/sf_${SHARE}" "/media/${USER}/${SHARE}"; do
    if [[ -d "$p" ]]; then
      if mountpoint -q "$p" 2>/dev/null; then
        echo "    ${p}: mountpoint"
      elif [[ -L "$p" ]]; then
        echo "    ${p}: symlink -> $(readlink "$p")"
      else
        echo "    ${p}: exists (not mounted)"
      fi
    fi
  done
}

find_automount_repo() {
  local p
  for p in "/media/sf_${SHARE}" "/media/${USER}/${SHARE}"; do
    if [[ -f "${p}/CMakeLists.txt" ]]; then
      printf '%s' "$p"
      return 0
    fi
  done
  return 1
}

ensure_vboxsf() {
  echo "==> vboxsf module..."
  if lsmod | grep -q vboxsf; then
    return 0
  fi
  if command -v VBoxService >/dev/null 2>&1; then
    sudo systemctl start vboxadd-service 2>/dev/null || true
  fi
  sudo modprobe vboxsf || {
    diagnose_share
    echo
    echo "vboxsf failed — Guest Additions may be missing or kernel mismatch." >&2
    echo "On Mac: run 02-guest-finish.command (or reinstall Guest Additions ISO)" >&2
    exit 1
  }
}

link_or_bind_automount() {
  local auto="$1"
  echo "==> automount found: ${auto}"
  sudo mkdir -p "$(dirname "$MNT")"
  if mountpoint -q "$MNT" 2>/dev/null; then
    if [[ -f "${MNT}/CMakeLists.txt" ]]; then
      return 0
    fi
    sudo umount "$MNT" || sudo umount -l "$MNT" || true
  fi
  if [[ -e "$MNT" && ! -L "$MNT" ]] && ! mountpoint -q "$MNT" 2>/dev/null; then
    sudo rmdir "$MNT" 2>/dev/null || sudo rm -rf "$MNT"
  fi
  if [[ ! -e "$MNT" ]]; then
    sudo ln -sfn "$auto" "$MNT"
  elif [[ -L "$MNT" ]]; then
    sudo ln -sfn "$auto" "$MNT"
  elif ! mountpoint -q "$MNT" 2>/dev/null; then
    sudo mount --bind "$auto" "$MNT"
  fi
}

manual_mount() {
  echo "==> manual mount ${SHARE} -> ${MNT}..."
  sudo usermod -aG vboxsf "$USER"
  sudo mkdir -p "$MNT"
  if mountpoint -q "$MNT" 2>/dev/null; then
    if [[ -f "${MNT}/CMakeLists.txt" ]]; then
      return 0
    fi
    sudo umount "$MNT" || sudo umount -l "$MNT" || true
  fi
  if [[ -e "$MNT" && ! -L "$MNT" ]] && ! mountpoint -q "$MNT" 2>/dev/null; then
    sudo rmdir "$MNT" 2>/dev/null || true
    sudo mkdir -p "$MNT"
  fi

  local attempt err
  for attempt in 1 2 3 4 5; do
    if err="$(sudo mount -t vboxsf "$SHARE" "$MNT" 2>&1)"; then
      return 0
    fi
    echo "    mount attempt ${attempt}/5 failed: ${err}"
    if [[ "$err" == *"Protocol error"* && -f "${MNT}/CMakeLists.txt" ]]; then
      echo "    (Protocol error but repo visible — treating as OK)"
      return 0
    fi
    if [[ "$err" == *"No such device"* ]]; then
      sudo modprobe vboxsf || true
    fi
    sleep 2
  done
  return 1
}

verify_repo() {
  if [[ -f "${MNT}/CMakeLists.txt" ]]; then
    return 0
  fi
  diagnose_share
  echo
  echo "Mount point ${MNT} has no CMakeLists.txt — host share may be missing." >&2
  echo "On Mac (Terminal.app): tools/linux-vm/05-mount-share.command" >&2
  echo "Or: tools/linux-vm/03-fix-shared-folder.command" >&2
  exit 1
}

main() {
  ensure_vboxsf

  local auto
  if auto="$(find_automount_repo)"; then
    link_or_bind_automount "$auto"
  elif ! manual_mount; then
    diagnose_share
    echo
    echo "Could not mount ${SHARE}." >&2
    echo "Try on Mac: 05-mount-share.command (VM logged in)" >&2
    exit 1
  fi

  verify_repo
  install_local_copy

  if [[ -f "${MNT}/tools/linux-vm/guest/guest-install-desktop-shortcut.sh" ]]; then
    bash "${MNT}/tools/linux-vm/guest/guest-install-desktop-shortcut.sh" "${MNT}"
  fi

  echo "==> contents:"
  ls "$MNT" | head -20

  cat <<EOF

OK: repo is at ${MNT}

After reboot:

  bash ~/mount-mp.sh

Mac (VM logged in):

  tools/linux-vm/05-mount-share.command

Install boot-time mount (once, needs sudo):

  bash ${MNT}/tools/linux-vm/guest/guest-install-mount-on-boot.sh

EOF
}

main "$@"
