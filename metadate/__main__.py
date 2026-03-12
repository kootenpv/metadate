import sys
from metadate import parse_date


def main():
    """ This is the function that is run from commandline with `metadate` """
    if "--lang" == sys.argv[1]:
        dates = parse_date(" ".join(sys.argv[3:]), lang=sys.argv[2], multi=True)
    else:
        dates = parse_date(" ".join(sys.argv[1:]), multi=True)
    if len(dates) == 2:
        print(dates[1].start_date - dates[0].start_date)
    else:
        print(dates)
