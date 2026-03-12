"""Benchmark old Scanner vs NewScanner vs CScanner."""

import timeit
from datetime import datetime
from metadate import parse_date

REF = datetime(2024, 6, 15, 12, 0, 0)

# Check if CScanner is available
try:
    from metadate.c_scanner import CScanner
    HAS_C = True
except ImportError:
    HAS_C = False

# Warm up all scanners
parse_date("tomorrow", reference_date=REF)
parse_date("tomorrow", reference_date=REF, use_new_scanner=True)
if HAS_C:
    parse_date("tomorrow", reference_date=REF, use_c_scanner=True)

INPUTS = [
    ("short (15 chars)", "tomorrow at 3pm"),
    ("medium (54 chars)", "the deadline is March 2025 and we need to ship by then"),
    (
        "long (212 chars)",
        "Hi team, as discussed on the call with 12 stakeholders and 4 engineering leads, "
        "the rollout is planned for June 25 and the rollback window closes on July 10. "
        "Please coordinate with the 3 regional offices. Thanks!",
    ),
    ("dense date (42 chars)", "2017-06-25 00:01:01 after next week tuesday"),
]

N = 10000

if HAS_C:
    header = f"{'':30s} {'Old (ms)':>10s} {'New (ms)':>10s} {'C (ms)':>10s} {'C Speedup':>10s}"
    print(header)
    print("-" * 74)
else:
    header = f"{'':30s} {'Old (ms)':>10s} {'New (ms)':>10s} {'Speedup':>8s}"
    print(header)
    print("-" * 62)

for label, text in INPUTS:
    t_old = timeit.timeit(lambda t=text: parse_date(t, reference_date=REF), number=N)
    t_new = timeit.timeit(
        lambda t=text: parse_date(t, reference_date=REF, use_new_scanner=True),
        number=N,
    )
    ms_old = t_old / N * 1000
    ms_new = t_new / N * 1000

    if HAS_C:
        t_c = timeit.timeit(
            lambda t=text: parse_date(t, reference_date=REF, use_c_scanner=True),
            number=N,
        )
        ms_c = t_c / N * 1000
        speedup_c = t_old / t_c
        print(f"{label:30s} {ms_old:10.3f} {ms_new:10.3f} {ms_c:10.3f} {speedup_c:9.1f}x")
    else:
        speedup = t_old / t_new
        print(f"{label:30s} {ms_old:10.3f} {ms_new:10.3f} {speedup:7.1f}x")

# Multi mode on long text
long_text = INPUTS[-1][1]
t_old = timeit.timeit(
    lambda: parse_date(long_text, reference_date=REF, multi=True), number=N
)
t_new = timeit.timeit(
    lambda: parse_date(long_text, reference_date=REF, multi=True, use_new_scanner=True),
    number=N,
)
ms_old = t_old / N * 1000
ms_new = t_new / N * 1000

if HAS_C:
    t_c = timeit.timeit(
        lambda: parse_date(long_text, reference_date=REF, multi=True, use_c_scanner=True),
        number=N,
    )
    ms_c = t_c / N * 1000
    speedup_c = t_old / t_c
    print(f"{'long multi':30s} {ms_old:10.3f} {ms_new:10.3f} {ms_c:10.3f} {speedup_c:9.1f}x")
else:
    speedup = t_old / t_new
    print(f"{'long multi':30s} {ms_old:10.3f} {ms_new:10.3f} {speedup:7.1f}x")
