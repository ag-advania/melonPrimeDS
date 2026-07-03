#!/usr/bin/env python3
"""Parse MelonPrimePerf stderr lines and print rolling / shutdown summaries.

Usage:
  MELONPRIME_PERF=1 ./melonPrimeDS 2>&1 | tee perf.log
  python3 .claude/skills/summarize-melonprime-perf.py perf.log

Reads lines prefixed with [MelonPrimePerf]. Supports 1 Hz window lines and the
shutdown histogram block emitted when the emu thread exits.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from typing import Iterable, TextIO


WINDOW_RE = re.compile(
    r"\[MelonPrimePerf\] frame_ms p50=(?P<p50>[0-9.]+) p95=(?P<p95>[0-9.]+) "
    r"p99=(?P<p99>[0-9.]+) max=(?P<max>[0-9.]+) n=(?P<n>[0-9]+)"
)

SHUTDOWN_RE = re.compile(
    r"\[MelonPrimePerf\] shutdown summary: frames=(?P<frames>[0-9]+) "
    r"frame_ms p50=(?P<p50>[0-9.]+) p95=(?P<p95>[0-9.]+) "
    r"p99=(?P<p99>[0-9.]+) max=(?P<max>[0-9.]+)"
)

HIST_RE = re.compile(
    r"^\s+(?P<lo>[0-9.]+)-(?P<hi>[0-9.]+) ms: (?P<count>[0-9]+)$"
)

SECTION_RE = re.compile(
    r"sec_avg_ms sleep=(?P<sleep>[0-9.]+) spin=(?P<spin>[0-9.]+) "
    r"input=(?P<input>[0-9.]+) run=(?P<run>[0-9.]+) "
    r"draw=(?P<draw>[0-9.]+) drain=(?P<drain>[0-9.]+)"
)

INPUT_RE = re.compile(
    r"input_src raw=(?P<raw>[0-9]+) mac=(?P<mac>[0-9]+) "
    r"linux=(?P<linux>[0-9]+) panel=(?P<panel>[0-9]+) "
    r"qcur=(?P<qcur>[0-9]+) \(tot=(?P<total>[0-9]+)\)"
)

COUNTER_RE = re.compile(
    r"warp=(?P<warp>[0-9]+) oog_patch=(?P<oog_patch>[0-9]+) "
    r"osd_apply=(?P<osd_apply>[0-9]+) osd_write=(?P<osd_write>[0-9]+)"
)

HUD_RE = re.compile(
    r"hud_dirty_px=(?P<hud_dirty_px>[0-9]+) gl_up_B=(?P<gl_up_b>[0-9]+) "
    r"dr3_skip=(?P<dr3_skip>[0-9]+) hud_render_us=(?P<hud_render_us>[0-9.]+)"
)


@dataclass
class WindowSample:
    p50: float
    p95: float
    p99: float
    max_ms: float
    n: int
    sleep_ms: float | None = None
    spin_ms: float | None = None
    input_ms: float | None = None
    run_ms: float | None = None
    draw_ms: float | None = None
    drain_ms: float | None = None
    input_raw: int = 0
    input_mac: int = 0
    input_linux: int = 0
    input_panel: int = 0
    input_qcur: int = 0
    input_total: int = 0
    warp: int = 0
    oog_patch: int = 0
    osd_apply: int = 0
    osd_write: int = 0
    hud_dirty_px: int = 0
    gl_up_b: int = 0
    dr3_skip: int = 0
    hud_render_us: float | None = None


@dataclass
class Report:
    windows: list[WindowSample] = field(default_factory=list)
    shutdown: WindowSample | None = None
    histogram: list[tuple[float, float, int]] = field(default_factory=list)
    hist_overflow: int = 0


def parse_lines(lines: Iterable[str]) -> Report:
    report = Report()
    in_hist = False

    for line in lines:
        line = line.rstrip("\n")
        if line.startswith("[MelonPrimePerf] histogram"):
            in_hist = True
            continue

        m = WINDOW_RE.search(line)
        if m:
            sample = WindowSample(
                p50=float(m.group("p50")),
                p95=float(m.group("p95")),
                p99=float(m.group("p99")),
                max_ms=float(m.group("max")),
                n=int(m.group("n")),
            )
            if section := SECTION_RE.search(line):
                sample.sleep_ms = float(section.group("sleep"))
                sample.spin_ms = float(section.group("spin"))
                sample.input_ms = float(section.group("input"))
                sample.run_ms = float(section.group("run"))
                sample.draw_ms = float(section.group("draw"))
                sample.drain_ms = float(section.group("drain"))
            if input_src := INPUT_RE.search(line):
                sample.input_raw = int(input_src.group("raw"))
                sample.input_mac = int(input_src.group("mac"))
                sample.input_linux = int(input_src.group("linux"))
                sample.input_panel = int(input_src.group("panel"))
                sample.input_qcur = int(input_src.group("qcur"))
                sample.input_total = int(input_src.group("total"))
            if counters := COUNTER_RE.search(line):
                sample.warp = int(counters.group("warp"))
                sample.oog_patch = int(counters.group("oog_patch"))
                sample.osd_apply = int(counters.group("osd_apply"))
                sample.osd_write = int(counters.group("osd_write"))
            if hud := HUD_RE.search(line):
                sample.hud_dirty_px = int(hud.group("hud_dirty_px"))
                sample.gl_up_b = int(hud.group("gl_up_b"))
                sample.dr3_skip = int(hud.group("dr3_skip"))
                sample.hud_render_us = float(hud.group("hud_render_us"))
            report.windows.append(sample)
            continue

        m = SHUTDOWN_RE.search(line)
        if m:
            report.shutdown = WindowSample(
                p50=float(m.group("p50")),
                p95=float(m.group("p95")),
                p99=float(m.group("p99")),
                max_ms=float(m.group("max")),
                n=int(m.group("frames")),
            )
            in_hist = True
            continue

        if in_hist:
            if line.startswith("  >="):
                parts = line.split(":")
                if len(parts) == 2:
                    report.hist_overflow = int(parts[1].strip())
                continue
            m = HIST_RE.match(line)
            if m:
                report.histogram.append(
                    (float(m.group("lo")), float(m.group("hi")), int(m.group("count")))
                )

    return report


def print_report(report: Report, out: TextIO) -> None:
    if not report.windows and not report.shutdown:
        print("No [MelonPrimePerf] lines found.", file=out)
        return

    if report.windows:
        print(f"1 Hz windows: {len(report.windows)}", file=out)
        last = report.windows[-1]
        print(
            f"  last window: p50={last.p50:.3f} p95={last.p95:.3f} "
            f"p99={last.p99:.3f} max={last.max_ms:.3f} n={last.n}",
            file=out,
        )
        if len(report.windows) > 1:
            p50s = [w.p50 for w in report.windows]
            p99s = [w.p99 for w in report.windows]
            print(
                f"  window p50 range: {min(p50s):.3f} .. {max(p50s):.3f}",
                file=out,
            )
            print(
                f"  window p99 range: {min(p99s):.3f} .. {max(p99s):.3f}",
                file=out,
            )
        print_window_details(report.windows, out)

    if report.shutdown:
        s = report.shutdown
        print(
            f"shutdown ({s.n} frames): p50={s.p50:.3f} p95={s.p95:.3f} "
            f"p99={s.p99:.3f} max={s.max_ms:.3f}",
            file=out,
        )

    if report.histogram:
        total = sum(c for _, _, c in report.histogram) + report.hist_overflow
        print(f"histogram buckets: {len(report.histogram)} (+overflow={report.hist_overflow}), total={total}", file=out)
        for lo, hi, count in report.histogram:
            bar = "#" * min(60, count // max(1, total // 60 or 1))
            print(f"  {lo:5.1f}-{hi:5.1f} ms: {count:6d} {bar}", file=out)


def median(values: list[float]) -> float:
    if not values:
        return 0.0
    sorted_values = sorted(values)
    mid = len(sorted_values) // 2
    if len(sorted_values) % 2:
        return sorted_values[mid]
    return (sorted_values[mid - 1] + sorted_values[mid]) / 2.0


def percentile(values: list[float], p: float) -> float:
    if not values:
        return 0.0
    sorted_values = sorted(values)
    idx = p * float(len(sorted_values) - 1)
    lo = int(idx)
    hi = min(lo + 1, len(sorted_values) - 1)
    frac = idx - float(lo)
    return sorted_values[lo] * (1.0 - frac) + sorted_values[hi] * frac


def format_num(value: float, suffix: str = "") -> str:
    return f"{value:.3f}{suffix}"


def summarize_for_tables(report: Report, platform: str) -> list[str]:
    if not report.shutdown and not report.windows:
        return ["No [MelonPrimePerf] data available for markdown rows."]

    frame = report.shutdown or report.windows[-1]
    section_windows = [w for w in report.windows if w.draw_ms is not None]
    draw_ms = median([w.draw_ms or 0.0 for w in section_windows]) if section_windows else 0.0

    total_frames = sum(w.n for w in report.windows)
    total_minutes = max(len(report.windows) / 60.0, 1.0 / 60.0)
    oog_per_min = sum(w.oog_patch for w in report.windows) / total_minutes
    osd_write_per_min = sum(w.osd_write for w in report.windows) / total_minutes
    hud_dirty_per_frame = (
        sum(w.hud_dirty_px for w in report.windows) / total_frames if total_frames else 0.0
    )
    gl_upload_per_frame = (
        sum(w.gl_up_b for w in report.windows) / total_frames if total_frames else 0.0
    )
    dr3_skip_per_frame = (
        sum(w.dr3_skip for w in report.windows) / total_frames if total_frames else 0.0
    )
    hud_render = [w.hud_render_us for w in report.windows if w.hud_render_us is not None]

    return [
        "V6 §8 frame row:",
        "| {} | {} | {} | {} | {} | {} | |".format(
            platform,
            format_num(frame.p50),
            format_num(frame.p95),
            format_num(frame.p99),
            format_num(frame.max_ms),
            format_num(draw_ms),
        ),
        "",
        "V6 §8 counter cells for this platform:",
        f"- `Patches_Apply(OutOfGameFrame)` / min: {oog_per_min:.1f}",
        f"- `OsdColor_ApplyOnce` write / min: {osd_write_per_min:.1f}",
        f"- HUD dirty area avg px/frame: {hud_dirty_per_frame:.1f}",
        f"- GL upload bytes/frame: {gl_upload_per_frame:.1f}",
        f"- DR3 hash skip/frame: {dr3_skip_per_frame:.3f}",
        "- `CustomHud_Render` p50/p99 us: {:.1f}/{:.1f}".format(
            percentile(hud_render, 0.50),
            percentile(hud_render, 0.99),
        ),
        "",
        "V5 Phase 0 frame row:",
        "| {} | {} | {} | {} | {} | |".format(
            platform,
            format_num(frame.p50),
            format_num(frame.p95),
            format_num(frame.p99),
            format_num(frame.max_ms),
        ),
    ]


def print_window_details(windows: list[WindowSample], out: TextIO) -> None:
    section_windows = [w for w in windows if w.draw_ms is not None]
    if section_windows:
        print("section median of 1 Hz averages:", file=out)
        print(
            "  sleep={:.3f} spin={:.3f} input={:.3f} run={:.3f} draw={:.3f} drain={:.3f}".format(
                median([w.sleep_ms or 0.0 for w in section_windows]),
                median([w.spin_ms or 0.0 for w in section_windows]),
                median([w.input_ms or 0.0 for w in section_windows]),
                median([w.run_ms or 0.0 for w in section_windows]),
                median([w.draw_ms or 0.0 for w in section_windows]),
                median([w.drain_ms or 0.0 for w in section_windows]),
            ),
            file=out,
        )

    total_frames = sum(w.n for w in windows)
    total_minutes = max(len(windows) / 60.0, 1.0 / 60.0)
    sums = {
        "raw": sum(w.input_raw for w in windows),
        "mac": sum(w.input_mac for w in windows),
        "linux": sum(w.input_linux for w in windows),
        "panel": sum(w.input_panel for w in windows),
        "qcur": sum(w.input_qcur for w in windows),
        "warp": sum(w.warp for w in windows),
        "oog_patch": sum(w.oog_patch for w in windows),
        "osd_apply": sum(w.osd_apply for w in windows),
        "osd_write": sum(w.osd_write for w in windows),
        "hud_dirty_px": sum(w.hud_dirty_px for w in windows),
        "gl_up_b": sum(w.gl_up_b for w in windows),
        "dr3_skip": sum(w.dr3_skip for w in windows),
    }
    if any(sums.values()):
        print("counter totals and rates:", file=out)
        print(
            "  input_src raw={} mac={} linux={} panel={} qcur={}".format(
                sums["raw"], sums["mac"], sums["linux"], sums["panel"], sums["qcur"]
            ),
            file=out,
        )
        print(
            "  per_min warp={:.1f} oog_patch={:.1f} osd_apply={:.1f} osd_write={:.1f} dr3_skip={:.1f}".format(
                sums["warp"] / total_minutes,
                sums["oog_patch"] / total_minutes,
                sums["osd_apply"] / total_minutes,
                sums["osd_write"] / total_minutes,
                sums["dr3_skip"] / total_minutes,
            ),
            file=out,
        )
        if total_frames:
            print(
                "  per_frame hud_dirty_px={:.1f} gl_upload_B={:.1f}".format(
                    sums["hud_dirty_px"] / total_frames,
                    sums["gl_up_b"] / total_frames,
                ),
                file=out,
            )

    hud_render = [w.hud_render_us for w in windows if w.hud_render_us is not None]
    if hud_render:
        print(f"custom HUD render median: {median(hud_render):.1f} us", file=out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "log",
        nargs="?",
        help="perf log file (default: stdin)",
    )
    parser.add_argument(
        "--markdown-platform",
        metavar="NAME",
        help="also print V6/V5 markdown table rows for the named platform",
    )
    args = parser.parse_args()

    if args.log:
        with open(args.log, encoding="utf-8", errors="replace") as f:
            report = parse_lines(f)
    else:
        report = parse_lines(sys.stdin)

    print_report(report, sys.stdout)
    if args.markdown_platform:
        print("", file=sys.stdout)
        for line in summarize_for_tables(report, args.markdown_platform):
            print(line, file=sys.stdout)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
