import re
from datetime import datetime

from dateutil.relativedelta import relativedelta as rd

from metadate.classes import (
    MetaBetween,
    MetaModifier,
    MetaOrdinal,
    MetaPeriod,
    MetaRange,
    MetaRelative,
    MetaUnit,
)
from metadate.scanner import Scanner
from metadate.new_scanner import NewScanner
from metadate.utils import Units, BOUNDARIES, erase_level, log, resolve_end_period
from metadate.classes import MetaAnd

import metadate.locales.en as locale_en
import metadate.locales.nl as locale_nl
from metadate.add_timezone import add_timezone

# TODO: have to make locale lazy still

lang_to_locale = {"en": locale_en, "nl": locale_nl}
lang_to_scanner = {}
lang_to_new_scanner = {}


def get_relevant_parts(matches):
    strike = 0
    bundles = [[]]
    for m in matches:
        if isinstance(m, str):
            strike = 1
        else:
            if strike:
                bundles.append([m])
            else:
                bundles[-1].append(m)
            strike = 0
    return bundles


def between_allowed(x, y, text):
    start = x.span[1]
    end = y.span[0]
    return re.match("^[ -]*(and)?[ -]*$", text[start:end])


def merge_ordinal_unit(matches, text):
    news = []
    t = 0
    last = False
    n = len(matches)
    spans = []
    for i, m in enumerate(matches):
        if i != n - 1 and isinstance(m, MetaOrdinal):
            if last and not between_allowed(last, m, text):
                t = 0
            t += float(m.amount)
            last = m
            spans.append(m.span)
            continue
        elif isinstance(m, MetaUnit):
            m.modifier *= t if t else 1
            m.span = [m.span] + spans
            spans = []
        news.append(m)
    return news


def cleanup_relevant_parts(bundles, locale, now):
    cleaned_bundles = []
    for bundle in bundles:
        modifier = False
        relative = False
        meta_units = []
        new = []
        for x in bundle:
            if isinstance(x, MetaModifier):  # "this"
                if meta_units:
                    for mu in meta_units:
                        unit = mu.unit.lower() + 's'
                        rd_kwargs = {unit: mu.modifier * locale.MODIFIERS[x.x]}
                        new.append(
                            MetaRelative(
                                levels={Units[mu.unit]},
                                span=[x.span, mu.span],
                                modifier=True,
                                **rd_kwargs,
                            )
                        )
                if relative and relative.rd.weekday is not None:
                    relative.modifier = x
                modifier = x
                relative = False
                meta_units = []
            elif isinstance(x, MetaAnd):
                continue
            elif modifier:
                if isinstance(x, MetaUnit):
                    if x.unit == "quarter":
                        rd_kwargs = {
                            "months": 3 * x.modifier * locale.MODIFIERS[modifier.x],
                            "day": 1,
                        }
                    elif x.unit == "season":
                        rd_kwargs = {"months": 3 * x.modifier * locale.MODIFIERS[modifier.x],
                                     "day": 21}
                    else:
                        unit = x.unit.lower() + 's'
                        rd_kwargs = {unit: x.modifier * locale.MODIFIERS[modifier.x]}
                    new.append(
                        MetaRelative(
                            levels={Units[x.unit]},
                            span=[x.span, modifier.span],
                            modifier=True,
                            **rd_kwargs,
                        )
                    )
                # elif isinstance(x, MetaDate):
                #     # if hasattr(x, "month"):
                #     #     print("relative", x.month, modifier)
                #     # if hasattr(x, "season"):
                #     #     print("relative", x.season, modifier)
                #     new.append(x)
                elif x.is_relative:
                    if x.rd.weekday is not None:
                        modifier_value = locale.MODIFIERS[modifier.x]
                        merge_rd_modifier(x, modifier_value, now)
                        modifier = False
                        relative = False
                    new.append(x)
            elif isinstance(x, MetaUnit):
                meta_units.append(x)
                modifier = False
                relative = False
            elif isinstance(x, MetaOrdinal):
                continue
            elif isinstance(x, MetaRelative):
                relative = x
                new.append(x)
            else:
                new.append(x)
        for mu in meta_units:
            if isinstance(mu.span, list):
                # A bare "second" or "minute" (modifier=1, no preceding number)
                # is likely the ordinal adjective, not the time unit.
                if mu.modifier == 1 and len(mu.span) == 1 and mu.unit in ("SECOND", "MINUTE"):
                    continue
                unit = mu.unit.lower() + 's'
                rd_kwargs = {unit: mu.modifier}
                new.append(
                    MetaRelative(
                        levels={Units[mu.unit]},
                        span=mu.span,
                        **rd_kwargs,
                    )
                )
        cleaned_bundles.append(new)
    cleaned_bundles = [x for x in cleaned_bundles if any([y.is_relative for y in x])]
    return cleaned_bundles


def flatten_inner(l):
    span = []
    for s in l:
        if isinstance(s, list):
            span.extend(flatten_span(s))
        else:
            span.append(s)
    return span


def flatten_span(l):
    return sorted(set(flatten_inner(l)))


def get_levels(cleaned_bundle):
    levels = set()
    for x in cleaned_bundle:
        if isinstance(x, (MetaRelative, MetaUnit)):
            levels.update(x.levels)
    return min(levels), levels


def correct_rd_weekday(cleaned_bundle, now):
    for x, y in zip(cleaned_bundle[:-1], cleaned_bundle[1:]):
        if x.is_relative and x.rd.weekday is not None and x.rd.days == 0:
            relative_weekday = rd(weekday=x.rd.weekday)
            if y.is_relative:
                now = now + y.rd
            now_weekday = now.weekday()

            # avoids 0/1 index problem with day
            same_day = (now + relative_weekday).weekday() == now_weekday
            still_to_come_this_week = (now + relative_weekday).weekday() > now_weekday
            already_passed_this_week = (now + relative_weekday).weekday() < now_weekday

            if same_day and x.modifier:
                if x.modifier.value > 0:
                    x.rd.weeks = 1
                else:
                    x.rd.weeks = -1
            # on wednesday saying: "monday in 2 weeks"
            if x.modifier and already_passed_this_week:
                x.rd.weeks = -1


def merge_rd_modifier(x, modifier_value, now):
    # x.modifier = True
    relative_weekday = rd(weekday=x.rd.weekday)
    now_weekday = now.weekday()

    # avoids 0/1 index problem with day
    same_day = (now + relative_weekday).weekday() == now_weekday
    still_to_come_this_week = (now + relative_weekday).weekday() > now_weekday
    already_passed_this_week = (now + relative_weekday).weekday() < now_weekday
    # relativedelta weekdays added always choose the next one
    # so when the modifier value is 1 week then ignore it
    if still_to_come_this_week and modifier_value == 1:
        modifier_value = 0
    if already_passed_this_week and modifier_value == 1:
        modifier_value = 0
    if same_day and modifier_value == 0:
        modifier_value = 1
    x.rd.weeks += modifier_value


def datify(cleaned_bundle, past_boundary, future_boundary, now, locale, text):
    span = flatten_span([x.span for x in cleaned_bundle])
    min_level, levels = get_levels(cleaned_bundle)
    words = [text[x[0] : x[1]].lower() for x in span]
    # print(level)
    rdt, erdt = resolve_dt(cleaned_bundle, now)

    correct_rd_weekday(cleaned_bundle, now)

    if erdt is not None:
        start_date = rdt + resolve_rd(cleaned_bundle)
        end_date = erdt + resolve_rd(cleaned_bundle)
        start_date, _, _ = resolve_end_period(
            start_date, levels, past_boundary, future_boundary, now, words, locale
        )
        end_date = erase_level(erdt, min_level)
    else:
        resolved_rd = resolve_rd(cleaned_bundle)
        try:
            start_date = rdt + resolved_rd
        except TypeError:
            resolved_rd.years = int(resolved_rd.years)
            resolved_rd.months = int(resolved_rd.months)
            try:
                start_date = rdt + resolved_rd
            except ValueError:
                return None, None, None, None
        except ValueError:
            return None, None, None, None
        start_date, end_date, _ = resolve_end_period(
            start_date, levels, past_boundary, future_boundary, now, words, locale
        )
    return start_date, end_date, levels, span


def has_between(bundle):
    return any(isinstance(x, MetaBetween) for x in bundle)


def merge_dt(mdt, dt):
    # mdt is a made-up mutable dt
    # month is precomputed so that day can be init
    month = mdt[1] if mdt[1] > -1 else dt.month
    day = mdt[2] if mdt[2] > -1 else dt.day
    ndt = datetime(
        year=mdt[0] if mdt[0] > -1 else dt.year,
        month=month,
        day=min(day, BOUNDARIES[month]),
        hour=mdt[3] if mdt[3] > -1 else dt.hour,
        minute=mdt[4] if mdt[4] > -1 else dt.minute,
        second=mdt[5] if mdt[5] > -1 else dt.second,
        microsecond=mdt[6] if mdt[6] > -1 else dt.microsecond,
    )
    return ndt


def resolve_dt(cleaned_bundle, now):
    # the concept of checking whether there is overlap is good, and crash?
    indices = {
        'year': 0,
        'month': 1,
        'day': 2,
        'hour': 3,
        'minute': 4,
        'second': 5,
        'microsecond': 6,
    }
    dts = [-1, -1, -1, -1, -1, -1, -1]
    edts = [-1, -1, -1, -1, -1, -1, -1]
    contains_between = has_between(cleaned_bundle)
    # for d in cleaned_bundle:
    #     if isinstance(d, MetaDate):
    #         for x in d.__dict__:
    #             if x not in ['level', 'span', 'now']:
    #                 if dts[indices[x]] != -1:
    #                     if not contains_between:
    #                         raise ValueError("Field already known in dt, should not overwrite?")
    #                 else:
    #                     dts[indices[x]] = getattr(d, x)
    #                 edts[indices[x]] = getattr(d, x)
    dt = merge_dt(dts, now)
    edt = merge_dt(edts, now) if contains_between else None
    return dt, edt


def resolve_rd(cleaned_bundle):
    rds = rd()
    for d in cleaned_bundle:
        if d.is_relative:
            other = d.rd
            # "on sunday 12th of may" --- adding weekday + day should
            # remove corrective weekday +7 days
            # if rds.weekday is not None and other.day is not None:
            #     rds.days = 0
            # if other.weekday is not None and rds.day is not None:
            #     other.days = 0
            rds += other
    return rds


def has_modifier(cleaned_bundle):
    return any([x.modifier for x in cleaned_bundle if x.is_relative])


def handle_meta_range(cleaned_bundle, past_boundary, locale, text, future_boundary, now):
    phase = 0
    mrange = None
    relatives = []
    metadates = []
    contains_modifier = False
    for x in cleaned_bundle:
        if phase == 0 and isinstance(x, MetaRange):
            phase = 1
            mrange = x
        elif (phase == 1 or phase == 2) and x.is_relative:
            phase = 2
            relatives.append(x)
            contains_modifier = contains_modifier or x.modifier
        elif (phase == 2 or phase == 3) and isinstance(x, MetaPeriod):
            phase = 3
            metadates.append(x)
    if phase < 2:
        return None
    # case 1: MetaRange, MetaRelative, MetaRelative, etc
    # log("relatives", relatives, True)
    # log("metadates", metadates, True)
    if not metadates:
        # in this case, the start_date becomes "now" adjusted for level
        # the end_date is the start_date + relativedeltas following
        min_level, levels = get_levels(relatives)
        rds = rd()
        start_date = erase_level(now, 1)
        for x in relatives:
            if x.rd is None:
                continue
            rds += x.rd
        try:
            end_date = start_date + rds
        except TypeError:
            rds.years = int(rds.years)
            rds.months = int(rds.months)
            end_date = start_date + rds
        span = flatten_span([mrange.span, [x.span for x in relatives]])
        if start_date > end_date:
            start_date, end_date = end_date, start_date
        return MetaPeriod(
            start_date, end_date, levels, span, locale.NAME, text, contains_modifier, now
        )
    else:
        spans = [flatten_span(x.span) for x in relatives]
        words = [text[x[0] : x[1]].lower() for x in spans]
        # generic
        dt, _ = resolve_dt(metadates, now)
        min_dt_level, dt_levels = get_levels(metadates)
        min_rd_level, rd_levels = get_levels(relatives)
        if "first" in words:
            # this is actually the logic for !first! days of, not "next days of"
            # case 2: MetaRange, MetaRelative, MetaRelative, etc, MetaDate, Metadate, etc
            start_date, _, date_changed = resolve_end_period(
                dt, dt_levels, past_boundary, future_boundary, now, words, locale
            )
            end_date = erase_level(dt, min_dt_level) + resolve_rd(relatives)
        elif "last" in words:
            _, end_date, date_changed = resolve_end_period(
                dt, dt_levels, past_boundary, future_boundary, now, words, locale
            )
            start_date = erase_level(end_date, min_dt_level) + resolve_rd(relatives)
        else:
            return None
            raise NotImplementedError("What's the case here?")
    if date_changed:
        start_date = start_date + rd(years=1)  # I think also here?
        end_date = end_date + rd(years=1)
    span = flatten_span([mrange.span] + [x.span for x in relatives] + [x.span for x in metadates])
    return MetaPeriod(
        start_date,
        end_date,
        rd_levels.union(dt_levels),
        span,
        locale.NAME,
        text,
        contains_modifier,
        now,
    )


def min_span(obj):
    span = getattr(obj, "span")
    if span is None:
        return 2 ** 50
    if isinstance(span, list):
        return span[0][0]
    return span[0]


def parse_date(
    text,
    future=rd(years=30),
    past=rd(years=100),
    lang="en",
    reference_date=None,
    scanner=None,
    multi=False,
    verbose=False,
    min_level=None,
    max_level=None,
    return_early=0,
    use_new_scanner=False,
):
    now = reference_date or datetime.now()
    past = past or rd()
    future = future or rd()
    locale = lang_to_locale[lang]
    log("\nSentence", text, verbose)
    if use_new_scanner:
        if lang not in lang_to_new_scanner:
            lang_to_new_scanner[lang] = NewScanner(locale)
        scanner = lang_to_new_scanner[lang]
    else:
        if lang not in lang_to_scanner:
            scanner = Scanner(locale)
            lang_to_scanner[lang] = scanner
        scanner = lang_to_scanner[lang]
    matches, _ = scanner.scan(text)
    log("matches", matches, verbose=verbose)
    parts = get_relevant_parts(matches)
    log("1", parts, verbose=verbose)
    if return_early == 1:
        return parts
    merged = [merge_ordinal_unit(x, text) for x in parts]
    if return_early == 2:
        return merged
    log("2", merged, verbose=verbose)
    cleaned_parts = cleanup_relevant_parts(merged, locale, now)
    if return_early == 3:
        return cleaned_parts
    log("3", cleaned_parts, verbose)

    if not cleaned_parts:
        return [] if multi else None

    if multi:
        expanded = []
        for bundle in cleaned_parts:
            weekday_only = [
                x for x in bundle
                if isinstance(x, MetaRelative) and x.rd.weekday is not None
                and x.rd.days == 0 and x.rd.months == 0 and x.rd.years == 0
            ]
            if len(weekday_only) > 1 and len(weekday_only) == len([x for x in bundle if isinstance(x, MetaRelative)]):
                for wd in weekday_only:
                    expanded.append([wd])
            else:
                expanded.append(bundle)
        cleaned_parts = expanded

    cleaned_parts = sorted(cleaned_parts, key=len, reverse=True)

    mps = []
    past_boundary = now - past
    future_boundary = now + future

    for cleaned_bundle in cleaned_parts:
        if mps and mps[0] and not multi:
            return add_timezone(mps[0])
        handle_meta_result = handle_meta_range(
            cleaned_bundle, past_boundary, locale, text, future_boundary, now
        )
        if handle_meta_result:
            log("4meta", handle_meta_result, verbose)
            mps.append(handle_meta_result)
            continue

        start_date, end_date, levels, span = datify(
            cleaned_bundle, past_boundary, future_boundary, now, locale, text
        )

        if start_date is None:
            continue

        contains_modifier = has_modifier(cleaned_bundle)
        mp = MetaPeriod(
            start_date, end_date, levels, span, locale.NAME, text, contains_modifier, now
        )

        if min_level is not None and mp.min_level < min_level:
            continue

        if max_level is not None and mp.max_level > max_level:
            continue

        log("4default", mp, verbose)
        if past_boundary <= mp.start_date <= future_boundary:
            mps.append(mp)

    if not multi:
        if mps:
            return add_timezone(mps[0])
        else:
            return None

    return [add_timezone(x) for x in mps]
