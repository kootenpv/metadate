""" You can kind of see this as the scope of `metadate` when you 'import metadate'
The following functions become available:
metadate.__project__
metadate.__version__
metadate.print_version
metadate.parse_date
"""
from datetime import datetime as _datetime

now = _datetime.now
date = _datetime.date
time = _datetime.time


from metadate.parse_date import parse_date
from metadate.utils import Units
from metadate.classes import MetaPeriod
from dateutil.relativedelta import relativedelta as rd

import sys

__project__ = "metadate"
__version__ = "0.5.74"


def is_mp(x):
    return isinstance(x, MetaPeriod)


def print_version():
    """ Convenient function for printing the version of the package """
    sv = sys.version_info
    py_version = f"{sv.major}.{sv.minor}.{sv.micro}"
    version_parts = __version__.split(".")
    s = f"{__project__} version: [{__version__}], Python {py_version}"
    s += f"\nMajor version: {version_parts[0]}  (breaking changes)"
    s += f"\nMinor version: {version_parts[1]}  (extra feature)"
    s += f"\nMicro version: {version_parts[2]} (commit count)"
    return s
