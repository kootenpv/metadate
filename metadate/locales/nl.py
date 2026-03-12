NAME = "en"

# make it not based on index, but key val, and include "hrs", "wks"
UNITS = {
    'microseconde': 'MICROSECOND',
    'microseconden': 'MICROSECOND',
    'second': 'SECOND',
    'seconde': 'SECOND',
    'minuut': 'MINUTE',
    'minuten': 'MINUTE',
    'mins': 'MINUTE',
    'min': 'MINUTE',
    'uur': 'HOUR',
    'uren': 'HOUR',
    'dag': 'DAY',
    'dagen': 'DAY',
    'week': 'WEEK',
    'weken': 'WEEK',
    'kwartaal': 'QUARTER',
    'kwartalen': 'QUARTER',
    'maand': 'MONTH',
    'maanden': 'MONTH',
    'seizoen': 'SEASON',
    'seizoenen': 'SEASON',
    'jaar': 'YEAR',
    'jaren': 'YEAR'
}


MODIFIERS = {
    "in": 1,
    "op": 0,
    "dit": 0,
    "deze": 0,
    "volgend":  1,
    "volgende":  1,
    "aankomend": 1,
    "aankomende": 1,
    "na": 1,
    "voor": -1,
    'recent': -1,
    'afgelope': -1,
    'afgelopen': -1,
    # untested from here
    "vorig": -1,
    "vorige": -1,
    "laatste": -1,
    "laatst": -1,
    "eerste": 1,
    "eerst": 1,
    "geleden": -1,
    "gelede": -1,
    "eerder": -1,
    "eerdere": -1
}

ORDINAL_NUMBERS = {
    'nul': 0,
    'een': 1,
    'twee': 2,
    'drie': 3,
    'een paar': 3,
    'weinig': 3,
    'vier': 4,
    'vijf': 5,
    'zes': 6,
    'zeven': 7,
    'acht': 8,
    'negen': 9,
    'tien': 10,
    'elf': 11,
    'twaalf': 12,
    'dertien': 13,
    'veertien': 14,
    'vijftien': 15,
    'zestien': 16,
    'zeventien': 17,
    'achttien': 18,
    'negentien': 19,
    'twintig': 20,
    'dertig': 30,
    'veertig': 40,
    'vijftig': 50,
    'zestig': 60,
    'zeventig': 70,
    'tachtig': 80,
    'negentig': 90,
    'honderd': 100,
    'duizend': 1000,
}


IN_THE = ['in de', 'over de', 'over het', 'tijdens de', 'tijdens het', 'binnen de', 'binnen het']


MONTHS = {
    "januari": 1, "februari": 2, "maart": 3, "april": 4, "mei": 5, "juni": 6, "juli": 7,
    "augustus": 8, "september": 9, "oktober": 10, "november": 11, "december": 12
}
MONTHS_SHORTS = {k[:3]: v for k, v in MONTHS.items()}
MONTHS_SHORTS["febr"] = 2
MONTHS_SHORTS["sept"] = 9

WEEKDAY = {
    "maandag": 0,
    "dinsdag": 1,
    "woensdag": 2,
    "donderdag": 3,
    "vrijdag": 4,
    "zaterdag": 5,
    "zondag": 6
}

WEEKDAY_SHORTS = {k[:3]: v for k, v in WEEKDAY.items()}

SEASONS = {
    "winter": 12,
    "lente": 3,
    "zomer": 6,
    "herfst": 9
}

QUARTERS = {
    "q1": 1,
    "eerste kwartaal": 1,
    "q2": 4,
    "tweede kwartaal": 4,
    "q3": 7,
    "derde kwartaal": 7,
    "q4": 10,
    "vierde kwartaal": 10,
    "laatste kwartaal": 10
}

NOW = [
    'nu'
]

TODAY_TOMORROW = {
    "vandaag": 0,
    "morgen": 1,
    "gisteren": -1,
    "overmorge": 2,
    "overmorgen": 2,
    "eergister": -1,
    "eergisteren": -1
}

NOON = {
    "middag": 12,
    "middernacht": 0,
    "eind van de middag": 17,
    "eind van de ochtend": 11,
}

AND = ["en", "of", "tot"]

RANK_NAMING = ['e']
ON_THE = ['op de']
AT = ['om']

WHITELIST = ["op", "of", "van", ",", "de", "nacht",
             "morgen", "voor", "om", "middag", "ochtend", "avond"]


BETWEEN = ["tussen", "voor"]
EVERY = ["elke"]
DURATION = ["voor", "tot"]

DD_LEFT_FIRST = True
