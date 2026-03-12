"""CScanner — C-accelerated drop-in replacement for NewScanner."""

from metadate._cscanner import Scanner as _CScanner
from metadate.classes import (
    MetaAnd,
    MetaModifier,
    MetaOrdinal,
    MetaRange,
    MetaRelative,
    MetaUnit,
)
from metadate.utils import Units


class CScanner:
    """C-accelerated scanner — same interface as Scanner/NewScanner."""

    def __init__(self, locale, re_flags=None):
        locale_data = {
            "months": {k.lower(): v for k, v in locale.MONTHS.items()},
            "months_shorts": {k.lower(): v for k, v in locale.MONTHS_SHORTS.items()},
            "weekday": {k.lower(): v for k, v in locale.WEEKDAY.items()},
            "weekday_shorts": {k.lower(): v for k, v in locale.WEEKDAY_SHORTS.items()},
            "modifiers": {k.lower(): v for k, v in locale.MODIFIERS.items()},
            "units": {k.lower(): v for k, v in locale.UNITS.items()},
            "ordinal_numbers": {k.lower(): v for k, v in locale.ORDINAL_NUMBERS.items()},
            "today_tomorrow": {k.lower(): v for k, v in locale.TODAY_TOMORROW.items()},
            "seasons": {k.lower(): v for k, v in locale.SEASONS.items()},
            "quarters": {k.lower(): v for k, v in locale.QUARTERS.items()},
            "now": [x.lower() for x in locale.NOW],
            "noon": {k.lower(): v for k, v in locale.NOON.items()},
            "whitelist": [x.lower() for x in locale.WHITELIST],
            "and": [x.lower() for x in locale.AND],
            "in_the": sorted(
                [x.lower() for x in locale.IN_THE], key=len, reverse=True
            ),
            "dd_left_first": locale.DD_LEFT_FIRST,
        }
        locale_data["noon_standalone"] = {
            k: v for k, v in locale_data["noon"].items() if v[0] == v[1]
        }
        locale_data["today_multi"] = sorted(
            [k for k in locale_data["today_tomorrow"] if " " in k],
            key=len,
            reverse=True,
        )
        locale_data["quarter_multi"] = sorted(
            [k for k in locale_data["quarters"] if " " in k],
            key=len,
            reverse=True,
        )
        locale_data["ordinal_multi"] = sorted(
            [k for k in locale_data["ordinal_numbers"] if " " in k],
            key=len,
            reverse=True,
        )
        self._scanner = _CScanner(locale_data)

    def scan(self, text):
        raw = self._scanner.scan(text)
        results = []
        for item in raw:
            tag = item[0]
            if tag == "R":
                _, start, end, kwargs = item
                kwargs = dict(kwargs)  # copy since we mutate
                levels_raw = kwargs.pop("_levels", None)
                modifier = kwargs.pop("_modifier", False)
                levels = {Units(lv) for lv in levels_raw} if levels_raw else None
                results.append(
                    MetaRelative(
                        span=(start, end),
                        levels=levels,
                        modifier=modifier,
                        **kwargs,
                    )
                )
            elif tag == "O":
                results.append(MetaOrdinal(item[3], span=(item[1], item[2])))
            elif tag == "U":
                results.append(MetaUnit(item[3], span=(item[1], item[2])))
            elif tag == "M":
                results.append(
                    MetaModifier(item[3], item[4], span=(item[1], item[2]))
                )
            elif tag == "G":
                results.append(MetaRange(item[3], span=(item[1], item[2])))
            elif tag == "A":
                results.append(MetaAnd(item[3], span=(item[1], item[2])))
            elif tag == "S":
                results.append(item[1])
            elif tag == "X":
                results.append("SENT")
        return results, []
