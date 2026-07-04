#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: .claude/skills/collect-perf-baseline.sh [--label NAME] [--out-dir DIR] -- <binary> [app args...]

Runs melonPrimeDS with MELONPRIME_PERF=1, tees stdout/stderr to an untracked
perf log, then writes a summary next to it.

If the default out dir is not writable (common on Linux VM shared folders where
`artifacts/` is owned by the host user), the script falls back to
`~/.local/share/melonprime-perf-baseline`, then `/tmp/melonprime-perf-baseline-$USER`.
Override with `--out-dir` or `MELONPRIME_PERF_OUT_DIR` / `MELONPRIME_PERF_FALLBACK_DIR`.

Examples:
  .claude/skills/collect-perf-baseline.sh --label macos -- \
    build-mac/melonPrimeDS.app/Contents/MacOS/melonPrimeDS

  .claude/skills/collect-perf-baseline.sh --label linux-vm -- \
    build-linux/melonPrimeDS
EOF
}

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/../.." && pwd)"
label="${MELONPRIME_PERF_LABEL:-$(uname -s | tr '[:upper:]' '[:lower:]')}"
out_dir="${MELONPRIME_PERF_OUT_DIR:-$repo_root/artifacts/perf-baseline}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --label)
            if [[ $# -lt 2 ]]; then
                usage >&2
                exit 2
            fi
            label="$2"
            shift 2
            ;;
        --out-dir)
            if [[ $# -lt 2 ]]; then
                usage >&2
                exit 2
            fi
            out_dir="$2"
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        --)
            shift
            break
            ;;
        *)
            break
            ;;
    esac
done

if [[ $# -lt 1 ]]; then
    usage >&2
    exit 2
fi

binary="$1"
shift

resolve_out_dir() {
    local dir="$1"
    mkdir -p "$dir" 2>/dev/null || true
    if [[ -d "$dir" && -w "$dir" ]]; then
        printf '%s\n' "$dir"
        return 0
    fi

    local fallback="${MELONPRIME_PERF_FALLBACK_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/melonprime-perf-baseline}"
    mkdir -p "$fallback" 2>/dev/null || true
    if [[ -d "$fallback" && -w "$fallback" ]]; then
        echo "Warning: cannot write to $dir (permission denied); using $fallback" >&2
        printf '%s\n' "$fallback"
        return 0
    fi

    fallback="/tmp/melonprime-perf-baseline-${USER:-$(id -un)}"
    mkdir -p "$fallback"
    echo "Warning: cannot write to $dir or $fallback; using $fallback" >&2
    printf '%s\n' "$fallback"
}

out_dir="$(resolve_out_dir "$out_dir")"
timestamp="$(date +%Y%m%d-%H%M%S)"
log="$out_dir/${label}-perf-${timestamp}.log"
summary="${log%.log}.summary.txt"

echo "Writing perf log: $log"
echo "After the app opens, load the ROM, enter the agreed in-game scene, soak for 10 minutes, then quit cleanly."

set +e
MELONPRIME_PERF=1 "$binary" "$@" 2>&1 | tee "$log"
tee_status=${PIPESTATUS[1]}
app_status=${PIPESTATUS[0]}
set -e

if [[ "$tee_status" -ne 0 || ! -f "$log" || ! -s "$log" ]]; then
    echo "Error: failed to write perf log: $log" >&2
    echo "Hint: set MELONPRIME_PERF_OUT_DIR to a writable directory, or rerun after fixing permissions on $repo_root/artifacts/perf-baseline" >&2
    exit 1
fi

python3 "$repo_root/.claude/skills/summarize-melonprime-perf.py" \
    --markdown-platform "$label" "$log" | tee "$summary"
if [[ "${PIPESTATUS[1]:-0}" -ne 0 || ! -f "$summary" || ! -s "$summary" ]]; then
    echo "Error: failed to write perf summary: $summary" >&2
    exit 1
fi
echo "Wrote perf summary: $summary"

exit "$app_status"
