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
  VBoxManage guestcontrol "$VM_NAME" run \
    --exe /bin/bash \
    --username "$GUEST_USER" \
    --password "$GUEST_PASS" \
    --wait-stdout --wait-stderr \
    --timeout 7200000 \
    -- -lc "$cmd"
}

guest_additions_version() {
  VBoxManage guestproperty get "$VM_NAME" "/VirtualBox/GuestInfo/GuestAdditionsVersion" 2>/dev/null \
    | awk -F': ' 'NF > 1 { print $2 }' | tr -d ' \r\n'
}

guest_auth_diagnose() {
  local ga err
  ga="$(guest_additions_version)"
  if [[ -z "$ga" || "$ga" == "No"*"value"* ]]; then
    echo "    Guest Additions: not reporting (log into desktop, or re-run Guest-Finish)"
  else
    echo "    Guest Additions: ${ga}"
  fi
  err="$(VBoxManage guestcontrol "$VM_NAME" run \
    --exe /bin/true \
    --username "$GUEST_USER" \
    --password "$GUEST_PASS" \
    --wait-stdout --wait-stderr 2>&1)" || true
  if [[ -n "$err" && "$err" != *"successfully"* && "$err" != *"exit code 0"* ]]; then
    echo "    guestcontrol: ${err//$'\n'/ }" | head -c 240
    echo
  fi
}

test_guest_auth() {
  guest_run "echo guestcontrol-ok" >/dev/null 2>&1
}

wait_for_guest_login() {
  local elapsed=0 interval=15
  echo
  echo "================================================================"
  echo "  VirtualBox ウィンドウをクリックして Ubuntu にログインしてください"
  echo "  ユーザー: ${GUEST_USER}  /  歯車 → Ubuntu on Xorg"
  echo "  デスクトップが表示されるまで待つ（ログイン画面のままでは不可）"
  echo "================================================================"
  while [[ "$elapsed" -lt "$LOGIN_WAIT_SEC" ]]; do
    if test_guest_auth; then
      echo "==> Guest session ready."
      return 0
    fi
    sleep "$interval"
    elapsed=$((elapsed + interval))
    echo "    Waiting for login... (${elapsed}s / ${LOGIN_WAIT_SEC}s)"
    if (( elapsed % 60 == 0 )); then
      guest_auth_diagnose
    fi
  done
  echo
  echo "Timed out waiting for guestcontrol." >&2
  print_manual_build_instructions
  exit 1
}

print_manual_build_instructions() {
  cat <<EOF

Ubuntu 内ターミナル (Ctrl+Alt+T) — 短いコマンドを1行ずつ:

  sudo modprobe vboxsf
  sudo mkdir -p /mnt/mp
  sudo mount -t vboxsf ${SHARE_NAME} /mnt/mp
  ls /mnt/mp
  bash /mnt/mp/tools/linux-vm/guest/guest-build-only.sh

EOF
}

ensure_host_shared_folder() {
  if [[ ! -d "$HOST_REPO" ]]; then
    echo "Host repo not found: $HOST_REPO" >&2
    return 1
  fi

  local state
  state="$(vm_state)"
  if [[ "$state" == "running" ]]; then
    echo "==> Adding transient shared folder (VM is running)..." >&2
    VBoxManage sharedfolder remove "$VM_NAME" --name "$SHARE_NAME" --transient 2>/dev/null || true
    if ! VBoxManage sharedfolder add "$VM_NAME" \
      --name "$SHARE_NAME" \
      --hostpath "$HOST_REPO" \
      --automount \
      --transient 2>/dev/null; then
      echo "==> Transient share skipped (will try guest mount)." >&2
    fi
    return 0
  fi

  echo "==> Refreshing host shared folder config..." >&2
  VBoxManage sharedfolder remove "$VM_NAME" --name "$SHARE_NAME" 2>/dev/null || true
  VBoxManage sharedfolder add "$VM_NAME" \
    --name "$SHARE_NAME" \
    --hostpath "$HOST_REPO" \
    --automount
}

ensure_guest_share_mounted() {
  echo "==> Mounting ${SHARE_NAME} inside guest (${REPO_GUEST_MOUNT})..." >&2
  guest_sudo "
    modprobe vboxsf
    usermod -aG vboxsf ${GUEST_USER}
    umount -l ${REPO_GUEST_MOUNT} 2>/dev/null || true
    rmdir ${REPO_GUEST_MOUNT} 2>/dev/null || true
    mkdir -p ${REPO_GUEST_MOUNT}
    mount -t vboxsf ${SHARE_NAME} ${REPO_GUEST_MOUNT}
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

prepare_guest_session() {
  require_vbox
  ensure_vm_running
  wait_for_guest_login
}
