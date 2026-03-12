import itertools
from datetime import datetime

from dateutil.relativedelta import relativedelta as rd
from dateutil.rrule import MONTHLY, rrule

from metadate.utils import erase_level, DAY


"""
# and and or make both go into the interval list
t = "every 2 weeks between this year and the next until December on Tuesdays and Wednesdays at ten and 11 pm for 10 minutes"
mp = MetaPeriod([rd(years=0, weekday=4, hour=10, minute=0), rd(years=0, weekday=5, hour=10, minute=0)],
                rd(years=1, month=12),
                rd(),
                rd(minute=10),
                [rd(weeks=2)])
for s, e in mp.cycle(level=9):
    print(s, e)

r = rrule(WEEKLY, interval=2, dtstart=datetime(2010, 1, 1), byhour=(22, 23), byweekday=(2, 3))
for x in r.between(datetime(2016, 1, 1), datetime(2017, 12, 1)):
    print(x, x + rd(minutes=10))

    # start/end:
#     between ... and ...
#     from ... to ...

# selector:
#     on ...
#     at ...
#     in ...

# highest level - 1
# then has to be constrained to highest_level

# MetaEvery + MetaBetween = every with start and end
# MetaEvery + MetaUntil/For = every with end
# MetaEvery + MetaFrom = every with start and end is final_date


# duration (apply at end to start and end)
#    for ...

t = "Every 3 hours from 9 AM to 5 PM"
mp = MetaPeriod(rd(hour=9),
                rd(hour=17),
                [rd()],
                [rd(hours=1)],
                [rd(hours=3)])
for s, e in mp.cycle(level=3):
    print(s, e)

r = rrule(HOURLY, interval=3, dtstart=datetime(2010, 1, 1))
for x in r.between(datetime(2016, 9, 5, 9), datetime(2016, 9, 5, 17), inc=True):
    print(x, x + rd(minutes=10))


t = "at 10 pm for 10 minutes"
mp = MetaPeriod(rd(hour=10),
                rd(hour=11),
                [rd()],
                [rd(minute=10)],
                None)
for s, e in mp.cycle(level=2):
    print(s, e)

r = rrule(HOURLY, interval=1, dtstart=datetime(2016, 9, 5, 10))
for x in r.between(datetime(2016, 9, 5, 10, 0), datetime(2016, 9, 5, 10, 1), inc=True):
    print(x, x + rd(minutes=10))

t = "on Tuesday at 3 pm for 3 hours"
mp = MetaPeriod(rd(weekday=1),
                rd(weekday=2), r=rrule(HOURLY, interval=1, count=1, dtstart=start, byweekday=(TU), byhour=(15))
                [rd(hour=3) + rd()],
                [rd(hour=3) + rd(hours=3)],
                None)
for s, e in mp.cycle(level=3):
    print(s, e)

start = datetime(2016, 9, 5, 14, 58)
end = datetime(2020, 9, 5, 14, 58)
r = rrule(HOURLY, interval=1, count=1, dtstart=start, byweekday=(TU), byhour=(15))
for x in r.between(start, end, inc=True):
    x = erase_level(x, 3)
    print(x, x + rd(hours=3))


start = datetime(2015, 9, 5, 14, 58)
end = datetime(2020, 9, 5, 14, 58)
r = rrule(MONTHLY, interval=1, dtstart=start, byweekday=(TU), byhour=(15), bysetpos=-1)
for x in r.between(start, end, inc=True):
    x = erase_level(x, 3)
    print(x, x + rd(hours=3))


start = datetime(2015, 9, 5, 14, 58)
end = datetime(2020, 9, 5, 14, 58)
r = rrule(MONTHLY, interval=1, count=1, dtstart=start, bymonthday=(16), byhour=(15), bysetpos=-1)
for x in r.between(start, end, inc=True):
    x = erase_level(x, 3)
    print(x, x + rd(hours=3))


def test_three():
    t = "The 3 of us around $100 (every 2 weeks) at (10:00) (two weeks after 25th of June) in (2018) to Brussels."
    mp = parse_date(t, verbose=VERBOSE)
    assert mp.start_date == dt(2018, 6, 25, 10, 0)
    assert mp.end_date == dt(2018, 6, 25, 10, 1)

from metadate.utils import FREQ


def level_to_duration(level):
    if level == 9:    # year
        return rd(years=1)
    elif level == 8:  # season
        return rd(months=3)
    elif level == 7:  # quarter
        return rd(months=3)
    elif level == 6:  # month
        return rd(months=1)
    elif level == 5:  # week
        return rd(weeks=1)
    elif level == 4:  # day
        return rd(days=1)
    elif level == 3:  # hour
        return rd(hours=1)
    elif level == 2:  # minute
        return rd(minutes=1)
    elif level == 1:  # second
        return rd(seconds=1)
    return rd()


def gen_date(start=None, end=None, freq=None, interval=None, count=None, duration=None, level=2, **kwargs):
    start = start or datetime.now()
    end = end or start + rd(years=3)
    duration = duration or level_to_duration(level)
    if freq is None:
        freq = "yearly"
        interval = 10000
        count = 1
    for x in rrule(freq=FREQ[freq], dtstart=start, interval=interval,
                   until=end, count=count, **kwargs):
        x = erase_level(x, level)
        yield x, x + duration


len(list(gen_date(datetime(2018, 6, 25, 10, 00), freq="weekly", level=2, interval=2)))
len(list(gen_date(datetime(2018, 6, 25, 10, 00), freq="hourly", level=2, interval=2 * 24 * 7)))

list(gen_date(datetime(2018, 6, 25, 10, 00)))
"""


# start = datetime(2017, 6, 1, 0, 0)
# end = datetime(2017, 8, 1, 0, 0)
# r = rrule(DAILY, dtstart=start, byhour=(10))
# for x in r.between(start, end, inc=True):
#     x = erase_level(x, 3)
#     print(x, x + rd(hours=1))

# start = datetime(2017, 1, 28, 0, 0)
# end = datetime(2020, 1, 28, 0, 0)
# r = rrule(MONTHLY, dtstart=start, bymonth=(6), byweekday=(WE), byhour=(11), bysetpos=2)
# for x in r.between(start, end, inc=True):
#     x = erase_level(x, 3)
#     print(x, x + rd(hours=1))


# units = ["quarter", "season", "year", "month", "week", "weekday", "day", "hour", "minute", "second"]

# start_rel =
# duration =
# end_rel =
# interval =
# count =
# freq =

# ["winter 2017", "summer 2018", "2017 days"]


# start = datetime(2017, 1, 28, 0, 0)
# end = datetime(2020, 1, 28, 0, 0)
# r = rrule(MONTHLY, interval=2, dtstart=start, byweekday=(MO), byhour=(10), bysetpos=2)
# for x in r.between(start, end, inc=True):
#     x = erase_level(x, 3)
#     print(x, x + rd(hours=1))


# start = datetime(2017, 1, 28, 0, 0)
# end = datetime(2020, 1, 28, 0, 0)
# r = rrule(MONTHLY, until=end, interval=2, dtstart=start, byweekday=(MO), byhour=(10, 11))
# for x in r:
#     x = erase_level(x, 3)
#     print(x, x + rd(hours=1))

from metadate.utils import FREQ


def level_to_duration(level):
    if level == 9:    # year
        return rd(years=1)
    elif level == 8:  # season
        return rd(months=3)
    elif level == 7:  # quarter
        return rd(months=3)
    elif level == 6:  # month
        return rd(months=1)
    elif level == 5:  # week
        return rd(weeks=1)
    elif level == 4:  # day
        return rd(days=1)
    elif level == 3:  # hour
        return rd(hours=1)
    elif level == 2:  # minute
        return rd(minutes=1)
    elif level == 1:  # second
        return rd(seconds=1)
    return rd()


def gen_date(ref=None, start=None, end=None, freq=None, interval=1, count=None, duration=None,
             level=DAY, **kwargs):
    ref = ref or datetime.now()
    start = start or rd()
    start = ref + start
    end = end or rd(years=3)
    end = start + end
    duration = duration or level_to_duration(level)
    if freq is None:
        freq = "yearly"
        interval = 10000
        count = 1
    for x in rrule(freq=FREQ[freq], dtstart=start, interval=interval,
                   until=end, count=count, **kwargs):
        x = erase_level(x, level)
        yield x, x + duration
