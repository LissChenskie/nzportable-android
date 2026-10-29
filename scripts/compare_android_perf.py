#!/usr/bin/env python3
"""Compare on-device NZP_PERF records. Missing measurements remain N/A."""
from __future__ import annotations

import re
import statistics
import sys
from pathlib import Path

METRICS = (
    ("fps", "Average FPS", "higher"),
    ("low_1pct_fps", "1% low FPS", "higher"),
    ("frame_p50_ms", "P50 frame time, ms", "lower"),
    ("frame_p95_ms", "P95 frame time, ms", "lower"),
    ("frame_p99_ms", "P99 frame time, ms", "lower"),
    ("cpu_ms_avg", "CPU thread time, ms", "lower"),
    ("cpu_ms_max", "Peak CPU thread ms", "lower"),
    ("draw_calls_per_frame", "Draw calls / frame", "lower"),
    ("flushes_per_frame", "GLES flushes / frame", "lower"),
    ("texture_binds_per_frame", "Texture binds / frame", "lower"),
    ("texture_params_per_frame", "Texture parameter updates / frame", "lower"),
    ("shader_switches_per_frame", "Shader switches / frame", "lower"),
    ("uniform_updates_per_frame", "Uniform updates / frame", "lower"),
    ("vbo_uploads_per_frame", "VBO uploads / frame", "lower"),
    ("vbo_upload_kib_per_frame", "VBO upload KiB / frame", "lower"),
    ("ibo_upload_kib_per_frame", "IBO upload KiB / frame", "lower"),
    ("skipped_states_per_frame", "Skipped redundant state calls / frame", "higher"),
    ("cpu_percent_one_core", "CPU use as % of 1 core", "lower"),
    ("rss_peak_kb", "Process peak RSS, KiB", "lower"),
)
PATTERN = re.compile(r"([A-Za-z0-9_]+)=([-+]?[0-9]+(?:\.[0-9]+)?)")


def read_samples(filename: str | Path) -> list[dict[str, float]]:
    samples = []
    for line in Path(filename).read_text(encoding="utf-8", errors="replace").splitlines():
        if "NZP_PERF " not in line:
            continue
        values = {}
        for key, val in PATTERN.findall(line.split("NZP_PERF ", 1)[1]):
            try:
                values[key] = float(val)
            except ValueError:
                pass
        if "fps" in values:
            samples.append(values)
    return samples


def metric_mean(samples: list[dict[str, float]], key: str) -> float | None:
    values = [sample[key] for sample in samples if key in sample]
    return statistics.mean(values) if values else None


def format_metric(value: float | None) -> str:
    return "Не измерено" if value is None else f"{value:.2f}"


def compare(before: list[dict[str, float]], after: list[dict[str, float]]) -> str:
    if not before or not after:
        raise ValueError("Нет NZP_PERF в одном из логов. Включите PERFORMANCE LOG.")
    lines = [
        f"Сэмплов: до {len(before)}, после {len(after)}",
        f"{'Метрика':40s} {'До':>15} {'После':>15} {'Изменение':>14}",
    ]
    for key, label, preference in METRICS:
        a, b = metric_mean(before, key), metric_mean(after, key)
        if a is None or b is None or a == 0:
            change = "Не измерено"
        else:
            change = f"{(b/a-1.0)*100:+.1f}%"
        lines.append(f"{label:40s} {format_metric(a):>15} "
                     f"{format_metric(b):>15} {change:>14}")
    lines.append("Сравнивайте одинаковую карту, 3D-разрешение, FPS/VSync, "
                 "освещение и одинаковые сцены. Часть драйверных счётчиков "
                 "недоступна в старых сборках.")
    lines.append("GPU время и мощность: без аппаратного замера — Не измерено.")
    return "\n".join(lines)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("Usage: python scripts/compare_android_perf.py before/logcat.txt after/logcat.txt")
    try:
        print(compare(read_samples(sys.argv[1]), read_samples(sys.argv[2])))
    except (OSError, ValueError) as exc:
        sys.exit(str(exc))
