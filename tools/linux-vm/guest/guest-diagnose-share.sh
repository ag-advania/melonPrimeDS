#!/usr/bin/env bash
# Print VirtualBox shared-folder diagnostics inside Ubuntu guest.
set -euo pipefail

SHARE="${1:-MelonPrimeDS}"

echo "==> MelonPrimeDS shared folder diagnose"
echo "    user: ${USER}"
echo "    groups: $(groups)"
echo "    kernel: $(uname -r)"
echo

echo "==> vboxsf / Guest Additions"
if lsmod | grep vboxsf; then
  echo "    vboxsf: loaded"
else
  echo "    vboxsf: NOT loaded"
fi
if [[ -x /sbin/mount.vboxsf || -x /usr/sbin/mount.vboxsf ]]; then
  echo "    mount.vboxsf: present"
else
  echo "    mount.vboxsf: MISSING"
fi
if command -v VBoxService >/dev/null 2>&1; then
  echo "    VBoxService: $(command -v VBoxService)"
  systemctl is-active vboxadd-service 2>/dev/null || echo "    vboxadd-service: inactive"
else
  echo "    VBoxService: not found"
fi
echo

echo "==> candidate paths"
for p in "/mnt/mp" "/media/sf_${SHARE}" "/media/${USER}/${SHARE}" "${HOME}/mount-mp.sh"; do
  echo -n "    ${p}: "
  if [[ ! -e "$p" ]]; then
    echo "missing"
  elif [[ -L "$p" ]]; then
    echo "symlink -> $(readlink "$p")"
  elif mountpoint -q "$p" 2>/dev/null; then
    echo "mounted ($(ls "$p" 2>/dev/null | wc -l | tr -d ' ') entries)"
  elif [[ -f "$p" ]]; then
    echo "file"
  elif [[ -d "$p" ]]; then
    echo "dir (not mountpoint)"
  else
    echo "?"
  fi
done
echo

echo "==> quick fix (copy/paste)"
cat <<EOF
sudo systemctl start vboxadd-service 2>/dev/null; sudo modprobe vboxsf
bash ~/mount-mp.sh
# or if mount-mp missing:
sudo mkdir -p /mnt/mp && sudo mount -t vboxsf ${SHARE} /mnt/mp && ls /mnt/mp
EOF
