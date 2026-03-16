# metadate

Natural language date parser with Python scanner and optional C-accelerated scanner.

## Build

```bash
pip install -e .          # builds C extension (_cscanner.c) in-place
```

Rebuild after any change to `_cscanner.c`.

## Test

```bash
pytest tests/ -x -q                # Python scanner
pytest tests/ -x -q --c-scanner    # C scanner
```

Always run both. A feature that works in one scanner may silently fail in the other.

## Architecture

- **Locales** (`metadate/locales/en.py`, `nl.py`): keyword dicts/lists (UNITS, MODIFIERS, WHITELIST, SKIP, etc.)
- **Python scanner** (`metadate/scanner.py`): `re.Scanner`-based tokenizer. Uses locale attrs directly via `self.__dict__.update()`.
- **C scanner** (`metadate/_cscanner.c`): C-accelerated tokenizer. Receives locale data as a dict built in `metadate/c_scanner.py`.
- **Parser** (`metadate/parse_date.py`): token pipeline: scan → `get_relevant_parts` → `merge_ordinal_unit` → `cleanup_relevant_parts` → `datify`. Use `verbose=True` to trace.

## Adding skip/whitelist words

Both scanners must be updated in sync:

1. **Locale**: add to `WHITELIST` (single + multi-word) and `SKIP` (for Python scanner's `re.Scanner` rule)
2. **Python scanner**: `SKIP` is picked up automatically via `pipe(self.SKIP)` → `None`
3. **C scanner**: single words go through `WT_WHITELIST` in the hash table. Multi-word phrases need `whitelist_multi` (built in `c_scanner.py`, matched in `do_try_multiword` in `_cscanner.c`)

## Tests

When fixing a parse bug or adding a feature, add a test to `tests/test_natural_language.py` in the relevant class. If you encounter a natural language phrase that looks like it should parse correctly but doesn't (or parses wrong), add it as a test case too.

## Debugging parse issues

```python
from metadate import parse_date
parse_date("your phrase", reference_date=some_dt, verbose=True)
```

Check the `matches` output for bundle splits — string tokens between Meta* objects act as separators in `get_relevant_parts`.
