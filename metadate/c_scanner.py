"""CScanner — C-accelerated date scanner."""

from metadate._cscanner import Scanner as _CScanner


class CScanner:
    """C-accelerated scanner — same interface as Scanner."""

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
        locale_data["whitelist_multi"] = sorted(
            [x for x in locale_data["whitelist"] if " " in x],
            key=len,
            reverse=True,
        )
        self._scanner = _CScanner(locale_data)

    def scan(self, text):
        return self._scanner.scan(text), []
