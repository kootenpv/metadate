"""
Token-based scanner: single-pass tokenization + dict lookups.

Drop-in replacement for Scanner — same .scan(text) interface,
same Meta* output objects.
"""

import re

from metadate.classes import (
    MetaAnd,
    MetaModifier,
    MetaOrdinal,
    MetaRange,
    MetaRelative,
    MetaUnit,
)
from metadate.utils import Units, strip_pm


# ── Single tokenizer regex ────────────────────────────────────────────
# Groups are tried left-to-right; first match wins at each position.
# Named groups let us instantly know *what* matched.

TOKEN_RE = re.compile(
    r"(?P<hms_micro>\b\d{1,2}[:h]\d{2}[:m]\d{2}\.\d+\b)"
    r"|(?P<hms>\b\d{1,2}[:h]\d{2}[:m]\d{2}(?:\s?[ap]\.?m\.?)?\b)"
    r"|(?P<hm>\b\d{1,2}[:h']\d{2}(?:\s?[ap]\.?m\.?|[hm])?\b)"
    r"|(?P<hour_apm>\b\d{1,2}\s?(?:a\.?m\.?|p\.?m\.?|afternoon|o'?clock)\b)"
    r"|(?P<iso_date>\b[12]\d{3}[-/](?:0?[1-9]|1[0-2])[-/](?:0?[1-9]|[12]\d|3[01])\b)"
    r"|(?P<iso_compact>\b[12]\d{3}(?:0[1-9]|1[0-2])(?:0[1-9]|[12]\d|3[01])\b)"
    r"|(?P<dd_mm_yyyy>\b(?:0?[1-9]|[12]\d|3[01])[/ ](?:0?[1-9]|1[0-2])[/ ][12]\d{3}\b)"
    r"|(?P<year4>\b[12]\d{3}\b)"
    r"|(?P<quarter_q>\b[Qq][1-4]\b)"
    r"|(?P<ordinal_num>\b\d{1,2}(?:st|nd|rd|th)\b)"
    r"|(?P<number>\d+[.,]?\d*)"
    r"|(?P<and_half>\band a half\b)"
    r"|(?P<word>[a-zA-Z]+)"
    r"|(?P<punct>[!?]+|\.(?![a-zA-Z0-9.])|\n)"
    r"|(?P<skip>\s+|[-,])",
    re.IGNORECASE,
)


def _validate(day=None, month=None, year=None, hour=None, minute=None, second=None):
    if day is not None and not 1 <= day <= 31:
        return False
    if month is not None and not 1 <= month <= 12:
        return False
    if year is not None and not 1900 <= year <= 2100:
        return False
    if hour is not None and not 0 <= hour <= 23:
        return False
    if minute is not None and not 0 <= minute <= 59:
        return False
    if second is not None and not 0 <= second <= 59:
        return False
    return True


class NewScanner:
    """Token-based scanner — same interface as Scanner."""

    def __init__(self, locale, re_flags=None):
        # Build lookup dicts from locale — all lowercased keys
        self.MONTHS = {k.lower(): v for k, v in locale.MONTHS.items()}
        self.MONTHS_SHORTS = {k.lower(): v for k, v in locale.MONTHS_SHORTS.items()}
        self.WEEKDAY = {k.lower(): v for k, v in locale.WEEKDAY.items()}
        self.WEEKDAY_SHORTS = {k.lower(): v for k, v in locale.WEEKDAY_SHORTS.items()}
        self.MODIFIERS = {k.lower(): v for k, v in locale.MODIFIERS.items()}
        self.UNITS = {k.lower(): v for k, v in locale.UNITS.items()}
        self.ORDINAL_NUMBERS = {k.lower(): v for k, v in locale.ORDINAL_NUMBERS.items()}
        self.TODAY_TOMORROW = {k.lower(): v for k, v in locale.TODAY_TOMORROW.items()}
        self.SEASONS = {k.lower(): v for k, v in locale.SEASONS.items()}
        self.QUARTERS = {k.lower(): v for k, v in locale.QUARTERS.items()}
        self.IN_THE = [x.lower() for x in locale.IN_THE]
        self.AND = [x.lower() for x in locale.AND]
        self.NOW = [x.lower() for x in locale.NOW]
        self.NOON = {k.lower(): v for k, v in locale.NOON.items()}
        self.NOON_STANDALONE = {k: v for k, v in self.NOON.items() if v[0] == v[1]}
        self.WHITELIST = set(x.lower() for x in locale.WHITELIST)
        self.DD_LEFT_FIRST = locale.DD_LEFT_FIRST

        # Multi-word phrases that need lookahead (sorted longest first)
        self._today_keys = sorted(self.TODAY_TOMORROW.keys(), key=len, reverse=True)
        self._in_the_keys = sorted(self.IN_THE, key=len, reverse=True)
        self._quarter_keys = sorted(
            [k for k in self.QUARTERS.keys() if " " in k], key=len, reverse=True
        )
        # "a couple" is a multi-word ordinal
        self._multiword_ordinals = sorted(
            [k for k in self.ORDINAL_NUMBERS.keys() if " " in k], key=len, reverse=True
        )

    def scan(self, text):
        text = text.replace("\u2013", "-")
        results = []
        pos = 0
        text_lower = text.lower()
        n = len(text)

        while pos < n:
            # ── Multi-word phrase lookahead ──────────────────────────
            consumed = self._try_multiword(text_lower, pos, results)
            if consumed:
                pos += consumed
                continue

            # ── Single token via regex ───────────────────────────────
            m = TOKEN_RE.match(text, pos)
            if not m:
                # Should not happen — skip a char
                results.append(text[pos])
                pos += 1
                continue

            kind = m.lastgroup
            raw = m.group()
            span = m.span()
            low = raw.lower()
            pos = m.end()

            # After number/ordinal, peek ahead for "month" or "of month"
            if kind in ("number", "ordinal_num") and pos < n:
                merged = self._try_number_month(kind, raw, span, text, pos)
                if merged is not None:
                    results.append(merged[0])
                    pos = merged[1]
                    continue

            obj = self._classify(kind, raw, low, span, text)
            if obj is not None:
                results.append(obj)

        return results, []

    # ── Multi-word phrase matching ────────────────────────────────────

    def _try_multiword(self, text_lower, pos, results):
        """Check for multi-word phrases at `pos`. Returns chars consumed or 0."""
        chunk = text_lower[pos:]

        # "and a half"
        if chunk.startswith("and a half"):
            results.append(MetaOrdinal("0.5", span=(pos, pos + 10)))
            return 10

        # "in the", "over the", "during the", "within the"
        for phrase in self._in_the_keys:
            if chunk.startswith(phrase + " "):
                end = pos + len(phrase)
                results.append(MetaRange(phrase, span=(pos, end)))
                return len(phrase) + 1  # consume trailing space

        # "day after tomorrow", "day before yesterday", etc
        for phrase in self._today_keys:
            if " " in phrase and chunk.startswith(phrase):
                end = pos + len(phrase)
                # Check word boundary
                if end >= len(text_lower) or not text_lower[end].isalpha():
                    days = self.TODAY_TOMORROW[phrase]
                    results.append(MetaRelative(days=days, span=(pos, end)))
                    return len(phrase)

        # "first quarter", "second quarter", etc
        for phrase in self._quarter_keys:
            if chunk.startswith(phrase):
                end = pos + len(phrase)
                if end >= len(text_lower) or not text_lower[end].isalpha():
                    results.append(
                        MetaRelative(
                            month=self.QUARTERS[phrase],
                            day=1,
                            levels={Units.QUARTER},
                            span=(pos, end),
                        )
                    )
                    return len(phrase)

        # "a couple"
        for phrase in self._multiword_ordinals:
            if chunk.startswith(phrase):
                end = pos + len(phrase)
                if end >= len(text_lower) or not text_lower[end].isalpha():
                    results.append(
                        MetaOrdinal(self.ORDINAL_NUMBERS[phrase], span=(pos, end))
                    )
                    return len(phrase)

        # "on the 31st" pattern
        on_the_m = re.match(r"on the (\d{1,2})(st|nd|rd|th)\b", chunk)
        if on_the_m:
            day = int(on_the_m.group(1))
            if _validate(day=day):
                end = pos + on_the_m.end()
                results.append(MetaRelative(day=day, span=(pos, end)))
                return on_the_m.end()

        # "at NN" (hour)
        at_m = re.match(r"at (\d{1,2})h?\b(?![:']|[.] )", chunk)
        if at_m:
            hour = int(at_m.group(1))
            if _validate(hour=hour):
                end = pos + at_m.end()
                results.append(MetaRelative(hour=hour, span=(pos, end)))
                return at_m.end()

        return 0

    # ── Token classification ─────────────────────────────────────────

    def _classify(self, kind, raw, low, span, text):
        if kind == "skip":
            return None

        if kind == "punct":
            return "SENT"

        if kind == "and_half":
            return MetaOrdinal("0.5", span=span)

        if kind == "hms_micro":
            return self._parse_time(raw, span)

        if kind == "hms":
            return self._parse_time(raw, span)

        if kind == "hm":
            return self._parse_time(raw, span)

        if kind == "hour_apm":
            return self._parse_time(raw, span)

        if kind == "quarter_q":
            if low in self.QUARTERS:
                return MetaRelative(
                    month=self.QUARTERS[low], day=1,
                    levels={Units.QUARTER}, span=span,
                )
            return raw

        if kind == "iso_date":
            parts = re.split(r"[-/]", raw)
            year, month, day = int(parts[0]), int(parts[1]), int(parts[2])
            if _validate(year=year, month=month, day=day):
                return MetaRelative(year=year, month=month, day=day, span=span)
            return raw

        if kind == "iso_compact":
            year, month, day = int(raw[:4]), int(raw[4:6]), int(raw[6:8])
            if _validate(year=year, month=month, day=day):
                return MetaRelative(year=year, month=month, day=day, span=span)
            return raw

        if kind == "dd_mm_yyyy":
            parts = re.split(r"[/ ]", raw)
            day, month, year = int(parts[0]), int(parts[1]), int(parts[2])
            if not self.DD_LEFT_FIRST:
                month, day = day, month
            if _validate(year=year, month=month, day=day):
                return MetaRelative(year=year, month=month, day=day, span=span)
            # try swapped
            day, month = month, day
            if _validate(year=year, month=month, day=day):
                return MetaRelative(year=year, month=month, day=day, span=span)
            return raw

        if kind == "year4":
            year = int(raw)
            if _validate(year=year):
                return MetaRelative(year=year, span=span)
            return raw

        if kind == "ordinal_num":
            num = int(re.sub(r"[a-z]+", "", low))
            return MetaOrdinal(num, span=span)

        if kind == "number":
            return MetaOrdinal(raw.replace(",", "."), span=span)

        if kind == "word":
            return self._classify_word(low, span, text, raw=raw)

        return raw

    def _classify_word(self, low, span, text, raw=None):
        """Classify a single word token via dict lookups."""
        # NOW (only standalone)
        if low in self.NOW:
            if len(text) > 5:
                return None
            return MetaRelative(days=0, hours=0, minutes=0, seconds=0, span=span)

        # Today/tomorrow/yesterday (single-word entries)
        if low in self.TODAY_TOMORROW:
            return MetaRelative(days=self.TODAY_TOMORROW[low], span=span)

        # Seasons
        if low in self.SEASONS:
            return MetaRelative(
                month=self.SEASONS[low], day=21, levels={Units.SEASON}, span=span
            )

        # Single-word quarters (Q1, Q2, etc)
        if low in self.QUARTERS:
            return MetaRelative(
                month=self.QUARTERS[low], day=1, levels={Units.QUARTER}, span=span
            )

        # Noon/midnight
        if low in self.NOON_STANDALONE:
            hour = self.NOON_STANDALONE[low][0]
            return MetaRelative(hour=hour, levels={Units.HOUR}, span=span)

        # Check for "month day" or "month dayth" by peeking ahead
        month_day = self._try_month_day(low, span, text)
        if month_day is not None:
            return month_day

        # Months (standalone)
        if low in self.MONTHS:
            return MetaRelative(month=self.MONTHS[low], span=span)
        if low in self.MONTHS_SHORTS:
            return MetaRelative(month=self.MONTHS_SHORTS[low], span=span)

        # Weekdays
        if low in self.WEEKDAY:
            return MetaRelative(weekday=self.WEEKDAY[low], span=span)
        if low in self.WEEKDAY_SHORTS:
            return MetaRelative(weekday=self.WEEKDAY_SHORTS[low], span=span)

        # Modifiers
        if low in self.MODIFIERS:
            return MetaModifier(low, self.MODIFIERS[low], span=span)

        # Units
        if low in self.UNITS:
            return MetaUnit(self.UNITS[low], span=span)

        # Ordinal words ("three", "five", etc.)
        if low in self.ORDINAL_NUMBERS:
            # Peek ahead: if followed by am/pm/afternoon/morning → time expression
            after = text[span[1]:].lstrip()
            apm_match = re.match(
                r"(?:a\.?m\.?|p\.?m\.?|in the afternoon|in the morning)\b",
                after,
                re.IGNORECASE,
            )
            if apm_match:
                apm_text = low + " " + after[: apm_match.end()]
                full_span = (span[0], span[1] + (len(after) - len(after.lstrip())) + apm_match.end())
                hour, minute, second, _ = strip_pm(
                    apm_text, numbers_dict=self.ORDINAL_NUMBERS
                )
                return MetaRelative(
                    hour=hour,
                    minute=minute,
                    second=second,
                    levels={Units.MINUTE, Units.HOUR},
                    span=full_span,
                )
            return MetaOrdinal(self.ORDINAL_NUMBERS[low], span=span)

        # AND words
        if low in self.AND:
            return MetaAnd(low, span=span)

        # Whitelist — consume silently
        if low in self.WHITELIST:
            return None

        # Unknown word — return as text to act as separator
        return raw or low

    def _try_number_month(self, kind, raw, span, text, pos):
        """Handle 'DD month' ('18 Feb') and 'DDth of month' ('5th of June')."""
        after = text[pos:]
        # "18 Feb", "18 February", "18-Feb"
        # "5th of June", "5th June"
        m = re.match(
            r"[\s\-]*(?:of\s+)?([a-zA-Z]+)",
            after,
            re.IGNORECASE,
        )
        if not m:
            return None
        word = m.group(1).lower()
        month_val = self.MONTHS.get(word) or self.MONTHS_SHORTS.get(word[:3])
        if month_val is None:
            return None
        day = int(re.sub(r"[a-z]+", "", raw.lower()))
        if not _validate(day=day):
            return None
        full_span = (span[0], pos + m.end())
        return MetaRelative(month=month_val, day=day, span=full_span), pos + m.end()

    def _try_month_day(self, low, span, text):
        """Peek ahead to match 'June 16', 'Jun 16th', 'Jun-16', '16 June', etc."""
        after = text[span[1]:]

        # Forward: "June 16" / "June 16th" / "Jun.16" / "Jun-16"
        if low in self.MONTHS or low in self.MONTHS_SHORTS:
            month_val = self.MONTHS.get(low) or self.MONTHS_SHORTS.get(low)
            m = re.match(r"[.\s-]?(\d{1,2})(?:\s?(?:st|nd|rd|th))?\b", after)
            if m:
                day = int(m.group(1))
                if _validate(day=day):
                    full_span = (span[0], span[1] + m.end())
                    return MetaRelative(month=month_val, day=day, span=full_span)
            return None  # Don't return — let caller handle standalone month

        # Backward: "16 June", "16th June", "16th of June"
        # This is handled by looking at the preceding token in scan results;
        # for simplicity, we handle "DD month" patterns in the number+word flow.
        return None

    def _parse_time(self, raw, span):
        hour, minute, second, microsecond = strip_pm(raw)
        if "." in raw and microsecond:
            return MetaRelative(
                hour=hour,
                minute=minute,
                second=second,
                microsecond=microsecond,
                span=span,
            )
        if second:
            return MetaRelative(hour=hour, minute=minute, second=second, span=span)
        if minute:
            return MetaRelative(hour=hour, minute=minute, span=span)
        return MetaRelative(hour=hour, span=span)
