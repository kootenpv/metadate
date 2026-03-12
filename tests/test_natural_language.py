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

    @pytest.mark.xfail(reason="weekday + 'in two weeks' combo not yet supported")
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

    @pytest.mark.xfail(reason="'in N weeks' treats start_date as ref rather than ref + offset")
    def test_in_3_weeks(self):
        r = parse_date("in 3 weeks", reference_date=REF)
        assert r is not None
        assert r.start_date > REF

    @pytest.mark.xfail(reason="'in N weeks and N days' treats start_date as ref rather than ref + offset")
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

    @pytest.mark.xfail(reason="multi-unit duration without modifier not parsed as single result")
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

    @pytest.mark.xfail(reason="'within' in sentence context not parsed")
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

    @pytest.mark.xfail(reason="'three in the afternoon' not parsed — 'in the' triggers range logic")
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
    @pytest.mark.xfail(reason="complex sentence: start_date year not correctly resolved to 2018")
    def test_date_embedded_in_sentence(self):
        r = parse_date(
            "The 3 of us will each buy 2 cars 2 weeks after 5th of June in 2018 at 10:05:01 for 100 dollars.",
            reference_date=REF,
        )
        assert r is not None
        assert r.start_date.year == 2018

    @pytest.mark.xfail(reason="complex sentence: start_date year not correctly resolved to 2018")
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

    @pytest.mark.xfail(reason="'in N hours' uses ref as start rather than ref + offset")
    def test_in_5_hours(self):
        r = parse_date("in 5 hours", reference_date=REF)
        assert r is not None
        assert r.start_date > REF

    @pytest.mark.xfail(reason="'in N minutes' uses ref as start rather than ref + offset")
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
