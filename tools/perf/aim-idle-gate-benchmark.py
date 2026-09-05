"""Compile the production aim admission gate in isolation (no Qt/ROM required).

Compares a git baseline to the working tree. Guest sampling is a counted stub:
times describe gate overhead only, never gameplay latency or real RAM cost.
Usage: python tools/perf/aim-idle-gate-benchmark.py --compiler PATH --baseline REF
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = "src/frontend/qt_sdl/MelonPrimeGameInput.cpp"


def gate(source, name):
    start = source.index("    HOT_FUNCTION void MelonPrimeCore::ProcessAimInputMouse()")
    end = source.index("            // P-17: Accumulate into residual", start)
    body = source[start:end].replace(
        "HOT_FUNCTION void MelonPrimeCore::ProcessAimInputMouse()",
        f"__attribute__((noinline)) void {name}()",
    )
    return body + "            ++accepted;\n        }\n    }\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--baseline", default="HEAD")
    args = parser.parse_args()
    before = subprocess.check_output(
        ["git", "show", f"{args.baseline}:{SOURCE}"], cwd=ROOT, encoding="utf-8"
    )
    after = (ROOT / SOURCE).read_text(encoding="utf-8")
    harness = r'''
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
struct StateFlags { enum { BIT_LAST_FOCUSED }; };
struct Flags { bool focused = true; bool test(int) const { return focused; } };
struct Gate {
    int m_nativeAimDeltaX = 1, m_nativeAimDeltaY = 1;
    uint32_t m_aimBlockBits = 0;
    bool m_isLayoutChangePending = false;
    Flags m_flags;
    struct { int32_t mouseX = 0, mouseY = 0; } m_input;
    int64_t m_aimResidualX = 7, m_aimResidualY = -9;
    bool m_enableZoomAimScale = true, m_enableAimAccumulator = true;
    uint64_t samples = 0, accepted = 0, resets = 0;
    void HandleAimEarlyReset() { ++resets; }
    __attribute__((noinline)) void UpdateZoomAimEffectiveScale() { ++samples; }
'''
    harness += gate(before, "Before") + gate(after, "After") + "};\n"
    harness += r'''
void require(bool value) { if (!value) std::abort(); }
int main() {
    for (int mask = 0; mask < 256; ++mask) {
        Gate a;
        a.m_flags.focused = mask & 1;
        a.m_aimBlockBits = (mask & 2) != 0;
        a.m_isLayoutChangePending = mask & 4;
        a.m_enableZoomAimScale = mask & 8;
        a.m_enableAimAccumulator = mask & 16;
        a.m_input.mouseX = (mask & 32) ? -3 : 0;
        a.m_input.mouseY = (mask & 64) ? 2 : 0;
        if (mask & 128) a.m_aimResidualX = a.m_aimResidualY = 0;
        Gate b = a;
        a.Before(); b.After();
        require(a.accepted == b.accepted && a.resets == b.resets);
        require(a.m_nativeAimDeltaX == b.m_nativeAimDeltaX);
        require(a.m_nativeAimDeltaY == b.m_nativeAimDeltaY);
        require(a.m_aimResidualX == b.m_aimResidualX);
        require(a.m_aimResidualY == b.m_aimResidualY);
        require(b.samples == (b.accepted && b.m_enableZoomAimScale ? 1u : 0u));
    }
    // Idle zoom transitions defer sampling until the first real input.
    Gate resume;
    resume.After(); require(resume.samples == 0);
    resume.m_input.mouseX = 1;
    resume.After(); require(resume.samples == 1 && resume.accepted == 1);
    std::puts("PASS: 256 admission/residual combinations and idle-to-motion refresh");
    constexpr int count = 10000000;
    for (int trial = 0; trial < 5; ++trial) {
        for (int order = 0; order < 2; ++order) {
            const bool after = ((trial + order) & 1) != 0;
            Gate state;
            const auto start = std::chrono::steady_clock::now();
            if (after) for (int i = 0; i < count; ++i) state.After();
            else for (int i = 0; i < count; ++i) state.Before();
            const auto elapsed = std::chrono::steady_clock::now() - start;
            const double ns = std::chrono::duration<double, std::nano>(elapsed).count() / count;
            std::printf("trial=%d %s %.3f ns/gate samples=%llu\n", trial,
                after ? "after" : "before", ns, (unsigned long long)state.samples);
        }
    }
}
'''
    with tempfile.TemporaryDirectory(prefix="aim-idle-gate-") as tmp:
        cpp = Path(tmp) / "gate.cpp"
        exe = Path(tmp) / "gate.exe"
        cpp.write_text(harness, encoding="utf-8")
        subprocess.run([args.compiler, "-O3", "-std=c++17", "-D_WIN32",
                        str(cpp), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
