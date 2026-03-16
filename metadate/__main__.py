import json as _json
import sys
from datetime import datetime

from cliche import cli

from metadate import parse_date


@cli
def main(
    text: tuple[str, ...] = (),
    lang: str = "en",
    multi: bool = False,
    c_scanner: bool = False,
    verbose: bool = False,
    json: bool = False,
    jsonl: bool = False,
    stdin: bool = True,
    reference_date: str = None,
):
    """Parse natural language date expressions.

    :param text: The text to parse for date expressions
    :param lang: Language (en, nl)
    :param multi: Return all dates found instead of just the first
    :param c_scanner: Use the C-accelerated scanner
    :param verbose: Show scanner pipeline debug output
    :param json: Output as indented JSON
    :param jsonl: Output as JSON Lines (one object per line, no indent)
    :param stdin: Read from stdin when no text is given
    :param reference_date: Reference date as ISO string (default: now)
    """
    joined = " ".join(text)
    if not joined and stdin and not sys.stdin.isatty():
        joined = sys.stdin.read().strip()
    if not joined:
        print("No text provided.", file=sys.stderr)
        sys.exit(1)
    ref = datetime.fromisoformat(reference_date) if reference_date else datetime.now()
    result = parse_date(
        joined,
        lang=lang,
        multi=multi,
        use_c_scanner=c_scanner,
        verbose=verbose,
        reference_date=ref,
    )
    if json or jsonl:
        indent = 2 if json else None
        if result is None:
            print(_json.dumps(None))
        elif isinstance(result, list):
            if jsonl:
                for r in result:
                    print(_json.dumps(r.to_dict()))
            else:
                print(_json.dumps([r.to_dict() for r in result], indent=indent))
        else:
            print(_json.dumps(result.to_dict(), indent=indent))
    elif result is None:
        print("No date found.")
    elif isinstance(result, list):
        for r in result:
            print(r)
    else:
        print(result)
