NAME = "en"

# make it not based on index, but key val, and include "hrs", "wks"
UNITS = {
    'microsecond': 'MICROSECOND',
    'microseconds': 'MICROSECOND',
    'second': 'SECOND',
    'seconds': 'SECOND',
    'minute': 'MINUTE',
    'minutes': 'MINUTE',
    'mins': 'MINUTE',
    'min': 'MINUTE',
    'hour': 'HOUR',
    'hours': 'HOUR',
    "hrs": 'HOUR',
    "hr": 'HOUR',
    'day': 'DAY',
    'days': 'DAY',
    'week': 'WEEK',
    'weeks': 'WEEK',
    'wks': "WEEK",
    'quarter': 'QUARTER',
    'quarters': 'QUARTER',
    'month': 'MONTH',
    'months': 'MONTH',
    'season': 'SEASON',
    'seasons': 'SEASON',
    'year': 'YEAR',
    'years': 'YEAR',
    'yrs': "YEAR"
}


MODIFIERS = {
    "in": 1,
    "on": 0,
    "this": 0,
    "next":  1,
    "coming": 1,
    "after": 1,
    "before": -1,
    'recent': -1,
    'past': -1,
    # untested from here
    "previous": -1,
    "last": -1,
    "first": 1,
    "ago": -1,
    "earlier": -1,
    "prior": -1
}

ORDINAL_NUMBERS = {
    'zero': 0,
    'one': 1,
    'two': 2,
    'three': 3,
    'few': 3,
    'a couple': 3,
    'four': 4,
    'five': 5,
    'six': 6,
    'seven': 7,
    'eight': 8,
    'nine': 9,
    'ten': 10,
    'eleven': 11,
    'twelve': 12,
    'thirteen': 13,
    'fourteen': 14,
    'fifteen': 15,
    'sixteen': 16,
    'seventeen': 17,
    'eighteen': 18,
    'nineteen': 19,
    'twenty': 20,
    'thirty': 30,
    'fourty': 40,
    'fifty': 50,
    'sixty': 60,
    'seventy': 70,
    'eighty': 80,
    'ninety': 90,
    'hundred': 100,
    'thousand': 1000,
}

# for?
IN_THE = ['in the', 'over the', 'during the', 'within the']  # 'for'

MONTHS = {
    "january": 1, "february": 2, "march": 3, "april": 4, "may": 5, "june": 6, "july": 7,
    "august": 8, "september": 9, "october": 10, "november": 11, "december": 12
}
MONTHS_SHORTS = {k[:3]: v for k, v in MONTHS.items()}
MONTHS_SHORTS["febr"] = 2
MONTHS_SHORTS["sept"] = 9

WEEKDAY = {
    "monday": 0,
    "tuesday": 1,
    "wednesday": 2,
    "thursday": 3,
    "friday": 4,
    "saturday": 5,
    "sunday": 6
}

WEEKDAY_SHORTS = {k[:3]: v for k, v in WEEKDAY.items()}

SEASONS = {
    "winter": 12,
    "spring": 3,
    "summer": 6,
    "fall": 9,
    "autumn": 9
}

QUARTERS = {
    "q1": 1,
    "first quarter": 1,
    "q2": 4,
    "second quarter": 4,
    "q3": 7,
    "third quarter": 7,
    "q4": 10,
    "fourth quarter": 10,
    "final quarter": 10,
    "last quarter": 10,
}


NOW = [
    'now'
]

TODAY_TOMORROW = {
    "today": 0,
    "tomorrow": 1,
    "tmrw": 1,
    "day after tomorrow": 2,
    "yesterday": -1,
    "day before yesterday": -2
}

NOON = {
    "noon": (12, 12),
    "midnight": (0, 0),
    # "end of noon": (17, 17),
    # "end of the afternoon": (1),
    # "end of afternoon": 17,
    "morning": (3, 12),
    "night": (0, 6),
}

AND = ["and", ","]

RANK_NAMING = ['st', 'nd', 'rd', 'th']
ON_THE = ['on the']
AT = ['at']

# still to implement: night, morning
WHITELIST = ["at", "of", ",", "the", "night", "morning"]

BETWEEN = ["between"]
EVERY = ["every"]
DURATION = ["for", "until"]

DD_LEFT_FIRST = False
