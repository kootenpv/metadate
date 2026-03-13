<p align="center">
  <img src="logo.jpg" alt="metadate logo" width="400">
</p>

<h1 align="center">metadate</h1>

<p align="center">
  Fast natural-language date parsing for Python — with an optional C-accelerated scanner.
</p>

<p align="center">
  <a href="https://pypi.org/project/metadate/"><img src="https://img.shields.io/pypi/v/metadate" alt="PyPI"></a>
  <a href="https://pypi.org/project/metadate/"><img src="https://img.shields.io/pypi/pyversions/metadate" alt="Python"></a>
  <a href="LICENSE"><img src="https://img.shields.io/github/license/pascal/metadate" alt="License"></a>
</p>

---

**metadate** parses human-written date expressions into structured `datetime` ranges.
It handles everything from `"tomorrow at 3 pm"` to `"the last 2 weeks of March 2024"` — and does it fast, with an optional C extension that provides up to **50x** speedup over pure-Python regex scanning.

## Features

- **Natural language** — understands relative dates (`"next tuesday"`, `"3 days ago"`), absolute dates (`"June 25"`, `"2024-01-15"`), times (`"at 3pm"`), ranges, and combinations
- **Fast C scanner** — optional C extension using a FNV-1a hash table for sub-millisecond parsing
- **ISO 8601 fast-path** — optional `ciso8601` integration for near-instant ISO date parsing
- **Granularity levels** — know whether a result has year, month, day, hour, minute, or second precision
- **Start & end dates** — every result is a period with `start_date` and `end_date`
- **Multi-date extraction** — pull all dates out of a block of text
- **Timezone support** — recognizes timezone names and abbreviations
- **Locale support** — English and Dutch built-in, extensible to other languages
- **CLI included** — parse dates from the command line

## Installation

```bash
pip install metadate
```

For faster ISO 8601 parsing:

```bash
pip install metadate[fastciso]
```

The C extension is compiled automatically when a C compiler is available. If compilation fails (e.g. no compiler installed), **metadate** falls back to the pure-Python scanner — no functionality is lost, only speed.

## Quick start

```python
from metadate import parse_date

# Simple dates
r = parse_date("tomorrow at 3pm")
print(r.start_date)  # 2026-03-13 15:00:00
print(r.end_date)    # 2026-03-13 16:00:00

# Relative dates
r = parse_date("last 2 weeks")
print(r.start_date)  # 2 weeks ago
print(r.end_date)    # now

# Absolute dates
r = parse_date("June 25, 2024")
print(r.start_date)  # 2024-06-25 00:00:00
print(r.end_date)    # 2024-06-26 00:00:00

# Extract multiple dates from text
results = parse_date(
    "The meeting moved from March 5 to March 12 at 2pm",
    multi=True
)
for r in results:
    print(r.start_date, "—", r.end_date)
```

## Using the C scanner

The C scanner is used automatically when available. To explicitly enable or disable it:

```python
# Force C scanner
r = parse_date("next friday", use_c_scanner=True)

# Force pure-Python scanner
r = parse_date("next friday", use_c_scanner=False)
```

## API reference

### `parse_date(text, **kwargs) -> MetaPeriod | list[MetaPeriod] | None`

| Parameter | Type | Default | Description |
|---|---|---|---|
| `text` | `str` | — | The text to parse |
| `reference_date` | `datetime` | `now()` | Anchor for relative dates |
| `future` | `relativedelta` | `30 years` | How far ahead to search |
| `past` | `relativedelta` | `100 years` | How far back to search |
| `lang` | `str` | `"en"` | Locale (`"en"` or `"nl"`) |
| `multi` | `bool` | `False` | Return all dates found |
| `use_c_scanner` | `bool` | `False` | Use C-accelerated scanner |
| `min_level` | `Units` | `None` | Minimum granularity filter |
| `max_level` | `Units` | `None` | Maximum granularity filter |
| `verbose` | `bool` | `False` | Print debug info |

### `MetaPeriod`

| Property | Type | Description |
|---|---|---|
| `start_date` | `datetime` | Start of the parsed period |
| `end_date` | `datetime` | End of the parsed period |
| `levels` | `set[Units]` | Granularity levels present |
| `matches` | `list[str]` | Matched text fragments |
| `has_time` | `bool` | Whether time was specified |
| `has_day` | `bool` | Whether day was specified |
| `has_month` | `bool` | Whether month was specified |
| `has_year` | `bool` | Whether year was specified |
| `is_in_past` | `bool` | Whether the date is in the past |
| `tz` | `tzinfo\|None` | Timezone if detected |
| `to_dict()` | `dict` | Serialize to dictionary |

### `Units`

Granularity levels from coarsest to finest:

```
YEAR (9) > SEASON (8) > QUARTER (7) > MONTH (6) > WEEK (5) > DAY (4) > HOUR (3) > MINUTE (2) > SECOND (1) > MICROSECOND (0)
```

Every `MetaPeriod` carries a `levels` set that tells you exactly which components were specified in the input. Use `min_level` and `max_level` on `parse_date` to filter results by granularity:

```python
from metadate import parse_date, Units

# "June 2024" has levels {YEAR, MONTH} — no day or time
r = parse_date("June 2024")
print(r.levels)     # {<Units.YEAR: 9>, <Units.MONTH: 6>}
print(r.has_month)  # True
print(r.has_day)    # False
print(r.has_time)   # False

# "tomorrow at 3pm" has levels {DAY, HOUR, MINUTE}
r = parse_date("tomorrow at 3pm")
print(r.levels)     # {<Units.DAY: 4>, <Units.HOUR: 3>, <Units.MINUTE: 2>}
print(r.has_time)   # True
print(r.min_level)  # Units.MINUTE (finest level present)
print(r.max_level)  # Units.DAY    (coarsest level present)

# Filter: only accept results that include at least HOUR precision
# min_level=Units.HOUR means "the finest level must be HOUR or finer"
r = parse_date("June 2024", min_level=Units.HOUR)
print(r)  # None — "June 2024" only has MONTH precision, no time

r = parse_date("tomorrow at 3pm", min_level=Units.HOUR)
print(r.start_date)  # matches — has HOUR

# Filter: only accept results no coarser than MONTH
# max_level=Units.MONTH means "the coarsest level must be MONTH or finer"
r = parse_date("2024", max_level=Units.MONTH)
print(r)  # None — "2024" has YEAR as its coarsest level

r = parse_date("June 2024", max_level=Units.MONTH)
print(r.start_date)  # matches — coarsest level is MONTH

# Multi-date extraction with level filtering
text = "Sometime in 2024, specifically June 15 at 10am"
results = parse_date(text, multi=True, min_level=Units.DAY)
# Only returns "June 15 at 10am", skips "2024" (no day precision)
```

## CLI

```bash
# Parse a single expression
metadate tomorrow at 3pm

# Parse with a specific locale
metadate --lang nl overmorgen om 15 uur

# Distance between two dates
metadate "January 1" "March 15"
```

## Supported expressions

| Category | Examples |
|---|---|
| **Today/tomorrow** | `today`, `tomorrow`, `yesterday`, `day after tomorrow` |
| **Weekdays** | `next tuesday`, `last friday`, `this wednesday` |
| **Months** | `June`, `June 2024`, `June 25`, `June 25, 2024` |
| **Relative** | `3 days ago`, `in 2 weeks`, `next month`, `last year` |
| **ISO dates** | `2024-01-15`, `2024-01-15T10:30:00` |
| **Times** | `at 3pm`, `10:30`, `15:00:00` |
| **Ranges** | `last 2 weeks`, `first 3 days of March` |
| **Seasons** | `winter`, `next spring`, `last summer` |
| **Quarters** | `Q1`, `Q3 2024`, `last quarter` |
| **Combined** | `next tuesday at 3pm`, `June 25 2024 10:30am` |

## License

[MIT](LICENSE)
