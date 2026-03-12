import math
from datetime import datetime
from metadate.utils import Units
from metadate.utils import FREQ
from metadate.utils import RD_ARG_TO_UNIT
from metadate.utils import BOUNDARIES
from dateutil.relativedelta import relativedelta


class Meta:
    def __init__(self, x, span):
        self.x = x
        self.span = span

    def __repr__(self):
        return f"{self.__class__.__name__}(x={self.x})"

    @property
    def is_relative(self):
        return isinstance(self, MetaRelative)


class MetaPeriod(Meta):
    def __init__(
        self, start_date, end_date, levels, spans, locale, text, has_modifier, reference_date
    ):
        self.start_date = start_date
        self.end_date = end_date
        self.levels = levels
        self.spans = spans
        self.locale = locale
        self.text = text
        self.matches = [text[i:j] for i, j in spans]
        self.has_modifier = has_modifier
        self.reference_date = reference_date

    def __repr__(self):
        name = self.__class__.__name__
        cases = ["start_date", "end_date", "levels", "matches", "locale"]
        n = len(name) + 1
        atts = ", ".join(
            str(getattr(self, x)) for x in cases if getattr(self, x, None) is not None
        )
        return f"{name}({atts})"

    @property
    def min_level(self):
        return min(self.levels)

    @property
    def max_level(self):
        return max(self.levels)

    @property
    def level(self):
        import warnings

        warnings.simplefilter("once", DeprecationWarning)
        warnings.warn("Please upgrade to using 'levels'", DeprecationWarning)
        return self.min_level

    def __gt__(self, other):
        return self.start_date > other.start_date or (
            self.start_date == other.start_date and self.end_date > other.end_date
        )

    def __eq__(self, other):
        return self.start_date == other.start_date and self.end_date == other.end_date

    def __lt__(self, other):
        return self.start_date < other.start_date or (
            self.start_date == other.start_date and self.end_date < other.end_date
        )

    @property
    def is_in_past(self):
        return self.start_date < self.reference_date

    @property
    def is_publish_date(self):
        return (self.has_day or self.has_modifier) and self.is_in_past

    @property
    def is_today(self):
        return self.start_date.date() == self.reference_date.date()

    @property
    def has_time(self):
        return self.has_second or self.has_minute or self.has_hour

    @property
    def has_second(self):
        return Units.SECOND in self.levels

    @property
    def has_minute(self):
        return Units.MINUTE in self.levels

    @property
    def has_hour(self):
        return Units.HOUR in self.levels

    @property
    def has_day(self):
        return Units.DAY in self.levels

    @property
    def has_month(self):
        return Units.MONTH in self.levels

    @property
    def has_year(self):
        return Units.YEAR in self.levels

    def to_dict(self):
        result = {
            "start_date": self.start_date.isoformat(),
            "end_date": self.end_date.isoformat(),
            "levels": list(self.levels),
            "spans": [{"begin": x[0], "end": x[1]} for x in self.spans],
            "matches": self.matches,
        }
        return result

    @property
    def tz(self):
        return self.start_date.tzinfo


class MetaUnit(Meta):
    def __init__(self, unit, span, modifier=1):
        self.unit = unit
        self.span = span
        self.levels = {Units[unit]}
        self.modifier = modifier

    def __repr__(self):
        cases = ["unit", "modifier"]
        atts = {x: str(getattr(self, x)) for x in cases if getattr(self, x, None) is not None}
        values = ", ".join(f"{x}={atts[x]}" for x in cases if x in atts)
        return f"{self.__class__.__name__}({values})"


class MetaOrdinal(Meta):
    def __init__(self, amount, span):
        self.amount = float(amount)
        self.span = span

    def __repr__(self):
        cases = ["amount"]
        atts = {x: str(getattr(self, x)) for x in cases if getattr(self, x, None) is not None}
        values = ", ".join(f"{x}={atts[x]}" for x in cases if x in atts)
        return f"{self.__class__.__name__}({values})"


class MetaRelative(Meta):
    def __init__(self, span, levels=None, rd=None, modifier=False, **rd_args):
        if levels is None:
            levels = set(RD_ARG_TO_UNIT[x] for x in rd_args)
        self.rd_args = rd_args
        self.levels = levels
        self.modifier = modifier
        if rd is not None:
            self.rd = rd
        else:
            for x in ["seasons", "quarters"]:
                if x in rd_args:
                    del rd_args[x]
            if isinstance(rd_args.get("days"), float) and int(rd_args["days"]) != float(
                rd_args["days"]
            ):
                frac, _ = math.modf(rd_args["days"])
                # currently only halfs are supported
                rd_args["hours"] = rd_args.get("hours", 0) + int(24 * frac)
                rd_args["days"] = int(rd_args["days"])
                self.levels.add(Units.HOUR)
            if isinstance(rd_args.get("months"), float) and int(rd_args["months"]) != float(
                rd_args["months"]
            ):
                frac, _ = math.modf(rd_args["months"])
                # currently only halfs are supported
                rd_args["days"] = rd_args.get("days", 0) + int(30 * frac)
                rd_args["months"] = int(rd_args["months"])
                self.levels.add(Units.DAY)
            if isinstance(rd_args.get("years"), float) and int(rd_args["years"]) != float(
                rd_args["years"]
            ):
                frac, _ = math.modf(rd_args["years"])
                rd_args["days"] = rd_args.get("days", 0) + int(365 * frac)
                rd_args["years"] = int(rd_args["years"])
                self.levels.add(Units.MONTH)
            self.rd = relativedelta(**rd_args)
        self.span = span

    def __add__(self, other):
        # if overlap, crash
        rd = self.rd + other.rd
        levels = self.levels.union(other.levels)
        span = sorted(self.span + other.span)
        modifier = self.modifier or getattr(other, "modifier", False)
        return MetaRelative(span, levels, modifier=modifier, rd=rd)

    def __repr__(self):
        return f"{self.__class__.__name__}(rd={self.rd}, levels={self.levels})"


class MetaModifier(Meta):
    def __init__(self, x, value, span):
        self.x = x
        self.value = value
        self.span = span

    def __repr__(self):
        return f"{self.__class__.__name__}(x={self.x}, value={self.value})"


class MetaRange(Meta):
    pass


class MetaBetween(Meta):
    def __init__(self, start, end, levels, span):
        self.start = start
        self.end = end
        self.levels = levels
        self.span = span

    def __repr__(self):
        return f"{self.__class__.__name__}(start={self.start}, end={self.end}, levels={self.levels})"


class MetaEvery(Meta):
    def __init__(self, freq, interval, levels, span):
        self.span = span
        self.freq = freq
        self.interval = interval
        self.levels = levels

    def __repr__(self):
        return f"{self.__class__.__name__}(freq={self.freq}, interval={self.interval}, levels={self.levels})"


class MetaAnd(Meta):
    pass


class MetaDuration(Meta):
    def __init__(self, rd, levels, span):
        self.span = span
        self.rd = rd
        self.levels = levels

    def __repr__(self):
        return f"{self.__class__.__name__}(rd={self.rd}, levels={self.levels})"
