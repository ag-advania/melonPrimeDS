#!/usr/bin/env bash
# Desktop shortcut + Files sidebar bookmark for /mnt/mp (MelonPrimeDS share).
set -euo pipefail

MNT="${1:-/mnt/mp}"
NAME="${2:-MelonPrimeDS}"
DESKTOP_NAME="${NAME} (Mac shared)"

if [[ ! -f "${MNT}/CMakeLists.txt" ]]; then
  echo "Repo not mounted at ${MNT} — run: bash ~/mount-mp.sh" >&2
  exit 1
fi

# vboxsf: readable when user is in vboxsf group (re-login may be needed).
if ! groups | grep -qw vboxsf; then
  sudo usermod -aG vboxsf "$USER"
  echo "==> Added ${USER} to vboxsf group (log out/in if Files still empty)."
fi

DESKTOP_DIR="${XDG_DESKTOP_DIR:-${HOME}/Desktop}"
mkdir -p "${DESKTOP_DIR}" "${HOME}/.local/share/applications"

APP_ID="melonprime-mp-folder"
DESKTOP_FILE="${HOME}/.local/share/applications/${APP_ID}.desktop"
DESKTOP_LINK="${DESKTOP_DIR}/${DESKTOP_NAME}.desktop"

cat > "${DESKTOP_FILE}" <<EOF
[Desktop Entry]
Version=1.0
Type=Application
Name=${DESKTOP_NAME}
Comment=VirtualBox shared folder from Mac (${MNT})
Exec=nautilus ${MNT}
Icon=folder
Terminal=false
Categories=Development;Utility;
StartupNotify=true
EOF

cp "${DESKTOP_FILE}" "${DESKTOP_LINK}"
chmod +x "${DESKTOP_FILE}" "${DESKTOP_LINK}"

if command -v gio >/dev/null 2>&1; then
  gio set "${DESKTOP_LINK}" metadata::trusted true 2>/dev/null || true
fi

# Files (Nautilus) sidebar bookmark
mkdir -p "${HOME}/.config/gtk-3.0"
BOOKMARKS="${HOME}/.config/gtk-3.0/bookmarks"
LINE="file://${MNT} ${NAME}"
if [[ -f "${BOOKMARKS}" ]] && grep -Fq "${MNT}" "${BOOKMARKS}" 2>/dev/null; then
  :
else
  echo "${LINE}" >> "${BOOKMARKS}"
fi

# Symlink fallback (some desktops show this more reliably than .desktop)
ln -sfn "${MNT}" "${DESKTOP_DIR}/${NAME}-shared"

echo "==> Desktop: ${DESKTOP_LINK}"
echo "==> Symlink: ${DESKTOP_DIR}/${NAME}-shared"
echo "==> Files sidebar bookmark: ${NAME}"
echo
echo "Open with: nautilus ${MNT}"
echo "If folder looks empty, log out and log in once (vboxsf group)."
