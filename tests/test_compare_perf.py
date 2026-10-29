#!/usr/bin/env python3
"""Regression: log parser never fabricates missing baseline GPU or GLES counters."""
import importlib.util
from pathlib import Path
import tempfile

path=Path(__file__).resolve().parents[1]/"scripts"/"compare_android_perf.py"
spec=importlib.util.spec_from_file_location("compare_android_perf",path)
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

old="2026 NZP_PERF fps=59.0 low_1pct_fps=44.0 cpu_ms_avg=8.0 draw_calls_per_frame=120.0 rss_peak_kb=50000\n"
new="2026 NZP_PERF fps=60.0 low_1pct_fps=52.0 frame_p99_ms=21.0 frame_p95_ms=19.0 frame_p50_ms=16.7 cpu_ms_avg=6.0 draw_calls_per_frame=90.0 flushes_per_frame=30.0 texture_binds_per_frame=17.0 vbo_upload_kib_per_frame=32.0 rss_peak_kb=51000 gpu_ms=unavailable\n"
with tempfile.TemporaryDirectory() as folder:
    before=Path(folder)/"before.log"
    after=Path(folder)/"after.log"
    before.write_text(old)
    after.write_text(new)
    result=module.compare(module.read_samples(before),module.read_samples(after))
    assert "60.00" in result
    assert "Не измерено" in result
    assert "P99" in result
    assert "Texture binds" in result
    assert "CPU" in result

try:
    module.compare([], [{"fps":60}])
except ValueError:
    pass
else:
    raise AssertionError("Empty logs should be rejected")
print("Android log comparison tests passed")
