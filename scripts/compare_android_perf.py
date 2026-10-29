#!/usr/bin/env python3
"""Compare on-device NZP_PERF logs; no invented performance numbers."""
import re
import statistics
import sys
from pathlib import Path

KEYS = ("fps", "low_1pct_fps", "cpu_ms_avg", "cpu_ms_max",
        "draw_calls_per_frame", "cpu_percent_one_core", "rss_peak_kb")
def read_samples(filename):
    out = []
    for line in Path(filename).read_text(encoding="utf-8", errors="replace").splitlines():
        if "NZP_PERF " not in line:
            continue
        values = {}
        for key, val in re.findall(r"([a-zA-Z0-9_]+)=([0-9.]+)", line):
            try:
                values[key] = float(val)
            except ValueError:
                pass
        if all(k in values for k in KEYS):
            out.append(values)
    return out

def summary(samples):
    return {key: statistics.mean(sample[key] for sample in samples) for key in KEYS}

def main():
    if len(sys.argv) != 3:
        sys.exit("Usage: compare_android_perf.py before/logcat.txt after/logcat.txt")
    before = read_samples(sys.argv[1])
    after = read_samples(sys.argv[2])
    if not before or not after:
        sys.exit("No complete NZP_PERF records; enable PERFORMANCE LOG in both APKs.")
    first, second = summary(before), summary(after)
    print("Hardware benchmark comparison, samples:", len(before), "before,", len(after), "after")
    print(f"{'Metric':26s} {'Before':>13} {'After':>13} {'Delta':>13}")
    for key in KEYS:
        a, b = first[key], second[key]
        pct = (b / a - 1.0) * 100 if a else 0
        print(f"{key:26s} {a:13.2f} {b:13.2f} {pct:+12.1f}%")
    print("Results are meaningful only with matched map, resolution, thermals and gameplay.")

if __name__ == "__main__":
    main()
