"""Benchmark old Scanner vs CScanner vs ciso8601 fast-path."""

import sys
import timeit
from datetime import datetime
from metadate import parse_date

REF = datetime(2024, 6, 15, 12, 0, 0)

# Check available backends
try:
    from metadate.c_scanner import CScanner
    HAS_C = True
except ImportError:
    HAS_C = False

try:
    from ciso8601 import parse_datetime
    HAS_CISO = True
except ImportError:
    HAS_CISO = False

# Warm up all scanners
parse_date("tomorrow", reference_date=REF)
if HAS_C:
    parse_date("tomorrow", reference_date=REF, use_c_scanner=True)

INPUTS = [
    ("short", "tomorrow at 3pm"),
    ("medium", "the deadline is March 2025 and we need to ship by then"),
    (
        "long",
        "Hi team, as discussed on the call with 12 stakeholders and 4 engineering leads, "
        "the rollout is planned for June 25 and the rollback window closes on July 10. "
        "Please coordinate with the 3 regional offices. Thanks!",
    ),
    ("dense date", "2017-06-25 00:01:01 after next week tuesday"),
    ("ISO date", "2024-01-15"),
    ("ISO datetime", "2017-06-25 14:30:00"),
    ("ISO T-sep", "2017-06-25T14:30:00"),
    ("ISO microsec", "2017-06-25 14:30:00.123456"),
    ("RFC 2822", "Sat, 15 Jun 2024 12:00:00"),
    ("RFC 2822 + tz", "Sat, 15 Jun 2024 12:00:00 +0000"),
]

N = 10000

# Detect fast-path
pd_mod = sys.modules['metadate.parse_date']
fast_path_active = HAS_CISO and getattr(pd_mod, '_ciso_parse', None) is not None

cols = f"{'':20s} {'Old (ms)':>10s}"
if HAS_C:
    cols += f" {'C (ms)':>10s} {'C Speedup':>10s}"
if fast_path_active:
    cols += f" {'fast?':>6s}"
print(cols)
print("-" * len(cols))

for label, text in INPUTS:
    t_old = timeit.timeit(lambda t=text: parse_date(t, reference_date=REF), number=N)
    ms_old = t_old / N * 1000
    row = f"{label:20s} {ms_old:10.3f}"

    if HAS_C:
        t_c = timeit.timeit(
            lambda t=text: parse_date(t, reference_date=REF, use_c_scanner=True),
            number=N,
        )
        ms_c = t_c / N * 1000
        speedup_c = t_old / t_c
        row += f" {ms_c:10.3f} {speedup_c:9.1f}x"

    if fast_path_active:
        # Check if fast-path actually triggers for this input
        triggers = pd_mod._try_iso_fast(
            text, REF, REF + __import__('dateutil.relativedelta', fromlist=['relativedelta']).relativedelta(years=30),
            REF - __import__('dateutil.relativedelta', fromlist=['relativedelta']).relativedelta(years=100),
            __import__('metadate.locales.en', fromlist=['en']), False,
        ) is not None
        row += f" {'yes':>6s}" if triggers else f" {'no':>6s}"

    print(row)

# Multi mode
long_text = INPUTS[2][1]
t_old = timeit.timeit(
    lambda: parse_date(long_text, reference_date=REF, multi=True), number=N
)
ms_old = t_old / N * 1000
row = f"{'long multi':20s} {ms_old:10.3f}"
if HAS_C:
    t_c = timeit.timeit(
        lambda: parse_date(long_text, reference_date=REF, multi=True, use_c_scanner=True),
        number=N,
    )
    ms_c = t_c / N * 1000
    speedup_c = t_old / t_c
    row += f" {ms_c:10.3f} {speedup_c:9.1f}x"
if fast_path_active:
    row += f" {'no':>6s}"
print(row)

print(f"\nBackends: old scanner, C scanner={HAS_C}, ciso8601 fast-path={fast_path_active}")
