#!/usr/bin/env bash
# Shared helpers for Mac-host → VirtualBox guest automation.
VM_NAME="${VM_NAME:-MelonPrimeDS-Ubuntu2204}"
GUEST_USER="${GUEST_USER:-melon}"
GUEST_PASS="${GUEST_PASS:-melon}"
SHARE_NAME="${SHARE_NAME:-MelonPrimeDS}"
REPO_GUEST="/media/${GUEST_USER}/${SHARE_NAME}"
REPO_GUEST_ALT="/media/sf_${SHARE_NAME}"
REPO_GUEST_MOUNT="/mnt/mp"
HOST_REPO="${HOST_REPO:-/Users/admin/git/MelonPrimeDS}"
LOGIN_WAIT_SEC="${LOGIN_WAIT_SEC:-600}"

require_vbox() {
  if ! command -v VBoxManage >/dev/null 2>&1 || ! VBoxManage list vms >/dev/null 2>&1; then
    echo "VirtualBox is not running. Open /Applications/VirtualBox.app first." >&2
    exit 1
  fi
}

vm_state() {
  VBoxManage showvminfo "$VM_NAME" --machinereadable \
    | grep '^VMState=' | cut -d= -f2 | tr -d '"'
}

ensure_vm_running() {
  local state
  state="$(vm_state)"
  case "$state" in
    running) echo "==> VM already running." ;;
    poweredoff|poweroff|saved|aborted)
      echo "==> Starting VM (${state})..."
      VBoxManage startvm "$VM_NAME" --type gui
      ;;
    *)
      echo "VM is in state '${state}'. Fix it in VirtualBox first." >&2
      exit 1
      ;;
  esac
  local i
  for ((i = 1; i <= 60; i++)); do
    [[ "$(vm_state)" == "running" ]] && return 0
    sleep 2
  done
  echo "VM did not reach running state." >&2
  exit 1
}

guest_run() {
  local cmd="$1"
  local timeout_ms="${2:-7200000}"
  VBoxManage guestcontrol "$VM_NAME" run \
    --exe /bin/bash \
    --username "$GUEST_USER" \
    --password "$GUEST_PASS" \
    --wait-stdout --wait-stderr \
    --timeout "$timeout_ms" \
    -- -lc "$cmd"
}

guest_run_quick() {
  guest_run "$1" "${2:-20000}"
}

guest_property() {
  local key="$1"
  VBoxManage guestproperty get "$VM_NAME" "$key" 2>/dev/null \
    | awk -F': ' 'NF > 1 { print $2 }' | tr -d '\r\n'
}

guest_logged_in_users() {
  guest_property "/VirtualBox/GuestInfo/OS/LoggedInUsersList"
}

guest_additions_version() {
  guest_property "/VirtualBox/GuestInfo/GuestAdditionsVersion"
}

guest_auth_diagnose() {
  local ga users err
  ga="$(guest_additions_version)"
  users="$(guest_logged_in_users)"
  if [[ -z "$ga" || "$ga" == "No"*"value"* ]]; then
    echo "    Guest Additions: not reporting (re-run 02-guest-finish.command)"
  else
    echo "    Guest Additions: ${ga}"
  fi
  if [[ -n "$users" && "$users" != "No"*"value"* ]]; then
    echo "    Logged-in users (VBox): ${users}"
  else
    echo "    Logged-in users (VBox): none reported yet"
  fi
  err="$(guest_run_quick "/bin/true" 15000 2>&1)" || true
  if [[ -n "$err" && "$err" != *"successfully"* && "$err" != *"exit code 0"* ]]; then
    echo "    guestcontrol: ${err//$'\n'/ }" | head -c 400
    echo
  fi
}

test_guest_auth() {
  guest_run_quick "echo guestcontrol-ok" 20000 >/dev/null 2>&1
}

wait_for_guest_login() {
  local elapsed=0 interval=5
  local max_sec="${LOGIN_WAIT_SEC:-600}"
  echo
  echo "================================================================"
  echo "  VirtualBox ウィンドウをクリックして Ubuntu にログインしてください"
  echo "  ユーザー: ${GUEST_USER}  /  パスワード: ${GUEST_PASS}"
  echo "  歯車 → Ubuntu on Xorg  /  デスクトップ表示まで待つ"
  echo "================================================================"
  echo "==> guestcontrol を確認中 (タイムアウト 20s/回)..."
  guest_auth_diagnose
  while [[ "$elapsed" -lt "$max_sec" ]]; do
    if test_guest_auth; then
      echo "==> Guest session ready."
      return 0
    fi
    elapsed=$((elapsed + interval))
    echo "    Waiting for guestcontrol... (${elapsed}s / ${max_sec}s)"
    guest_auth_diagnose
    sleep "$interval"
  done
  echo
  echo "Timed out waiting for guestcontrol." >&2
  return 1
}

guest_additions_ok() {
  local ga
  ga="$(guest_additions_version)"
  [[ -n "$ga" && "$ga" != "No"*"value"* ]]
}

guest_user_logged_in() {
  local users
  users="$(guest_logged_in_users)"
  [[ -n "$users" && "$users" != "No"*"value"* && "$users" == *"${GUEST_USER}"* ]]
}

last_guestcontrol_error() {
  guest_run_quick "true" 15000 2>&1 || true
}

guest_additions_broken_while_logged_in() {
  guest_user_logged_in && ! guest_additions_ok
}

host_only_mount_fallback() {
  echo
  echo "==> Mac 側の共有フォルダ設定は更新済み。guestcontrol は使えませんでした。"
  echo "    Ubuntu 内ターミナル (Ctrl+Alt+T) で以下を実行してください:"
  print_manual_mount_instructions
  cat <<EOF

まずこれ (Guest Additions / vboxsf):

  sudo apt-get update
  sudo apt-get install -y virtualbox-guest-utils virtualbox-guest-dkms
  sudo systemctl restart vboxadd-service
  sudo modprobe vboxsf
  bash ~/mount-mp.sh

それでもダメなら ISO から入れ直し:

  # VirtualBox メニュー → Devices → Insert Guest Additions CD image...
  sudo mount /dev/cdrom /mnt
  sudo /mnt/VBoxLinuxAdditions.run
  sudo reboot

再起動後: bash ~/mount-mp.sh

パスワードを melon 以外に変えている場合、Mac 側 guestcontrol は使えません。
VM 内で直接マウントしてください。

EOF
}

explain_guest_additions_blocked() {
  echo
  echo "================================================================"
  echo "  ${GUEST_USER} はログイン済みですが Guest Additions が動いていません"
  echo "  (VBox が GA バージョンを報告しない → guestcontrol 不可)"
  echo "================================================================"
  echo "  デスクトップの「4.1 KB Volume」は automount 失敗のゴミです。"
  echo "  05 を繰り返すと増えます — 先に Mac で 03-fix-shared-folder.command"
  echo "================================================================"
  host_only_mount_fallback
}

print_manual_mount_instructions() {
  cat <<EOF

Ubuntu 内ターミナル (Ctrl+Alt+T) — 共有フォルダだけ（ビルドなし）:

  bash ~/mount-mp.sh

または手入力:

  sudo modprobe vboxsf
  sudo mkdir -p /mnt/mp
  sudo mount -t vboxsf ${SHARE_NAME} /mnt/mp
  ls /mnt/mp

Mac から自動マウント:

  ${HOST_REPO}/tools/linux-vm/05-mount-share.command

EOF
}

print_manual_build_instructions() {
  print_manual_mount_instructions
  cat <<EOF
ビルド:

  bash /mnt/mp/tools/linux-vm/guest/guest-build-only.sh

EOF
}

share_attached_on_host() {
  VBoxManage showvminfo "$VM_NAME" 2>/dev/null \
    | grep -F "Name: '${SHARE_NAME}'" -q
}

ensure_host_shared_folder() {
  if [[ ! -d "$HOST_REPO" ]]; then
    echo "Host repo not found: $HOST_REPO" >&2
    return 1
  fi

  if share_attached_on_host; then
    echo "==> Shared folder '${SHARE_NAME}' already attached (not re-adding)." >&2
    return 0
  fi

  local state
  state="$(vm_state)"
  if [[ "$state" == "running" ]]; then
    echo "==> Attaching shared folder (transient, manual mount only — no automount)..." >&2
    if ! VBoxManage sharedfolder add "$VM_NAME" \
      --name "$SHARE_NAME" \
      --hostpath "$HOST_REPO" \
      --transient 2>/dev/null; then
      echo "==> Could not attach transient share. Power off VM and run 03-fix-shared-folder.command" >&2
      return 1
    fi
    return 0
  fi

  echo "==> Configuring shared folder (manual mount at /mnt/mp — automount OFF)..." >&2
  VBoxManage sharedfolder remove "$VM_NAME" --name "$SHARE_NAME" 2>/dev/null || true
  VBoxManage sharedfolder add "$VM_NAME" \
    --name "$SHARE_NAME" \
    --hostpath "$HOST_REPO"
}

ensure_guest_share_mounted() {
  echo "==> Mounting ${SHARE_NAME} inside guest (${REPO_GUEST_MOUNT})..." >&2
  guest_sudo "
    set -e
    systemctl start vboxadd-service 2>/dev/null || true
    modprobe vboxsf
    usermod -aG vboxsf ${GUEST_USER}
    for p in '${REPO_GUEST_ALT}' '${REPO_GUEST}'; do
      if [[ -f \"\$p/CMakeLists.txt\" ]]; then
        mkdir -p '${REPO_GUEST_MOUNT}'
        if ! mountpoint -q '${REPO_GUEST_MOUNT}' 2>/dev/null; then
          ln -sfn \"\$p\" '${REPO_GUEST_MOUNT}' 2>/dev/null || mount --bind \"\$p\" '${REPO_GUEST_MOUNT}'
        fi
        test -f '${REPO_GUEST_MOUNT}/CMakeLists.txt'
        exit 0
      fi
    done
    mkdir -p ${REPO_GUEST_MOUNT}
    if mountpoint -q ${REPO_GUEST_MOUNT} 2>/dev/null; then
      umount ${REPO_GUEST_MOUNT} 2>/dev/null || umount -l ${REPO_GUEST_MOUNT} 2>/dev/null || true
    fi
    for i in 1 2 3 4 5; do
      if mount -t vboxsf ${SHARE_NAME} ${REPO_GUEST_MOUNT} 2>/dev/null; then
        break
      fi
      sleep 2
      modprobe vboxsf 2>/dev/null || true
    done
    test -f ${REPO_GUEST_MOUNT}/CMakeLists.txt
  "
}

guest_sudo() {
  local cmd="$1"
  local pw_b64
  pw_b64="$(printf '%s' "$GUEST_PASS" | base64 | tr -d '\n')"
  guest_run "
    set -e
    PW=\$(printf '%s' '${pw_b64}' | base64 -d)
    printf '%s\n' \"\$PW\" | sudo -S -p '' bash -lc $(printf '%q' "$cmd")
  "
}

resolve_repo_path() {
  guest_run "
    for p in '${REPO_GUEST_MOUNT}' '${REPO_GUEST_ALT}' '${REPO_GUEST}' '${HOME}/MelonPrimeDS'; do
      if [[ -f \"\$p/CMakeLists.txt\" ]]; then echo \"\$p\"; exit 0; fi
    done
    echo MISSING
  " 2>/dev/null | tail -1 | tr -d '\r\n'
}

find_or_mount_repo() {
  ensure_host_shared_folder || true
  local repo
  repo="$(resolve_repo_path)"
  if [[ "$repo" != "MISSING" && -n "$repo" ]]; then
    printf '%s' "$repo"
    return 0
  fi
  ensure_guest_share_mounted
  repo="$(resolve_repo_path)"
  if [[ "$repo" != "MISSING" && -n "$repo" ]]; then
    printf '%s' "$repo"
    return 0
  fi
  printf 'MISSING'
}

mount_guest_share_from_host() {
  ensure_host_shared_folder || true
  local repo
  repo="$(resolve_repo_path)"
  if [[ "$repo" != "MISSING" && -n "$repo" ]]; then
    echo "==> Shared folder already available: ${repo}" >&2
    printf '%s' "$repo"
    return 0
  fi
  ensure_guest_share_mounted
  repo="$(resolve_repo_path)"
  if [[ "$repo" == "MISSING" || -z "$repo" ]]; then
    printf 'MISSING'
    return 1
  fi
  echo "==> Shared folder mounted: ${repo}" >&2
  printf '%s' "$repo"
  return 0
}

prepare_guest_session() {
  require_vbox
  ensure_vm_running
  if ! wait_for_guest_login; then
    print_manual_build_instructions
    exit 1
  fi
}

prepare_guest_session_for_mount() {
  require_vbox
  ensure_vm_running
  ensure_host_shared_folder || true
  if test_guest_auth; then
    echo "==> Guest session ready."
    return 0
  fi

  echo "==> guestcontrol を確認中..."
  guest_auth_diagnose

  if guest_additions_broken_while_logged_in; then
    explain_guest_additions_blocked
    exit 0
  fi

  local max_sec="${LOGIN_WAIT_SEC:-90}"
  echo "==> ログイン待ち (最大 ${max_sec}s)..."
  if wait_for_guest_login; then
    return 0
  fi
  host_only_mount_fallback
  exit 0
}
