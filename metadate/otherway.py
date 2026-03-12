import re


def join(x, y=None):
    if y is None:
        return "|".join(x)
    else:
        r = y + "|"
        return r.join(x) + y


def group(x):
    return "({})".format(x)

RE_UNITS = group(join(UNITS, "s?"))
RE_ORDINAL = group(join(ORDINAL_NUMBERS))
RE_LAST_FIRST = group(join(LAST_FIRST))
SS = "[ -]+"

t = "within the last fourty 4 weeks"
re.search("within the " + RE_LAST_FIRST + SS + group(RE_ORDINAL) + "+" + RE_UNITS, t).groups()

re.search("within the " + RE_LAST_FIRST + SS + RE_ORDINAL + RE_ORDINAL + RE_UNITS, t).groups()

matches = []
matches += match(RE_LAST_FIRST, t)
matches += match(RE_ORDINAL, t)
matches += match(RE_UNITS, t)
matches += match("\d+", t)


def match(needle, haystack):
    return [(x.string[x.span()[0]:x.span()[1]], x.span()) for x in re.finditer(needle, haystack)]


haha = "(" + ")|(".join(ORDINAL_NUMBERS) + ")"
RE_ORDINAL = "(?:(?:" + haha + ")[ -]+)+"

t = "within the last fourty four weeks"
list(filter(None, re.search("within the " + RE_LAST_FIRST + SS + RE_ORDINAL + RE_UNITS, t).groups()))


re.findall(RE_ORDINAL + r"[ -]+(?=.*\bitems)", s)
