#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: .claude/skills/collect-perf-baseline.sh [--label NAME] [--out-dir DIR] -- <binary> [app args...]

Runs melonPrimeDS with MELONPRIME_PERF=1, tees stdout/stderr to an untracked
perf log, then writes a summary next to it.

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

mkdir -p "$out_dir"
timestamp="$(date +%Y%m%d-%H%M%S)"
log="$out_dir/${label}-perf-${timestamp}.log"
summary="${log%.log}.summary.txt"

echo "Writing perf log: $log"
echo "After the app opens, load the ROM, enter the agreed in-game scene, soak for 10 minutes, then quit cleanly."

set +e
MELONPRIME_PERF=1 "$binary" "$@" 2>&1 | tee "$log"
app_status=${PIPESTATUS[0]}
set -e

python3 "$repo_root/.claude/skills/summarize-melonprime-perf.py" \
    --markdown-platform "$label" "$log" | tee "$summary"
echo "Wrote perf summary: $summary"

exit "$app_status"
