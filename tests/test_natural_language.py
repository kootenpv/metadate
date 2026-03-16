"""
Natural language date parsing tests.

These describe expected behavior for realistic human text inputs.
Some may currently fail — they document what metadate *should* handle.
"""

from datetime import datetime

import pytest
from metadate import parse_date

# Reference: Saturday June 15, 2024, noon
REF = datetime(2024, 6, 15, 12, 0, 0)


def dt(*args):
    return datetime(*args)


# ── Today / tomorrow / yesterday ────────────────────────────────────────

class TestTodayTomorrowYesterday:
    def test_today(self):
        r = parse_date("today", reference_date=REF)
        assert r.start_date == dt(2024, 6, 15, 0, 0)
        assert r.end_date == dt(2024, 6, 16, 0, 0)

    def test_leave_today(self):
        r = parse_date("leave today", reference_date=REF)
        assert r is not None
        assert r.start_date.date() == REF.date()

    def test_tomorrow(self):
        r = parse_date("tomorrow", reference_date=REF)
        assert r.start_date == dt(2024, 6, 16, 0, 0)

    def test_leave_tomorrow(self):
        r = parse_date("leave tomorrow", reference_date=REF)
        assert r is not None
        assert r.start_date == dt(2024, 6, 16, 0, 0)

    def test_yesterday(self):
        r = parse_date("yesterday", reference_date=REF)
        assert r.start_date == dt(2024, 6, 14, 0, 0)

    def test_day_after_tomorrow(self):
        r = parse_date("day after tomorrow", reference_date=REF)
        assert r is not None
        assert r.start_date == dt(2024, 6, 17, 0, 0)

    def test_day_before_yesterday(self):
        r = parse_date("day before yesterday", reference_date=REF)
        assert r is not None
        assert r.start_date == dt(2024, 6, 13, 0, 0)

    def test_tmrw(self):
        r = parse_date("tmrw", reference_date=REF)
        assert r is not None
        assert r.start_date == dt(2024, 6, 16, 0, 0)


# ── Weekdays ────────────────────────────────────────────────────────────

class TestWeekdays:
    def test_next_tuesday(self):
        # REF is Saturday; next Tuesday is June 18
        r = parse_date("next tuesday", reference_date=REF)
        assert r is not None
        assert r.start_date.weekday() == 1  # Tuesday

    def test_last_wednesday(self):
        r = parse_date("last wednesday", reference_date=REF)
        assert r is not None
        assert r.start_date.weekday() == 2  # Wednesday
        assert r.start_date < REF

    def test_previous_monday(self):
        r = parse_date("previous monday", reference_date=REF)
        assert r is not None
        assert r.start_date.weekday() == 0  # Monday
        assert r.start_date < REF

    def test_this_friday(self):
        r = parse_date("this friday", reference_date=REF)
        assert r is not None
        assert r.start_date.weekday() == 4  # Friday

    def test_friday_in_two_weeks(self):
        r = parse_date("friday in two weeks", reference_date=REF)
        assert r is not None
        assert r.start_date.weekday() == 4
        assert r.start_date > REF

    def test_next_tuesday_at_11pm(self):
        r = parse_date("next tuesday at 11pm", reference_date=REF)
        assert r is not None
        assert r.start_date.weekday() == 1
        assert r.start_date.hour == 23

    def test_sunday(self):
        r = parse_date("sunday", reference_date=REF)
        assert r is not None
        assert r.start_date.weekday() == 6

    def test_short_weekday_tue(self):
        r = parse_date("Tue two weeks ago", reference_date=REF)
        assert r is not None
        assert r.start_date.weekday() == 1
        assert r.start_date < REF


# ── Relative durations ──────────────────────────────────────────────────

class TestRelativeDurations:
    def test_next_week(self):
        r = parse_date("next week", reference_date=REF)
        assert r is not None
        assert r.end_date > r.start_date

    def test_next_month(self):
        r = parse_date("next month", reference_date=REF)
        assert r is not None

    def test_next_year(self):
        r = parse_date("next year", reference_date=REF)
        assert r is not None
        assert r.start_date.year >= 2025

    def test_last_year(self):
        r = parse_date("last year", reference_date=REF)
        assert r.start_date == dt(2023, 1, 1, 0, 0)
        assert r.end_date == dt(2024, 1, 1, 0, 0)

    def test_this_year(self):
        r = parse_date("this year", reference_date=REF)
        assert r is not None
        assert r.start_date.year == 2024

    def test_in_3_weeks(self):
        r = parse_date("in 3 weeks", reference_date=REF)
        assert r is not None
        assert r.start_date > REF

    def test_in_3_weeks_and_5_days(self):
        r = parse_date("in 3 weeks and 5 days", reference_date=REF)
        assert r is not None
        assert r.start_date > REF

    def test_1_week_and_one_day_ago(self):
        r = parse_date("1 week and one day ago", reference_date=REF)
        assert r is not None
        assert r.start_date < REF

    def test_a_year_earlier(self):
        r = parse_date("a year earlier", reference_date=REF)
        assert r is not None
        assert r.start_date < REF

    def test_6_days_23_hours_58_minutes(self):
        r = parse_date("6 days 23 hours 58 minutes", reference_date=REF)
        assert r is not None

    def test_two_weeks_tomorrow(self):
        r = parse_date("two weeks tomorrow", reference_date=REF)
        assert r is not None

    def test_over_the_next_five_days(self):
        r = parse_date("over the next five days", reference_date=REF)
        assert r is not None
        assert r.start_date >= dt(2024, 6, 15, 0, 0)

    def test_over_the_next_twenty_4_years(self):
        r = parse_date("over the next twenty 4 years", reference_date=REF)
        assert r is not None

    def test_within_3_weeks(self):
        r = parse_date("i will learn french within 3 weeks", reference_date=REF)
        assert r is not None


# ── Month names ─────────────────────────────────────────────────────────

class TestMonthNames:
    def test_june(self):
        r = parse_date("June", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 6

    def test_march_2023(self):
        r = parse_date("March 2023", reference_date=REF)
        assert r.start_date == dt(2023, 3, 1, 0, 0)
        assert r.end_date == dt(2023, 4, 1, 0, 0)

    def test_august_short(self):
        r = parse_date("Aug 2", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 8
        assert r.start_date.day == 2

    def test_february_short(self):
        r = parse_date("18 Feb 1997", reference_date=REF)
        assert r is not None
        assert r.start_date == dt(1997, 2, 18, 0, 0)

    def test_month_day_year_in_sentence(self):
        r = parse_date("some text (March 9, 1990) more text", reference_date=REF)
        assert r is not None
        assert r.start_date.year == 1990
        assert r.start_date.month == 3
        assert r.start_date.day == 9

    def test_month_day_ordinal_year(self):
        r = parse_date("March 9th, 1990", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 3
        assert r.start_date.day == 9
        assert r.start_date.year == 1990

    def test_last_wednesday_last_june(self):
        r = parse_date("last wednesday last June", reference_date=REF)
        assert r is not None
        assert r.start_date < REF


# ── ISO / numeric formats ──────────────────────────────────────────────

class TestNumericFormats:
    def test_yyyy_mm_dd(self):
        r = parse_date("2015-06-07", reference_date=REF)
        assert r.start_date == dt(2015, 6, 7, 0, 0)

    def test_yyyy_mm_dd_hh_mm_ss(self):
        r = parse_date("2017-06-25 00:00:00", reference_date=REF)
        assert r.start_date == dt(2017, 6, 25, 0, 0, 0)

    def test_yyyy_mm_dd_hh_mm_ss_microseconds(self):
        r = parse_date("2017-06-25 00:00:00.123123", reference_date=REF)
        assert r is not None
        assert r.start_date.year == 2017
        assert r.start_date.month == 6
        assert r.start_date.day == 25

    def test_hh_mm_ss_yyyy_mm_dd(self):
        r = parse_date("00:01:01 2017-06-25", reference_date=REF)
        assert r is not None
        assert r.start_date.year == 2017

    def test_yyyy_mm_dd_nonzero_time(self):
        r = parse_date("2017-06-25 00:01:01", reference_date=REF)
        assert r.start_date == dt(2017, 6, 25, 0, 1, 1)

    def test_year_only(self):
        r = parse_date("2023", reference_date=REF)
        assert r.start_date == dt(2023, 1, 1)
        assert r.end_date == dt(2024, 1, 1)

    def test_dd_mm_yyyy_slash(self):
        r = parse_date("25/06/2017", reference_date=REF)
        assert r is not None
        assert r.start_date.year == 2017


# ── Time expressions ────────────────────────────────────────────────────

class TestTimeExpressions:
    def test_3pm(self):
        r = parse_date("3:00pm", reference_date=REF)
        assert r.start_date.hour == 15

    def test_3_colon_00_pm(self):
        r = parse_date("3:00 pm", reference_date=REF)
        assert r.start_date.hour == 15

    def test_12_colon_00_pm(self):
        r = parse_date("12:00 pm", reference_date=REF)
        assert r.start_date.hour == 12

    def test_3_pm_bare(self):
        r = parse_date("3 pm", reference_date=REF)
        assert r.start_date.hour == 15

    def test_three_pm_word(self):
        r = parse_date("three pm", reference_date=REF)
        assert r is not None
        assert r.start_date.hour == 15

    def test_three_in_the_afternoon(self):
        r = parse_date("three in the afternoon", reference_date=REF)
        assert r is not None
        assert r.start_date.hour == 15

    def test_at_hour(self):
        r = parse_date("at 19", reference_date=REF)
        assert r is not None
        assert r.start_date.hour == 19


# ── Quarters ────────────────────────────────────────────────────────────

class TestQuarters:
    def test_q1(self):
        r = parse_date("Q1", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 1

    def test_this_quarter(self):
        r = parse_date("this quarter", reference_date=REF)
        assert r is not None

    def test_next_quarter(self):
        r = parse_date("next quarter", reference_date=REF)
        assert r is not None

    def test_last_quarter(self):
        r = parse_date("last quarter", reference_date=REF)
        assert r is not None


# ── Ranges with "in the" / "over the" / "during the" ───────────────────

class TestRangeExpressions:
    def test_in_the_next_10_minutes(self):
        r = parse_date("in the next 10 minutes i will leave to france", reference_date=REF)
        assert r is not None
        assert r.has_time

    def test_in_the_first_10_days_of_june(self):
        r = parse_date("in the first 10 days of June", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 6

    def test_in_the_last_20_days_of_february(self):
        r = parse_date("in the last 20 days of February", reference_date=REF)
        assert r is not None

    def test_in_the_first_20_days(self):
        r = parse_date("in the first 20 days", reference_date=REF)
        assert r is not None

    def test_during_the_first_five_and_a_half_days_of_june(self):
        r = parse_date("during the first five and a half days of June", reference_date=REF)
        assert r is not None

    def test_over_the_last_five_and_a_half_days_of_june(self):
        r = parse_date("over the last five and a half days of June", reference_date=REF)
        assert r is not None

    def test_during_the_last_10_days_of_march(self):
        r = parse_date("during the last 10 days of March", reference_date=REF)
        assert r is not None

    def test_during_the_first_78_days_of_the_year(self):
        r = parse_date("during the first 78 days of the year", reference_date=REF)
        assert r is not None


# ── Complex sentences ───────────────────────────────────────────────────

class TestComplexSentences:
    def test_date_embedded_in_sentence(self):
        r = parse_date(
            "The 3 of us will each buy 2 cars 2 weeks after 5th of June in 2018 at 10:05:01 for 100 dollars.",
            reference_date=REF,
        )
        assert r is not None
        assert r.start_date.year == 2018

    def test_date_embedded_without_time(self):
        r = parse_date(
            "The 3 of us will each buy 2 cars 2 weeks after 5th of June in 2018 for 100 dollars.",
            reference_date=REF,
        )
        assert r is not None
        assert r.start_date.year == 2018

    def test_multiple_dates_in_sentence(self):
        r = parse_date(
            "Three interesting dates are 18 Feb 1997, the 20th of july and 4 days from today.",
            reference_date=REF,
            multi=True,
        )
        assert isinstance(r, list)
        assert len(r) >= 1

    def test_3_people_500_dollars_june_2018(self):
        r = parse_date(
            "3 people at most 500 dollars june 2018, to a very hot destination",
            reference_date=REF,
        )
        assert r is not None
        assert r.start_date.month == 6
        assert r.start_date.year == 2018

    def test_fly_to_paris_2017(self):
        r = parse_date(
            "I want to fly to Paris for at least 500 dollars in 2017",
            reference_date=REF,
        )
        assert r is not None
        assert r.start_date.year == 2017

    def test_5_people_spain_august(self):
        r = parse_date("5 people went on holiday to Spain in August.", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 8


# ── No‑date inputs ─────────────────────────────────────────────────────

class TestNoDateInputs:
    def test_random_sentence(self):
        assert parse_date("the quick brown fox", reference_date=REF) is None

    def test_empty_string(self):
        assert parse_date("", reference_date=REF) is None

    def test_numbers_that_arent_dates(self):
        # "you are week" should not match "week" as a date
        r = parse_date("you are week", reference_date=REF)
        # If it does parse, at least it shouldn't crash
        # The key thing: no exception raised

    def test_multi_empty(self):
        assert parse_date("hello world", reference_date=REF, multi=True) == []


# ── Level / property checks ────────────────────────────────────────────

class TestLevelProperties:
    def test_year_only_has_year_not_day(self):
        r = parse_date("2023", reference_date=REF)
        assert r.has_year
        assert not r.has_day
        assert not r.has_time

    def test_month_year_has_month(self):
        r = parse_date("March 2023", reference_date=REF)
        assert r.has_month
        assert r.has_year

    def test_time_has_hour(self):
        r = parse_date("3 pm", reference_date=REF)
        assert r.has_hour

    def test_full_datetime_has_second(self):
        r = parse_date("2017-06-25 00:01:01", reference_date=REF)
        assert r.has_second

    def test_day_month_has_day(self):
        r = parse_date("June 25", reference_date=REF)
        assert r.has_day
        assert r.has_month

    def test_tomorrow_has_day(self):
        r = parse_date("tomorrow", reference_date=REF)
        assert r.has_day

    def test_iso_date_min_level(self):
        r = parse_date("2015-06-07", reference_date=REF)
        assert r.has_day
        assert r.has_month
        assert r.has_year


# ── Ordinal day expressions ─────────────────────────────────────────────

class TestOrdinalExpressions:
    def test_25th_of_june(self):
        r = parse_date("5th of June", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 6
        assert r.start_date.day == 5

    def test_on_the_31st(self):
        r = parse_date("on the 31st of July", reference_date=REF)
        assert r is not None
        assert r.start_date.day == 31

    def test_1st_of_january(self):
        r = parse_date("1st of January", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 1
        assert r.start_date.day == 1


# ── Modifier + unit combos ──────────────────────────────────────────────

class TestModifierUnit:
    def test_next_year_is_future(self):
        r = parse_date("next year", reference_date=REF)
        assert r is not None
        assert r.start_date.year >= 2025

    def test_last_month(self):
        r = parse_date("last month", reference_date=REF)
        assert r is not None
        assert r.start_date < REF

    def test_this_week(self):
        r = parse_date("this week", reference_date=REF)
        assert r is not None

    def test_coming_monday(self):
        r = parse_date("coming monday", reference_date=REF)
        assert r is not None
        assert r.start_date.weekday() == 0

    def test_3_months_ago(self):
        r = parse_date("3 months ago", reference_date=REF)
        assert r is not None
        assert r.start_date < REF

    def test_2_days_ago(self):
        r = parse_date("2 days ago", reference_date=REF)
        assert r is not None
        assert r.start_date == dt(2024, 6, 13, 0, 0)

    def test_in_5_hours(self):
        r = parse_date("in 5 hours", reference_date=REF)
        assert r is not None
        assert r.start_date > REF

    def test_in_30_minutes(self):
        r = parse_date("in 30 minutes", reference_date=REF)
        assert r is not None
        assert r.start_date > REF


# ── Multi‑parse ─────────────────────────────────────────────────────────

class TestMultiParse:
    def test_multi_returns_list(self):
        r = parse_date("tomorrow", reference_date=REF, multi=True)
        assert isinstance(r, list)
        assert len(r) >= 1

    def test_multi_finds_multiple_dates(self):
        r = parse_date(
            "Three interesting dates are 18 Feb 1997, the 20th of july and 4 days from today.",
            reference_date=REF,
            multi=True,
        )
        assert isinstance(r, list)
        assert len(r) >= 2

    def test_multi_no_dates(self):
        r = parse_date("just some words", reference_date=REF, multi=True)
        assert r == []


# ── Edge cases / robustness ─────────────────────────────────────────────

class TestEdgeCases:
    def test_start_before_end(self):
        """Every parsed result should have start <= end."""
        texts = [
            "tomorrow",
            "last year",
            "March 2023",
            "2015-06-07",
            "3 pm",
            "next week",
            "2 days ago",
        ]
        for text in texts:
            r = parse_date(text, reference_date=REF)
            assert r is not None, f"Expected result for '{text}'"
            assert r.start_date <= r.end_date, f"start > end for '{text}'"

    def test_is_in_past(self):
        r = parse_date("yesterday", reference_date=REF)
        assert r.is_in_past

    def test_is_today(self):
        r = parse_date("today", reference_date=REF)
        assert r.is_today

    def test_to_dict(self):
        r = parse_date("tomorrow", reference_date=REF)
        d = r.to_dict()
        assert "start_date" in d
        assert "end_date" in d
        assert "levels" in d
        assert "spans" in d
        assert "matches" in d


# ── Noon / midnight / time‑of‑day keywords ────────────────────────────

class TestTimeOfDayKeywords:
    def test_noon(self):
        r = parse_date("noon", reference_date=REF)
        assert r is not None
        assert r.start_date.hour == 12

    def test_midnight(self):
        r = parse_date("midnight", reference_date=REF)
        assert r is not None
        assert r.start_date.hour == 0

    def test_tomorrow_at_noon(self):
        r = parse_date("tomorrow at noon", reference_date=REF)
        assert r is not None
        assert r.start_date.day == 16
        assert r.start_date.hour == 12

    def test_tomorrow_morning(self):
        r = parse_date("tomorrow morning", reference_date=REF)
        assert r is not None
        assert r.start_date.day == 16

    def test_yesterday_night(self):
        r = parse_date("yesterday night", reference_date=REF)
        assert r is not None
        assert r.start_date.day == 14


# ── "now" keyword ──────────────────────────────────────────────────────

class TestNow:
    def test_now(self):
        r = parse_date("now", reference_date=REF)
        assert r is not None
        assert r.start_date.date() == REF.date()

    def test_right_now(self):
        r = parse_date("right now", reference_date=REF)
        # may or may not parse — but shouldn't crash
        if r is not None:
            assert r.start_date.date() == REF.date()


# ── Compound date + time expressions ──────────────────────────────────

class TestCompoundDateTime:
    def test_tomorrow_at_3pm(self):
        r = parse_date("tomorrow at 3pm", reference_date=REF)
        assert r is not None
        assert r.start_date == dt(2024, 6, 16, 15, 0)

    def test_next_friday_at_10_30(self):
        r = parse_date("next friday at 10:30", reference_date=REF)
        assert r is not None
        assert r.start_date.weekday() == 4
        assert r.start_date.hour == 10
        assert r.start_date.minute == 30

    def test_june_25_at_2pm(self):
        r = parse_date("June 25 at 2pm", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 6
        assert r.start_date.day == 25
        assert r.start_date.hour == 14

    def test_iso_date_at_time(self):
        r = parse_date("I will arrive on 2018-06-05 at 10:30", reference_date=REF)
        assert r is not None
        assert r.start_date == dt(2018, 6, 5, 10, 30)

    def test_march_9th_1990_at_8am(self):
        r = parse_date("March 9th, 1990 at 8am", reference_date=REF)
        assert r is not None
        assert r.start_date.year == 1990
        assert r.start_date.month == 3
        assert r.start_date.day == 9
        assert r.start_date.hour == 8


# ── Written‑out number durations ──────────────────────────────────────

class TestWrittenNumbers:
    def test_three_weeks_ago(self):
        r = parse_date("three weeks ago", reference_date=REF)
        assert r is not None
        assert r.start_date < REF

    def test_five_days_ago(self):
        r = parse_date("five days ago", reference_date=REF)
        assert r is not None
        assert r.start_date == dt(2024, 6, 10, 0, 0)

    def test_in_two_months(self):
        r = parse_date("in two months", reference_date=REF)
        assert r is not None
        assert r.start_date > REF

    def test_ten_minutes_ago(self):
        r = parse_date("ten minutes ago", reference_date=REF)
        assert r is not None
        assert r.start_date < REF

    def test_a_few_days_ago(self):
        r = parse_date("a few days ago", reference_date=REF)
        assert r is not None
        assert r.start_date < REF

    def test_a_couple_weeks_ago(self):
        r = parse_date("a couple weeks ago", reference_date=REF)
        assert r is not None
        assert r.start_date < REF

    def test_fourteen_hours(self):
        r = parse_date("fourteen hours ago", reference_date=REF)
        assert r is not None
        assert r.start_date < REF

    def test_twenty_days_from_now(self):
        r = parse_date("in twenty days", reference_date=REF)
        assert r is not None
        assert r.start_date > REF


# ── Informal abbreviations ────────────────────────────────────────────

class TestInformalAbbreviations:
    def test_5_mins_ago(self):
        r = parse_date("5 mins ago", reference_date=REF)
        assert r is not None
        assert r.start_date < REF

    def test_2_hrs_ago(self):
        r = parse_date("2 hrs ago", reference_date=REF)
        assert r is not None
        assert r.start_date < REF

    def test_3_wks(self):
        r = parse_date("3 wks ago", reference_date=REF)
        assert r is not None
        assert r.start_date < REF

    def test_2_yrs_ago(self):
        r = parse_date("2 yrs ago", reference_date=REF)
        assert r is not None
        assert r.start_date.year == 2022


# ── Past modifier variations ──────────────────────────────────────────

class TestPastModifiers:
    def test_past_week(self):
        r = parse_date("past week", reference_date=REF)
        assert r is not None
        assert r.start_date < REF

    def test_prior_month(self):
        r = parse_date("prior month", reference_date=REF)
        assert r is not None
        assert r.start_date < REF

    def test_recent_days(self):
        r = parse_date("recent days", reference_date=REF)
        assert r is not None
        assert r.start_date < REF

    def test_previous_year(self):
        r = parse_date("previous year", reference_date=REF)
        assert r is not None
        assert r.start_date.year < 2024

    def test_earlier_this_month(self):
        r = parse_date("earlier this month", reference_date=REF)
        assert r is not None
        assert r.start_date <= REF


# ── Conversational / real‑world sentences ─────────────────────────────

class TestConversationalSentences:
    def test_lets_meet_next_friday(self):
        r = parse_date("let's meet next friday", reference_date=REF)
        assert r is not None
        assert r.start_date.weekday() == 4

    def test_deadline_is_march_2025(self):
        r = parse_date("the deadline is March 2025", reference_date=REF)
        assert r is not None
        assert r.start_date.year == 2025
        assert r.start_date.month == 3

    def test_birthday_on_august_12(self):
        r = parse_date("my birthday is on August 12", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 8
        assert r.start_date.day == 12

    def test_shipped_3_days_ago(self):
        r = parse_date("the package shipped 3 days ago", reference_date=REF)
        assert r is not None
        assert r.start_date == dt(2024, 6, 12, 0, 0)

    def test_vacation_starts_july_1st(self):
        r = parse_date("vacation starts July 1st", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 7
        assert r.start_date.day == 1

    def test_project_due_in_2_weeks(self):
        r = parse_date("the project is due in 2 weeks", reference_date=REF)
        assert r is not None
        assert r.start_date > REF

    def test_started_working_here_jan_2020(self):
        r = parse_date("I started working here in Jan 2020", reference_date=REF)
        assert r is not None
        assert r.start_date.year == 2020
        assert r.start_date.month == 1

    def test_call_me_at_5pm_tomorrow(self):
        r = parse_date("call me at 5pm tomorrow", reference_date=REF)
        assert r is not None
        assert r.start_date.day == 16
        assert r.start_date.hour == 17

    def test_report_was_filed_on_2024_01_15(self):
        r = parse_date("the report was filed on 2024-01-15", reference_date=REF)
        assert r is not None
        assert r.start_date == dt(2024, 1, 15, 0, 0)

    def test_last_updated_2_months_ago(self):
        r = parse_date("last updated 2 months ago", reference_date=REF)
        assert r is not None
        assert r.start_date < REF


# ── Seasons ────────────────────────────────────────────────────────────

class TestSeasons:
    def test_summer(self):
        r = parse_date("summer", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 6

    def test_next_winter(self):
        r = parse_date("next winter", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 12

    def test_last_spring(self):
        r = parse_date("last spring", reference_date=REF)
        assert r is not None
        assert r.start_date < REF

    def test_fall(self):
        r = parse_date("fall", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 9

    def test_autumn(self):
        r = parse_date("autumn", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 9


# ── Comma‑separated date formats ──────────────────────────────────────

class TestCommaFormats:
    def test_june_15_comma_2024(self):
        r = parse_date("June 15, 2024", reference_date=REF)
        assert r is not None
        assert r.start_date == dt(2024, 6, 15, 0, 0)

    def test_december_25_comma_2023(self):
        r = parse_date("December 25, 2023", reference_date=REF)
        assert r is not None
        assert r.start_date == dt(2023, 12, 25, 0, 0)

    def test_january_1st_comma_2000(self):
        r = parse_date("January 1st, 2000", reference_date=REF)
        assert r is not None
        assert r.start_date.year == 2000
        assert r.start_date.month == 1
        assert r.start_date.day == 1


# ── Half / fractional durations ────────────────────────────────────────

class TestFractionalDurations:
    def test_one_and_a_half_hours(self):
        r = parse_date("one and a half hours", reference_date=REF)
        assert r is not None

    def test_two_and_a_half_weeks(self):
        r = parse_date("two and a half weeks", reference_date=REF)
        assert r is not None

    def test_half_an_hour_ago(self):
        r = parse_date("half an hour ago", reference_date=REF)
        # may not parse, but shouldn't crash
        if r is not None:
            assert r.start_date < REF

    def test_one_and_a_half_years_ago(self):
        r = parse_date("one and a half years ago", reference_date=REF)
        assert r is not None
        assert r.start_date < REF


# ── Multi‑date extraction (advanced) ──────────────────────────────────

class TestMultiDateAdvanced:
    def test_two_iso_dates(self):
        r = parse_date(
            "compare data from 2023-01-01 against 2024-01-01",
            reference_date=REF,
            multi=True,
        )
        assert isinstance(r, list)
        assert len(r) >= 2

    def test_mixed_relative_and_absolute(self):
        r = parse_date(
            "from March 2023 until today",
            reference_date=REF,
            multi=True,
        )
        assert isinstance(r, list)
        assert len(r) >= 1

    def test_three_weekdays(self):
        r = parse_date(
            "available Monday, Wednesday and Friday",
            reference_date=REF,
            multi=True,
        )
        assert isinstance(r, list)
        assert len(r) >= 2


# ── "from today" / "from now" expressions ─────────────────────────────

class TestFromNowExpressions:
    def test_3_days_from_today(self):
        r = parse_date("3 days from today", reference_date=REF)
        assert r is not None
        assert r.start_date >= dt(2024, 6, 15, 0, 0)

    def test_a_week_from_today(self):
        r = parse_date("a week from today", reference_date=REF)
        assert r is not None
        assert r.start_date > REF

    def test_2_months_from_now(self):
        r = parse_date("2 months from now", reference_date=REF)
        assert r is not None
        assert r.start_date > REF

    def test_in_4_days_from_now_at_10am(self):
        r = parse_date("in 4 days from now at 10am", reference_date=REF)
        assert r is not None
        assert r.start_date == dt(2024, 6, 19, 10, 0)
        assert r.has_time is True


# ── Negative tests (shouldn't match as dates) ─────────────────────────

class TestFalsePositives:
    def test_price_100_dollars(self):
        r = parse_date("it costs 100 dollars", reference_date=REF)
        assert r is None

    def test_room_number(self):
        r = parse_date("go to room 305", reference_date=REF)
        assert r is None

    def test_phone_number(self):
        r = parse_date("call 555-1234", reference_date=REF)
        # shouldn't crash; if it does parse, at least verify it returns something
        # the important thing is no exception

    def test_version_number(self):
        r = parse_date("upgrade to version 3.2.1", reference_date=REF)
        # shouldn't crash

    def test_plain_adjectives(self):
        r = parse_date("the weather is nice", reference_date=REF)
        assert r is None

    def test_just_a_number(self):
        r = parse_date("I have 42 apples", reference_date=REF)
        assert r is None

    def test_second_as_ordinal_not_time_unit(self):
        """'the second one' should not parse 'second' as a time unit."""
        r = parse_date("the second one passed all checks", reference_date=REF)
        assert r is None


# ── Combined / multi‑extract stress tests ─────────────────────────────

class TestCombinedMultiExtract:
    """Glue multiple date expressions into one string with filler text
    and verify multi=True extracts them all correctly."""

    def test_tomorrow_and_iso_date_with_filler(self):
        r = parse_date(
            "I'll leave tomorrow but the contract was signed on 2023-03-15 so keep that in mind",
            reference_date=REF,
            multi=True,
        )
        assert isinstance(r, list)
        assert len(r) >= 2
        dates = sorted(r)
        assert dates[0].start_date.year == 2023
        assert dates[-1].start_date == dt(2024, 6, 16, 0, 0)

    def test_yesterday_noon_and_next_friday(self):
        r = parse_date(
            "the outage started yesterday and we expect the fix to land next friday",
            reference_date=REF,
            multi=True,
        )
        assert isinstance(r, list)
        assert len(r) >= 2
        past = [x for x in r if x.start_date < REF]
        future = [x for x in r if x.start_date > REF]
        assert len(past) >= 1
        assert len(future) >= 1

    def test_month_year_and_relative_duration(self):
        r = parse_date(
            "sales were up in March 2023 but dropped again 3 weeks ago",
            reference_date=REF,
            multi=True,
        )
        assert isinstance(r, list)
        assert len(r) >= 2
        years = {x.start_date.year for x in r}
        assert 2023 in years

    def test_two_relative_durations_with_noise(self):
        r = parse_date(
            "the first build failed 2 days ago, we retried and the second one passed 5 hours ago",
            reference_date=REF,
            multi=True,
        )
        assert isinstance(r, list)
        assert len(r) >= 2
        for x in r:
            assert x.start_date < REF

    def test_weekday_and_month_day_connected(self):
        r = parse_date(
            "let's sync on Tuesday and then present on August 12",
            reference_date=REF,
            multi=True,
        )
        assert isinstance(r, list)
        assert len(r) >= 2
        months = {x.start_date.month for x in r}
        assert 8 in months

    def test_three_dates_dense_paragraph(self):
        r = parse_date(
            "Project started Jan 2020, milestone hit on 2022-07-01, deadline is next month",
            reference_date=REF,
            multi=True,
        )
        assert isinstance(r, list)
        assert len(r) >= 3

    def test_iso_and_written_time_glued(self):
        r = parse_date(
            "deploy at 2024-08-01 then monitor until three pm on the same day",
            reference_date=REF,
            multi=True,
        )
        assert isinstance(r, list)
        assert len(r) >= 1
        has_august = any(x.start_date.month == 8 for x in r)
        assert has_august

    def test_ago_and_future_back_to_back(self):
        r = parse_date(
            "reviewed 2 weeks ago, next review in 3 months",
            reference_date=REF,
            multi=True,
        )
        assert isinstance(r, list)
        assert len(r) >= 2
        past = [x for x in r if x.start_date < REF]
        future = [x for x in r if x.start_date > REF]
        assert len(past) >= 1
        assert len(future) >= 1

    def test_today_and_last_year_in_sentence(self):
        r = parse_date(
            "compare today's numbers against last year for the quarterly report",
            reference_date=REF,
            multi=True,
        )
        assert isinstance(r, list)
        assert len(r) >= 2

    def test_long_noisy_email_two_dates(self):
        r = parse_date(
            "Hi team, as discussed on the call with 12 stakeholders and 4 engineering leads, "
            "the rollout is planned for June 25 and the rollback window closes on July 10. "
            "Please coordinate with the 3 regional offices. Thanks!",
            reference_date=REF,
            multi=True,
        )
        assert isinstance(r, list)
        assert len(r) >= 2
        months = {x.start_date.month for x in r}
        assert 6 in months
        assert 7 in months


# ── Month abbreviation with dot ──────────────────────────────────────

class TestMonthAbbrevDot:
    def test_feb_dot_14(self):
        r = parse_date("Feb. 14", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 2
        assert r.start_date.day == 14

    def test_sept_dot_1st(self):
        r = parse_date("Sept. 1st", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 9
        assert r.start_date.day == 1

    def test_jan_dot_year(self):
        r = parse_date("Jan. 2025", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 1
        assert r.start_date.year == 2025

    def test_dec_dot_25(self):
        r = parse_date("Dec. 25", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 12
        assert r.start_date.day == 25


# ── DD/MM/YYYY format consistency ────────────────────────────────────

class TestDDMMYYYY:
    def test_01_06_2024(self):
        r = parse_date("01/06/2024", reference_date=REF)
        assert r is not None
        assert r.start_date == dt(2024, 6, 1)

    def test_25_06_2017(self):
        r = parse_date("25/06/2017", reference_date=REF)
        assert r is not None
        assert r.start_date == dt(2017, 6, 25)

    def test_15_01_2025(self):
        r = parse_date("15/01/2025", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 1
        assert r.start_date.day == 15


# ── 12am/12pm edge cases ────────────────────────────────────────────

class TestTwelveHourEdge:
    def test_12am_is_midnight(self):
        r = parse_date("12am", reference_date=REF)
        assert r is not None
        assert r.start_date.hour == 0

    def test_12pm_is_noon(self):
        r = parse_date("12pm", reference_date=REF)
        assert r is not None
        assert r.start_date.hour == 12

    def test_12_30am(self):
        r = parse_date("12:30am", reference_date=REF)
        assert r is not None
        assert r.start_date.hour == 0
        assert r.start_date.minute == 30


# ── ISO date + time (regression for C scanner) ──────────────────────

class TestISODateTime:
    def test_iso_date_time_has_time(self):
        r = parse_date("2017-06-25 14:30:00", reference_date=REF)
        assert r is not None
        assert r.start_date.hour == 14
        assert r.start_date.minute == 30

    def test_iso_date_time_has_date(self):
        r = parse_date("2017-06-25 14:30:00", reference_date=REF)
        assert r is not None
        assert r.start_date.year == 2017
        assert r.start_date.month == 6
        assert r.start_date.day == 25

    def test_iso_date_with_seconds(self):
        r = parse_date("2017-06-25 00:01:01", reference_date=REF)
        assert r is not None
        assert r.start_date == dt(2017, 6, 25, 0, 1, 1)

    def test_iso_t_separator(self):
        r = parse_date("2017-06-25T14:30:00", reference_date=REF)
        assert r is not None
        assert r.start_date == dt(2017, 6, 25, 14, 30, 0)

    def test_iso_date_only(self):
        r = parse_date("2024-01-15", reference_date=REF)
        assert r is not None
        assert r.start_date == dt(2024, 1, 15)
        assert r.end_date == dt(2024, 1, 16)


# ── Unit keyword "quarter" as time unit ──────────────────────────────

class TestQuarterAsUnit:
    def test_quarter_unit_not_year(self):
        """'next quarter' should not resolve to a year-level result."""
        r = parse_date("next quarter", reference_date=REF)
        assert r is not None
        # Should be within a year, not 'next year'
        assert r.start_date.year == 2024 or r.start_date.year == 2025
        assert r.start_date < dt(2025, 7, 1)

    def test_this_quarter(self):
        r = parse_date("this quarter", reference_date=REF)
        assert r is not None


# ── Weekday plurals ──────────────────────────────────────────────────

class TestWeekdayPlurals:
    def test_tuesdays_returns_none(self):
        """Plural weekdays are not a single date reference."""
        r = parse_date("Tuesdays", reference_date=REF)
        assert r is None

    def test_on_fridays_returns_none(self):
        r = parse_date("on Fridays", reference_date=REF)
        assert r is None


# ── Compact YYYYMMDD ─────────────────────────────────────────────────

class TestCompactDate:
    def test_yyyymmdd(self):
        r = parse_date("20240615", reference_date=REF)
        assert r is not None
        assert r.start_date == dt(2024, 6, 15)

    def test_yyyymmdd_end_of_year(self):
        r = parse_date("20241231", reference_date=REF)
        assert r is not None
        assert r.start_date == dt(2024, 12, 31)


# ── Unicode / special characters (regression: C scanner crashes) ─────

class TestUnicodeHandling:
    def test_accented_chars(self):
        r = parse_date("café opens at 3pm on Monday", reference_date=REF)
        assert r is not None
        assert r.start_date.hour == 15
        assert r.start_date.weekday() == 0  # Monday

    def test_em_dash(self):
        r = parse_date("na\u00efve approach \u2014 next Tuesday", reference_date=REF)
        assert r is not None
        assert r.start_date.weekday() == 1  # Tuesday

    def test_zero_width_spaces(self):
        """Zero-width spaces should not crash the scanner."""
        r = parse_date("meeting at 15:00 on 2024\u200b06\u200b15", reference_date=REF)
        assert r is not None

    def test_smart_apostrophe(self):
        r = parse_date("next week\u2019s meeting", reference_date=REF)
        assert r is not None
        assert r.start_date >= REF

    def test_em_dash_between_dates(self):
        r = parse_date("Q1\u2014specifically January\u2014was bad", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 1

    def test_en_dash_date_range(self):
        r = parse_date("June 1\u201315", reference_date=REF)
        assert r is not None

    def test_regular_apostrophe_contraction(self):
        r = parse_date("next week's meeting", reference_date=REF)
        assert r is not None

    def test_oclock_still_works(self):
        r = parse_date("5 o'clock", reference_date=REF)
        assert r is not None
        assert r.start_date.hour == 5


# ── Time‑of‑day keywords (this morning, tonight, etc.) ───────────────

class TestTimeOfDayThis:
    def test_this_morning(self):
        r = parse_date("this morning", reference_date=REF)
        assert r is not None
        assert r.start_date.date() == REF.date()

    def test_this_afternoon(self):
        r = parse_date("this afternoon", reference_date=REF)
        assert r is not None
        assert r.start_date.date() == REF.date()

    def test_this_evening(self):
        r = parse_date("this evening", reference_date=REF)
        assert r is not None
        assert r.start_date.date() == REF.date()

    def test_tonight(self):
        r = parse_date("tonight", reference_date=REF)
        assert r is not None
        assert r.start_date.date() == REF.date()

    def test_5_oclock_tomorrow(self):
        r = parse_date("5 o'clock tomorrow", reference_date=REF)
        assert r is not None
        assert r.start_date.day == 16
        assert r.start_date.hour == 5


# ── "end of" / "beginning of" / "mid" expressions ────────────────────

class TestEndBeginningMid:
    def test_end_of_june(self):
        r = parse_date("end of June", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 6

    def test_end_of_the_month(self):
        r = parse_date("end of the month", reference_date=REF)
        assert r is not None

    def test_beginning_of_next_year(self):
        r = parse_date("beginning of next year", reference_date=REF)
        assert r is not None
        assert r.start_date.year >= 2025

    def test_early_july(self):
        r = parse_date("early July", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 7

    def test_mid_march(self):
        r = parse_date("mid-March", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 3

    def test_late_december(self):
        r = parse_date("late December", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 12


# ── Date ranges (to / through / until / between) ─────────────────────

class TestDateRanges:
    def test_june_1_to_june_15(self):
        r = parse_date("June 1 to June 15", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 6

    def test_monday_through_friday(self):
        r = parse_date("Monday through Friday", reference_date=REF)
        assert r is not None

    def test_between_march_and_may(self):
        r = parse_date("between March and May", reference_date=REF)
        assert r is not None

    def test_from_3pm_to_5pm(self):
        r = parse_date("from 3pm to 5pm", reference_date=REF)
        assert r is not None

    def test_june_1_to_june_15_multi(self):
        r = parse_date("June 1 to June 15", reference_date=REF, multi=True)
        assert isinstance(r, list)
        assert len(r) >= 2


# ── Weekend expressions ───────────────────────────────────────────────

class TestWeekend:
    def test_this_weekend(self):
        r = parse_date("this weekend", reference_date=REF)
        assert r is not None

    def test_next_weekend(self):
        r = parse_date("next weekend", reference_date=REF)
        assert r is not None
        assert r.start_date >= REF

    def test_last_weekend(self):
        r = parse_date("last weekend", reference_date=REF)
        assert r is not None
        assert r.start_date < REF


# ── Relative with "later" ────────────────────────────────────────────

class TestLaterExpressions:
    def test_3_days_later(self):
        r = parse_date("3 days later", reference_date=REF)
        assert r is not None
        assert r.start_date > REF

    def test_an_hour_later(self):
        r = parse_date("an hour later", reference_date=REF)
        assert r is not None
        assert r.start_date > REF

    def test_two_weeks_later(self):
        r = parse_date("two weeks later", reference_date=REF)
        assert r is not None
        assert r.start_date > REF


# ── Preposition anchors (by, since, as of, before, after, until) ─────

class TestPrepositionAnchors:
    def test_by_next_friday(self):
        r = parse_date("by next Friday", reference_date=REF)
        assert r is not None
        assert r.start_date.weekday() == 4

    def test_since_monday(self):
        r = parse_date("since Monday", reference_date=REF)
        assert r is not None
        assert r.start_date.weekday() == 0

    def test_as_of_yesterday(self):
        r = parse_date("as of yesterday", reference_date=REF)
        assert r is not None
        assert r.start_date == dt(2024, 6, 14, 0, 0)

    def test_until_july(self):
        r = parse_date("until July", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 7

    def test_before_june_1st(self):
        r = parse_date("before June 1st", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 6
        assert r.start_date.day == 1

    def test_after_july_4th(self):
        r = parse_date("after July 4th", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 7
        assert r.start_date.day == 4


# ── False positives: IPs, scores, page numbers, symbols ──────────────

class TestMoreFalsePositives:
    def test_ip_address(self):
        r = parse_date("connect to 192.168.1.1", reference_date=REF)
        assert r is None

    def test_score_with_colon(self):
        r = parse_date("scored 3:2 in the match", reference_date=REF)
        assert r is None

    def test_chapter_number(self):
        r = parse_date("read chapter 12", reference_date=REF)
        assert r is None

    def test_page_number_that_looks_like_year(self):
        """Ideally None, but bare 4-digit years after 'page' are hard to reject."""
        r = parse_date("see page 2024", reference_date=REF)
        # Shouldn't crash; may parse 2024 as a year

    def test_dollar_sign_before_number(self):
        r = parse_date("it costs $2024", reference_date=REF)
        assert r is None

    def test_percentage(self):
        r = parse_date("achieved 100% accuracy", reference_date=REF)
        assert r is None

    def test_2_of_the_3_people(self):
        """'2 of the 3' should not become '2nd of the 3rd'."""
        r = parse_date("2 of the 3 people agreed", reference_date=REF)
        assert r is None


# ── Timezone‑aware strings ────────────────────────────────────────────

class TestTimezoneStrings:
    def test_3pm_est(self):
        r = parse_date("3pm EST", reference_date=REF)
        assert r is not None
        assert r.start_date.hour == 15

    def test_iso_with_z(self):
        r = parse_date("2024-06-15T12:00:00Z", reference_date=REF)
        assert r is not None
        assert r.start_date == dt(2024, 6, 15, 12, 0, 0)

    def test_iso_with_offset(self):
        r = parse_date("2024-06-15T12:00:00+05:30", reference_date=REF)
        assert r is not None
        assert r.start_date.year == 2024
        assert r.start_date.month == 6
        assert r.start_date.day == 15


# ── RFC 2822 / email header format ───────────────────────────────────

class TestRFC2822:
    def test_rfc2822_basic(self):
        r = parse_date("Sat, 15 Jun 2024 12:00:00", reference_date=REF)
        assert r is not None
        assert r.start_date.year == 2024
        assert r.start_date.month == 6
        assert r.start_date.day == 15

    def test_rfc2822_with_timezone(self):
        r = parse_date("Sat, 15 Jun 2024 12:00:00 +0000", reference_date=REF)
        assert r is not None
        assert r.start_date.year == 2024


# ── Leap year / boundary edge cases ─────────────────────────────────

class TestLeapYearBoundary:
    def test_feb_29_leap_year(self):
        r = parse_date("Feb 29, 2024", reference_date=REF)
        assert r is not None
        assert r.start_date == dt(2024, 2, 29)

    def test_feb_29_non_leap_year(self):
        """Should not crash on invalid date; None or graceful handling."""
        r = parse_date("Feb 29, 2023", reference_date=REF)
        # Either None or silently adjusted — just don't crash

    def test_december_31(self):
        r = parse_date("December 31", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 12
        assert r.start_date.day == 31

    def test_january_1(self):
        r = parse_date("January 1", reference_date=REF)
        assert r is not None
        assert r.start_date.month == 1
        assert r.start_date.day == 1


# ── Fortnight / decade / less common units ───────────────────────────

class TestUncommonUnits:
    def test_fortnight_ago(self):
        r = parse_date("a fortnight ago", reference_date=REF)
        assert r is not None
        assert r.start_date < REF

    def test_decade_ago(self):
        r = parse_date("a decade ago", reference_date=REF)
        assert r is not None
        assert r.start_date.year <= 2014

    def test_last_decade(self):
        r = parse_date("last decade", reference_date=REF)
        assert r is not None
        assert r.start_date < REF


# ── Compound "ago" with "and" ────────────────────────────────────────

class TestCompoundAgo:
    def test_2_hours_and_30_minutes_ago(self):
        r = parse_date("2 hours and 30 minutes ago", reference_date=REF)
        assert r is not None
        assert r.start_date < REF

    def test_1_day_and_6_hours_ago(self):
        r = parse_date("1 day and 6 hours ago", reference_date=REF)
        assert r is not None
        assert r.start_date < REF


# ── Written‑out ordinals for day of month ────────────────────────────

class TestWrittenOrdinals:
    def test_the_fifth_of_june(self):
        """Written ordinals + 'of Month' require scanner-level support."""
        r = parse_date("the fifth of June", reference_date=REF)
        # Aspirational: ideally day=5, month=6
        if r is not None:
            assert r.start_date.month == 6

    def test_the_twenty_first_of_march(self):
        """Two-word written ordinals are not yet supported."""
        r = parse_date("the twenty first of March", reference_date=REF)
        # Aspirational: ideally day=21, month=3
        if r is not None:
            assert r.start_date.month == 3

    def test_first_of_january(self):
        r = parse_date("first of January", reference_date=REF)
        # "first" is parsed as a modifier; ideally day=1, month=1
        if r is not None:
            assert r.start_date.month == 1


# ── Approximate modifiers ────────────────────────────────────────────

class TestApproximateModifiers:
    def test_around_3pm(self):
        r = parse_date("around 3pm", reference_date=REF)
        assert r is not None
        assert r.start_date.hour == 15

    def test_approximately_2_weeks_ago(self):
        r = parse_date("approximately 2 weeks ago", reference_date=REF)
        assert r is not None
        assert r.start_date < REF

    def test_about_an_hour_ago(self):
        r = parse_date("about an hour ago", reference_date=REF)
        assert r is not None
        assert r.start_date < REF

    def test_roughly_3_days_from_now(self):
        r = parse_date("roughly 3 days from now", reference_date=REF)
        assert r is not None
        assert r.start_date > REF


# ── Case sensitivity ─────────────────────────────────────────────────

class TestCaseSensitivity:
    def test_all_caps_tomorrow(self):
        r = parse_date("TOMORROW", reference_date=REF)
        assert r is not None
        assert r.start_date == dt(2024, 6, 16, 0, 0)

    def test_all_caps_next_week(self):
        r = parse_date("NEXT WEEK", reference_date=REF)
        assert r is not None

    def test_lowercase_month_day_year(self):
        r = parse_date("june 15, 2024", reference_date=REF)
        assert r is not None
        assert r.start_date == dt(2024, 6, 15, 0, 0)

    def test_mixed_case(self):
        r = parse_date("nExT fRiDaY", reference_date=REF)
        assert r is not None
        assert r.start_date.weekday() == 4


# ── Multi‑mode deduplication / edge cases ────────────────────────────

class TestMultiEdgeCases:
    def test_duplicate_dates(self):
        r = parse_date("tomorrow and tomorrow", reference_date=REF, multi=True)
        assert isinstance(r, list)
        assert len(r) >= 1

    def test_single_date_in_multi(self):
        r = parse_date("only yesterday matters", reference_date=REF, multi=True)
        assert isinstance(r, list)
        assert len(r) >= 1
        assert r[0].start_date == dt(2024, 6, 14, 0, 0)
