import re
from itertools import product
from datetime import datetime

from metadate.classes import MetaRelative
from metadate.classes import MetaOrdinal
from metadate.classes import MetaUnit
from metadate.classes import MetaRange
from metadate.classes import MetaBetween
from metadate.classes import MetaEvery
from metadate.classes import MetaAnd
from metadate.classes import MetaModifier
from metadate.classes import MetaDuration
from metadate.utils import strip_pm
from metadate.utils import Units

_RFC2822_MON = {
    'jan': 1, 'feb': 2, 'mar': 3, 'apr': 4, 'may': 5, 'jun': 6,
    'jul': 7, 'aug': 8, 'sep': 9, 'oct': 10, 'nov': 11, 'dec': 12,
}

RYEAR = ["[12][0-9]{3}"]
RMONTH = ["0?[1-9]", "1[0-2]"]
RDAY = ['[12][0-9]', '[3][01]', '0[1-9]', '[0-9]']
SEP = ['[ -/]']
RHOUR = ['[01][0-9]', '2[0-3]', '[0-9]']
RMINUTE = ['[0-5][0-9]']
RSECOND = ['[0-5][0-9]']
END = [r'\b']
APM = [r" ?a\.?m\.?", r" ?p\.?m\.?", r" afternoon", r".?o'?clock", "'"]


def pipe(ls, pre=r'\b', post=r''):
    # this is flipped
    p = post + "|" + pre
    return pre + p.join(sorted(ls, key=lambda x: len(x), reverse=True)) + post


def scan_product(*args):
    return '|'.join([''.join(x) for x in product(*args)])


def validate(second=None, minute=None, hour=None, day=None, month=None, year=None):
    if second is not None and second not in range(0, 60):
        return False
    if minute is not None and minute not in range(0, 60):
        return False
    if hour is not None and hour not in range(0, 24):
        return False
    if day is not None and day not in range(1, 32):
        return False
    if month is not None and month not in range(1, 13):
        return False
    if year is not None and year not in range(1900, 2100):
        return False
    return True


def day_month_year_switch_validate(day, month, year, dd_left_first):
    if not dd_left_first:
        day, month = month, day
    if validate(day=day, month=month, year=year):
        return True, day, month
    day, month = month, day
    if validate(day=day, month=month, year=year):
        return True, day, month
    return False, None, None


class Scanner():

    def __init__(self, locale, re_flags=re.IGNORECASE | re.MULTILINE):
        self.__dict__.update({k: v for k, v in locale.__dict__.items() if not k.startswith("__")})

        self.blacklist = []
        for k, v in locale.__dict__.items():
            if isinstance(v, (dict, list)) and k == k.upper():
                self.blacklist.extend(list(v))

        self.HH_MM_SS = scan_product(END, RHOUR, ["[:h]"], RMINUTE, ["[:m]"],
                                     RSECOND, APM + ["m?"], END)
        # probably not so needed
        self.HH_MM_SS_m = scan_product(END, RHOUR, ["[:h]"], RMINUTE, ["[:m]"],
                                       RSECOND, [r'\.\d+'], END)

        self.HH_MM = scan_product(END, RHOUR, ["[:h']"], RMINUTE, APM + ["[hm]?"], END)
        #self.HH_MM2 = scan_product(END, RHOUR, ["."], RMINUTE, APM, END)
        self.HHAPM = scan_product(END, RHOUR, APM, END)
        self.YYYY_MM_DD = scan_product(END, RYEAR, SEP, RMONTH, SEP, RDAY, END)
        self.YYYYMMDD = scan_product(END, RYEAR, RMONTH, RDAY, END)
        self.DD_MM_YYYY = scan_product(END, RDAY, SEP, RMONTH, SEP, RYEAR, END)
        self.AMBIGUOUS_DD_DD_YYYY = scan_product(
            END, RDAY, ["[./ -]"], RDAY, ["[./ -]"], RYEAR + ["[01289][0-9]"], END)
        self.DDMMYYYY = scan_product(END, RDAY, RMONTH, RYEAR, END)
        self.YYYY = scan_product([r'(?<![$€£¥#])'], END, RYEAR, END)
        self.AMBIGUOUS_DD_DD = scan_product(
            ["on ", "the ", "for ", "from ", "until "], RDAY, ["[./ -]"], RDAY, END, ["(?![./0-9-][./0-9-])"])
        self.DDMM = scan_product(END, [r"\d{1,2}"], [" ?..", ""], [
                                 " of ", " ", "-", ""], self.MONTHS)
        self.DDMMS = scan_product(END, [r"\d{1,2}"], [" ?[snrt][tdh]", ""], [
            " of ", " ", "-", ""], self.MONTHS_SHORTS)
        self.LETTER_MONTH_DAY = scan_product(END, self.MONTHS, [" [0-3]?[0-9]"],
                                             [" ?"], self.RANK_NAMING + [""], END)
        self.SHORT_DAY_MONTH = scan_product(END, self.MONTHS_SHORTS, ["[.]?"], ["[ -][0-3]?[0-9]"],
                                            [" ?"], self.RANK_NAMING + [""], END)
        self.ORDINAL_APM = scan_product(END, self.ORDINAL_NUMBERS,
                                        [r" a\.?m", r" p\.?m", " in the afternoon", " in the morning"])

        self.ORDINAL = scan_product(END, self.ORDINAL_NUMBERS, [" "])
        self.ON_THE_DAY = scan_product(END, self.ON_THE, RDAY, [" ?"], self.RANK_NAMING, END)
        self.AT_HOUR = scan_product(END, self.AT, [" "], RHOUR, ["h?"], ["(?![:']|[.] )"], END)

        # IN_THE = ['in the', 'over the', 'during the']
        # LAST_FIRST = {
        #     'last': -1,
        #     'first': 1
        # }

        # MODIFIERS = {
        #     "in": 1,
        #     "on": 0,
        #     "this": 0,
        #     "next":  1,
        #     "coming": 1
        # }

        # RFC 2822: "Sat, 15 Jun 2024 12:00:00 +0000"
        # One regex to match the shape; callback uses dict lookups, no further regex.
        self.RFC2822 = (
            r'(?:Mon|Tue|Wed|Thu|Fri|Sat|Sun), '
            r'[0-3]?[0-9] '
            r'(?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) '
            r'[12][0-9]{3}'
            r'(?: [0-2][0-9]:[0-5][0-9](?::[0-5][0-9])?)?'
            r'(?: [+-][0-9]{4})?'
        )

        self.scanner = re.Scanner([
            (self.RFC2822, self.rfc2822),
            (self.HH_MM_SS_m, self.hh_mm_ss),
            (self.HH_MM_SS, self.hh_mm_ss),
            (self.HH_MM, self.hh_mm),
            #(self.HH_MM2, self.hh_mm),
            (self.HHAPM, self.hh_mm),
            (self.YYYY_MM_DD, self.yyyy_mm_dd),
            (self.YYYYMMDD, self.yyyymmdd),
            # these ambigious have a bad performance
            #(self.AMBIGUOUS_DD_DD_YYYY, self.ambiguous_dd_dd_yyyy),
            #(self.AMBIGUOUS_DD_DD, self.ambiguous_dd_dd),
            (self.DD_MM_YYYY, self.dd_mm_yyyy),
            #(self.DDMMYYYY, self.ddmmyyyy),
            (self.YYYY, self.yyyy),                      # YYYY
            (self.ORDINAL_APM, self.ordinal_apm),
            (self.ORDINAL, self.ordinal),
            (pipe(self.NOW, post=r"\b"), self.now),
            #(pipe(self.BETWEEN, post=r'\b'), lambda y, x: MetaBetween(None, None, None, span=y.match.span())),
            #(pipe(self.EVERY, post=r'\b'), self.every),
            (pipe(self.IN_THE, post=" "), self.in_the),
            #(pipe(self.DURATION, post=" "), self.duration),
            (pipe(self.TODAY_TOMORROW), self.today_tomorrow),
            (pipe(self.SEASONS, post=r'\b'), self.season),
            (pipe(self.QUARTERS, post=r'\b'), self.quarter),
            (pipe(self.MODIFIERS, post=r'\b'), self.modifier),
            (self.LETTER_MONTH_DAY, self.letter_month_day),
            (self.SHORT_DAY_MONTH, self.short_day_month),
            (self.DDMM, self.ddmm),
            (self.DDMMS, self.ddmms),
            (pipe(self.MONTHS, post=r'\b(?! i)'), self.letter_month),
            (pipe(self.MONTHS_SHORTS, post=r'[.]?(?![a-zA-Z])'), self.short_month),
            (pipe(self.WEEKDAY, post=r'\b'), self.weekday),
            (pipe(self.WEEKDAY_SHORTS, post=r'\b'), self.weekday_shorts),
            (self.ON_THE_DAY, self.on_the_day),
            (self.AT_HOUR, self.at_hour),
            ("and a half", lambda y, x: MetaOrdinal("0.5", span=y.match.span())),
            # (self.ON_THE_DAY, self.on_the_day_ordinal),
            (r'\bfortnights?\b', lambda y, x: MetaUnit("WEEK", span=y.match.span(), modifier=2)),
            (r'\bdecades?\b', lambda y, x: MetaUnit("YEAR", span=y.match.span(), modifier=10)),
            (pipe(self.UNITS, r'\b', r'\b'), lambda y, x: MetaUnit(
                self.UNITS[x.lower()], span=y.match.span())),
            (r"\d+[.,]?[0-9]?", lambda y, x: MetaOrdinal(x.replace(",", "."), span=y.match.span())),

            # temps
            ("[.][.]+", "ellipsis"),
            (r"\b[a-zA-Z]{1,3}[.](?![a-zA-Z0-9])(?! ?\d)[^.]", "abbrev"),
            ("[!?]+(?![a-zA-Z0-9]) ?", "SENT"),
            ("[.](?![a-zA-Z0-9.]) ?", "SENT"),
            ("\n", "SENT"),
            # tricky stuff
            (pipe(self.AND), lambda y, x: MetaAnd(x, span=y.match.span())),
            (pipe([k for k, v in self.NOON.items() if v[0] == v[1]], post=r'\b'), self.noon_midnight),
            (pipe(self.blacklist), None),
            (r' +', None),
            (r'-', None),  # can be "5-6 days" and "2009-2010"
            (r'.', lambda y, x: x)
        ], re_flags)

    def scan(self, text):
        text = text.replace("–", "-")
        return self.scanner.scan(text)

    @staticmethod
    def rfc2822(y, x):
        # "Sat, 15 Jun 2024 12:00:00 +0000" — all parsing via split + dict lookup
        parts = x.split()
        # parts: ['Sat,', '15', 'Jun', '2024', '12:00:00', '+0000']
        day = int(parts[1])
        month = _RFC2822_MON[parts[2][:3].lower()]
        year = int(parts[3])
        levels = {Units.YEAR, Units.MONTH, Units.DAY}
        hour, minute, second = 0, 0, 0
        if len(parts) >= 5 and ':' in parts[4]:
            time_parts = parts[4].split(':')
            hour = int(time_parts[0])
            levels.add(Units.HOUR)
            if len(time_parts) >= 2:
                minute = int(time_parts[1])
                levels.add(Units.MINUTE)
            if len(time_parts) >= 3:
                second = int(time_parts[2])
                levels.add(Units.SECOND)
        return MetaRelative(
            year=year, month=month, day=day,
            hour=hour, minute=minute, second=second,
            levels=levels, span=y.match.span(),
        )

    def season(self, y, x):
        return MetaRelative(month=self.SEASONS[x.lower()], day=21, levels={Units.SEASON},
                            span=y.match.span())

    def quarter(self, y, x):
        return MetaRelative(month=self.QUARTERS[x.lower()], day=1, levels={Units.QUARTER},
                            span=y.match.span())

    def modifier(self, y, x):
        x = x.lower()
        return MetaModifier(x, self.MODIFIERS[x], span=y.match.span())

    def in_the(self, y, x):
        s, e = y.match.span()
        e -= 1
        return MetaRange(x.rstrip(" "), span=(s, e))

    @staticmethod
    def duration(y, x):
        s, e = y.match.span()
        e -= 1
        return MetaDuration(None, None, span=(s, e))

    @staticmethod
    def every(y, x):
        s, e = y.match.span()
        e -= 1
        return MetaEvery(None, None, None, span=(s, e))

    def letter_month_day(self, y, x):
        # June 16
        month, day = re.sub("[a-zA-Z]+$", "", x).lower().split()
        return MetaRelative(month=self.MONTHS[month], day=int(day), span=y.match.span())

    def letter_day_month(self, y, x):
        # 16 June
        day, month = x.lower().split()
        return MetaRelative(month=self.MONTHS[month], day=int(day), span=y.match.span())

    def short_month_day(self, y, x):
        month, day = re.sub("[a-zA-Z.]+$", "", x).lower().split()
        return MetaRelative(month=self.MONTHS_SHORTS[month], day=int(day), span=y.match.span())

    def short_day_month(self, y, x):
        month, day = re.split("[^a-zA-Z0-9]+", x.lower().strip())
        day = re.sub("[stndrh]", "", day)
        return MetaRelative(month=self.MONTHS_SHORTS[month[:3]], day=int(day), span=y.match.span())

    def weekday(self, y, x):
        # Tuesday
        x = x.replace("-", " ")
        weekday = self.WEEKDAY[x.lower().rstrip("s")]
        return MetaRelative(weekday=weekday, span=y.match.span())

    def weekday_shorts(self, y, x):
        # Tuesday
        x = x.replace("-", " ")
        weekday = self.WEEKDAY_SHORTS[x.lower().rstrip("s")]
        return MetaRelative(weekday=weekday, span=y.match.span())

    def ddmm(self, y, x):
        x = x.lower()
        month = re.findall("|".join(self.MONTHS), x)[0]
        day = re.sub("[^0-9]", "", x)
        return MetaRelative(month=self.MONTHS[month], day=int(day), span=y.match.span())

    def ddmms(self, y, x):
        x = x.replace("-", " ")
        # now to cover 31mar
        parts = x[:-3], x[-4:]
        day = re.sub("[^0-9]", "", parts[0])
        month = re.sub("[0-9]", "", parts[-1].lower().strip())
        return MetaRelative(month=self.MONTHS_SHORTS[month], day=int(day), span=y.match.span())

    def letter_month(self, y, x):
        return MetaRelative(month=self.MONTHS[x.lower()], span=y.match.span())

    def short_month(self, y, x):
        return MetaRelative(month=self.MONTHS_SHORTS[x[:3].lower()], span=y.match.span())

    def today_tomorrow(self, y, x):
        days = self.TODAY_TOMORROW[x.lower()]
        return MetaRelative(days=days, span=y.match.span())

    def th_of_month(self, y, x):
        # 25th of June
        x = x.replace("-", " ")
        parts = x.lower().split()
        dayth, month = parts[0][:-2], parts[-1]
        return MetaRelative(month=self.MONTHS[month], day=int(dayth), span=y.match.span())

    @staticmethod
    def now(y, x):
        if len(y.match.string) > 5:
            return None
        return MetaRelative(days=0, hours=0, minutes=0, seconds=0, span=y.match.span())

    def noon_midnight(self, y, x):
        hour = self.NOON[x.lower()][0]
        return MetaRelative(hour=hour, levels={Units.HOUR}, span=y.match.span())

    @staticmethod
    def hh_mm_ss(y, x):
        hour, minute, second, microsecond = strip_pm(x)
        if "." in x:
            md = MetaRelative(hour=hour, minute=minute, second=second,
                              microsecond=microsecond, span=y.match.span())
        else:
            md = MetaRelative(hour=hour, minute=minute, second=second,
                              span=y.match.span())
        return md

    @staticmethod
    def hh_mm(y, x):
        hour, minute, _, _ = strip_pm(x)
        if minute is None:
            return MetaRelative(hour=hour, span=y.match.span())
        return MetaRelative(hour=hour, minute=minute, span=y.match.span())

    def ordinal_apm(self, y, x):
        hour, minute, second, _ = strip_pm(x, numbers_dict=self.ORDINAL_NUMBERS)
        return MetaRelative(hour=hour, minute=minute, second=second, levels={Units.MINUTE, Units.HOUR},
                            span=y.match.span())

    def ordinal(self, y, x):
        return MetaOrdinal(self.ORDINAL_NUMBERS[x.lower().strip().replace(",", ".")], span=y.match.span())

    @staticmethod
    def yyyy_mm_dd(y, x):
        parts = re.split("[ /-]", x)
        if len(parts) != 3:
            return None
        year, month, day = parts
        return MetaRelative(year=int(year), month=int(month), day=int(day), span=y.match.span())

    @staticmethod
    def yyyymmdd(y, x):
        if len(x) != 8:
            return None
        year, month, day = int(x[:4]), int(x[4:6]), int(x[6:8])
        return MetaRelative(year=year, month=month, day=day, span=y.match.span())

    @staticmethod
    def dd_mm_yyyy(y, x):
        parts = re.split("[ /.-]", x)
        if len(parts) != 3:
            return None
        day, month, year = parts
        return MetaRelative(year=int(year), month=int(month), day=int(day), span=y.match.span())

    @staticmethod
    def ddmmyyyy(y, x):
        # TODO: 1 01 2015 / 01 1 2015, 1 and 2
        if len(x) == 6:
            return None
        else:
            year, month, day = int(x[4:8]), int(x[2:4]), int(x[:2])
        return MetaRelative(year=year, month=month, day=day, span=y.match.span())

    def ambiguous_dd_dd_yyyy(self, y, x):
        seps = re.findall("[ ./-]", x)
        num_sep = len(seps)
        num_numeric = len(x) - num_sep
        # different separators is not allowed
        # only 1 sep is also not allowed
        if len(set(seps)) > 1:
            return None
        if num_numeric == 8 and num_sep != 1:
            x = re.sub("[ ./-]", "", x)
            day, month, year = x[:2], x[2:4], x[4:8]
            day, month, year = int(day), int(month), int(year)
            valid, day, month = day_month_year_switch_validate(
                day, month, year, self.DD_LEFT_FIRST)
            if not valid:
                return None
        elif num_numeric == 7 and num_sep == 2:
            day, month, year = x.split(seps[0])
            day, month, year = int(day), int(month), int(year)
            valid, day, month = day_month_year_switch_validate(
                day, month, year, self.DD_LEFT_FIRST)
            if not valid:
                return None
        # 1-1-2016, dayfirst check
        elif num_numeric == 6 and num_sep == 2:
            sep = seps[0]
            day, month, year = x.split(sep)
            day, month, year = int(day), int(month), int(year)
            valid, day, month = day_month_year_switch_validate(
                day, month, year, self.DD_LEFT_FIRST)
            if not valid:
                return None
        # 02-2010 no dayfirst needed, has to be month
        elif num_numeric == 6 and num_sep == 1:
            sep = seps[0]
            month, year = x.split(sep)
            day, month = int(day), int(month)
            if not validate(year=year, month=month):
                return None
            else:
                return MetaRelative(year=year, month=month, span=y.match.span())
        # elif num_numeric == 5 and num_sep == 1:
        # # 1-1-16 and 7-11-17
        # dayfirst check
        elif num_numeric in (4, 5) and num_sep == 2:
            sep = seps[0]
            day, month, year = x.split(sep)
            day, month = int(day), int(month)
            year = int("20" + str(year))
            valid, day, month = day_month_year_switch_validate(
                day, month, year, self.DD_LEFT_FIRST)
            if not valid:
                return None
        else:
            raise ValueError("Cannot understand '{}'".format(x))
            # let's remind ourselves that this is not going to matter in some years
        return MetaRelative(year=year, month=month, day=day, span=y.match.span())

    @staticmethod
    def ambiguous_dd_dd(y, x):
        _, day, month = re.split("[ ./-]", x)
        day, month = int(day), int(month)
        if not validate(day=day, month=month):
            if validate(day=month, month=day):
                day, month = month, day
            else:
                return None
        return MetaRelative(month=month, day=day, span=y.match.span())

    @staticmethod
    def yyyy(y, x):
        return MetaRelative(year=int(x), span=y.match.span())

    @staticmethod
    def on_the_day(y, x):
        # "on the 31st"
        return MetaRelative(day=int(x.split()[2][:-2]), span=y.match.span())

    @staticmethod
    def at_hour(y, x):
        # "at 19"
        x = x.strip("h")
        return MetaRelative(hour=int(x.split()[1]), span=y.match.span())
