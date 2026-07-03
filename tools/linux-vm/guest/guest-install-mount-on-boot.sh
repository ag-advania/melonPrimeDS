#!/usr/bin/env bash
# Install systemd unit to mount /mnt/mp after Guest Additions start (once per VM).
set -euo pipefail

SHARE="${1:-MelonPrimeDS}"
MNT="${2:-/mnt/mp}"
INSTALL_SRC="${3:-}"

if [[ -z "$INSTALL_SRC" ]]; then
  for p in "/mnt/mp/tools/linux-vm/guest/guest-mount-share.sh" \
           "/media/sf_${SHARE}/tools/linux-vm/guest/guest-mount-share.sh" \
           "${HOME}/MelonPrimeDS/tools/linux-vm/guest/guest-mount-share.sh"; do
    if [[ -f "$p" ]]; then
      INSTALL_SRC="$p"
      break
    fi
  done
fi

if [[ -z "$INSTALL_SRC" || ! -f "$INSTALL_SRC" ]]; then
  echo "guest-mount-share.sh not found — mount share manually first." >&2
  exit 1
fi

HELPER="/usr/local/sbin/melon-mount-mp.sh"
UNIT="/etc/systemd/system/melon-mount-mp.service"

echo "==> Installing ${HELPER}..."
sudo install -m 755 "$INSTALL_SRC" "$HELPER"

echo "==> Installing ${UNIT}..."
sudo tee "$UNIT" >/dev/null <<EOF
[Unit]
Description=Mount MelonPrimeDS VirtualBox share at ${MNT}
After=vboxadd.service vboxadd-service.service
Wants=vboxadd-service.service

[Service]
Type=oneshot
RemainAfterExit=yes
Environment=SHARE_NAME=${SHARE}
Environment=MOUNT_POINT=${MNT}
ExecStart=${HELPER}

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable melon-mount-mp.service
sudo systemctl start melon-mount-mp.service || true

echo
echo "==> status:"
systemctl is-enabled melon-mount-mp.service
systemctl is-active melon-mount-mp.service || true
if [[ -f "${MNT}/CMakeLists.txt" ]]; then
  echo "OK: ${MNT} is ready"
else
  echo "Service installed but mount not verified — run: bash ~/mount-mp.sh"
fi
