from datetime import datetime

import pytest
from dateutil.relativedelta import relativedelta as rd

from metadate import parse_date, MetaPeriod


REF = datetime(2024, 6, 15, 12, 0, 0)


def dt(*args):
    return datetime(*args)


class TestBasicDates:
    def test_today(self):
        r = parse_date("today", reference_date=REF)
        assert r.start_date == dt(2024, 6, 15, 0, 0, 0)
        assert r.end_date == dt(2024, 6, 16, 0, 0, 0)

    def test_tomorrow(self):
        r = parse_date("tomorrow", reference_date=REF)
        assert r.start_date == dt(2024, 6, 16, 0, 0, 0)
        assert r.end_date == dt(2024, 6, 17, 0, 0, 0)

    def test_yesterday(self):
        r = parse_date("yesterday", reference_date=REF)
        assert r.start_date == dt(2024, 6, 14, 0, 0, 0)
        assert r.end_date == dt(2024, 6, 15, 0, 0, 0)


class TestYearMonthDay:
    def test_iso_date(self):
        r = parse_date("2024-01-15", reference_date=REF)
        assert r.start_date == dt(2024, 1, 15, 0, 0, 0)
        assert r.end_date == dt(2024, 1, 16, 0, 0, 0)

    def test_month_name(self):
        r = parse_date("June 25", reference_date=REF)
        assert r.start_date == dt(2024, 6, 25, 0, 0, 0)

    def test_month_year(self):
        r = parse_date("March 2023", reference_date=REF)
        assert r.start_date == dt(2023, 3, 1, 0, 0, 0)
        assert r.end_date == dt(2023, 4, 1, 0, 0, 0)

    def test_year(self):
        r = parse_date("2023", reference_date=REF)
        assert r.start_date == dt(2023, 1, 1, 0, 0, 0)
        assert r.end_date == dt(2024, 1, 1, 0, 0, 0)

    def test_last_year(self):
        r = parse_date("last year", reference_date=REF)
        assert r.start_date == dt(2023, 1, 1, 0, 0, 0)
        assert r.end_date == dt(2024, 1, 1, 0, 0, 0)


class TestTimes:
    def test_time_pm(self):
        r = parse_date("3 pm", reference_date=REF)
        assert r.start_date == dt(2024, 6, 15, 15, 0, 0)

    def test_time_colon_pm(self):
        r = parse_date("3:00 pm", reference_date=REF)
        assert r.start_date == dt(2024, 6, 15, 15, 0, 0)

    def test_12pm(self):
        r = parse_date("12:00 pm", reference_date=REF)
        assert r.start_date == dt(2024, 6, 15, 12, 0, 0)

    def test_full_datetime(self):
        r = parse_date("2024-06-15 10:30:00", reference_date=REF)
        assert r.start_date == dt(2024, 6, 15, 10, 30, 0)


class TestRelative:
    def test_next_week(self):
        r = parse_date("next week", reference_date=REF)
        assert r is not None
        assert r.start_date >= REF
        assert r.end_date > REF

    def test_next_month(self):
        r = parse_date("next month", reference_date=REF)
        assert r is not None
        assert r.start_date >= REF


class TestNoDate:
    def test_no_date_returns_none(self):
        r = parse_date("nothing here", reference_date=REF)
        assert r is None

    def test_no_date_multi_returns_empty(self):
        r = parse_date("nothing here", reference_date=REF, multi=True)
        assert r == []


class TestLevels:
    def test_time_has_time(self):
        r = parse_date("3 pm", reference_date=REF)
        assert r.has_time is True

    def test_date_has_day(self):
        r = parse_date("June 25", reference_date=REF)
        assert r.has_day is True

    def test_year_has_year(self):
        r = parse_date("2023", reference_date=REF)
        assert r.has_year is True

    def test_month_has_month(self):
        r = parse_date("March 2023", reference_date=REF)
        assert r.has_month is True


class TestMetaPeriodComparison:
    """Tests for the fixed __gt__, __eq__, __lt__ comparisons."""

    def _make_mp(self, start, end):
        return MetaPeriod(start, end, set(), [], "en", "", False, REF)

    def test_eq_same(self):
        mp1 = self._make_mp(dt(2024, 1, 1), dt(2024, 1, 2))
        mp2 = self._make_mp(dt(2024, 1, 1), dt(2024, 1, 2))
        assert mp1 == mp2

    def test_eq_different_end(self):
        mp1 = self._make_mp(dt(2024, 1, 1), dt(2024, 1, 2))
        mp2 = self._make_mp(dt(2024, 1, 1), dt(2024, 1, 3))
        assert not (mp1 == mp2)

    def test_gt_by_start(self):
        mp1 = self._make_mp(dt(2024, 2, 1), dt(2024, 2, 2))
        mp2 = self._make_mp(dt(2024, 1, 1), dt(2024, 1, 2))
        assert mp1 > mp2

    def test_gt_by_end_same_start(self):
        mp1 = self._make_mp(dt(2024, 1, 1), dt(2024, 1, 3))
        mp2 = self._make_mp(dt(2024, 1, 1), dt(2024, 1, 2))
        assert mp1 > mp2

    def test_lt_by_start(self):
        mp1 = self._make_mp(dt(2024, 1, 1), dt(2024, 1, 2))
        mp2 = self._make_mp(dt(2024, 2, 1), dt(2024, 2, 2))
        assert mp1 < mp2

    def test_lt_by_end_same_start(self):
        mp1 = self._make_mp(dt(2024, 1, 1), dt(2024, 1, 2))
        mp2 = self._make_mp(dt(2024, 1, 1), dt(2024, 1, 3))
        assert mp1 < mp2

    def test_not_gt_when_equal(self):
        mp1 = self._make_mp(dt(2024, 1, 1), dt(2024, 1, 2))
        mp2 = self._make_mp(dt(2024, 1, 1), dt(2024, 1, 2))
        assert not (mp1 > mp2)

    def test_not_lt_when_equal(self):
        mp1 = self._make_mp(dt(2024, 1, 1), dt(2024, 1, 2))
        mp2 = self._make_mp(dt(2024, 1, 1), dt(2024, 1, 2))
        assert not (mp1 < mp2)


class TestMetaRelativeRdPassthrough:
    """Test that passing rd= to MetaRelative stores it correctly."""

    def test_rd_passthrough(self):
        from metadate.classes import MetaRelative

        expected_rd = rd(days=5)
        mr = MetaRelative(span=(0, 1), rd=expected_rd)
        assert mr.rd == expected_rd

    def test_rd_passthrough_not_rd_args(self):
        from metadate.classes import MetaRelative

        expected_rd = rd(days=5)
        mr = MetaRelative(span=(0, 1), rd=expected_rd, years=1)
        # rd should be the passed relativedelta, not the rd_args dict
        assert isinstance(mr.rd, rd)


class TestMultiParse:
    def test_multi_returns_list(self):
        r = parse_date("tomorrow", reference_date=REF, multi=True)
        assert isinstance(r, list)
        assert len(r) >= 1
