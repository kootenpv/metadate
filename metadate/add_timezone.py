import re
import collections
import datetime as DT
from dateutil.tz import tzoffset, tzutc
import pytz
from metadate.utils import Units

abbrevs = {"EDT"}

for name in pytz.all_timezones:
    tzone = pytz.timezone(name)
    for utcoffset, dstoffset, tzabbrev in getattr(
        tzone, '_transition_info', [[None, None, DT.datetime.now(tzone).tzname()]]
    ):
        abbrevs.add(name)

TIMEZONE_3_RE = re.compile("|".join(sorted([x for x in abbrevs if x == x.upper()])))


def timezone_1(mp):
    beg = mp.spans[0][0]
    end = mp.spans[-1][1]
    match = re.search("([+-])(0[0-9]|1[012])([0-9][0-9])", mp.text[beg : end + 10])
    if match is not None:
        direction, hour, minute = match.groups()
        offset = int(hour) * 3600 + int(minute) * 60
        direction = 1 if direction == "+" else -1
        offset *= direction
        if offset == 0:
            tz = tzutc()
        else:
            tz = tzoffset(None, offset)
        span = match.span()
        return span, tz
    return None, None


def timezone_2(mp):
    beg = mp.spans[0][0]
    end = mp.spans[-1][1]
    match = re.search(r"(GMT|UTC)([+-])([0-9]|1[012])\b", mp.text[beg : end + 10])
    if match is not None:
        _, direction, hour = match.groups()
        offset = int(hour) * 3600
        direction = 1 if direction == "+" else -1
        offset *= direction
        if offset == 0:
            tz = tzutc()
        else:
            tz = tzoffset(None, offset)
        span = match.span()
        return span, tz
    return None, None


def timezone_3(mp):
    beg = mp.spans[0][0]
    end = mp.spans[-1][1]
    match = TIMEZONE_3_RE.search(mp.text[beg : end + 10])
    if match is not None:
        name = match.group().replace("EDT", "EST")
        tz = pytz.timezone(name)
        span = match.span()
        return span, tz
    return None, None


def timezone_4(mp):
    beg = mp.spans[0][0]
    end = mp.spans[-1][1]
    match = re.search(r"\bBST\b", mp.text[beg : end + 7])
    if match is not None:
        name = match.group()
        tz = tzoffset(None, 3600)
        span = match.span()
        return span, tz
    return None, None


def add_timezone(mp):
    if mp.min_level > Units.HOUR:
        return mp
    beg = mp.spans[0][0]
    span, tz = timezone_1(mp)
    if span is None:
        span, tz = timezone_2(mp)
    if span is None:
        span, tz = timezone_3(mp)
    if span is None:
        span, tz = timezone_4(mp)
    if span is None:
        return mp
    mp.start_date = mp.start_date.replace(tzinfo=tz)
    mp.end_date = mp.end_date.replace(tzinfo=tz)
    mp.matches.append(mp.text[span[0] + beg : span[1] + beg])
    mp.spans.append((span[0] + beg, span[1] + beg))
    mp.spans = sorted(mp.spans)
    return mp
