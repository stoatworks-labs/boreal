#!/usr/bin/env python3
"""Every preset row must carry a full set of values, of the right kind.

`Boreal.cpp` maps each preset column to a ParamId with a table whose size is
`presets::kParamCount`, so a column added to one and not the other fails to
compile. What no compiler catches is a preset **row** with too few values: C++
aggregate initialisation fills the rest with zero, without a word, so the row
is accepted and silently means something else.

A `FF_TYPE_INTEGER`, option or boolean parameter holds the **real value**, not
a 0..1 fraction. The SDK's clamp into 0..1 is guarded by
`if( pType == FF_TYPE_STANDARD )`. Write 0.5 into the Hemisphere column and it does
not mean "halfway": it rounds to one or the other depending on who reads it.
"""

import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent.parent
SOURCE = HERE / "source" / "Presets.h"


def main() -> int:
    text = SOURCE.read_text()

    start = text.index("enum Param\n{")
    end = text.index("kParamCount", start)
    names = [
        line.strip().rstrip(",")
        for line in text[start:end].splitlines()
        if line.strip().startswith("k")
    ]
    expected = len(names)
    if expected == 0:
        print("could not parse the Param enum out of Presets.h", file=sys.stderr)
        return 1

    # Columns whose host parameter is discrete, with the range declared for it
    # in Graticule.cpp. A value here must be a whole number inside that range.
    discrete_columns = {
        "kHemisphere": (0, 1),
        "kArcs": (1, 5),
        "kCamera": (0, 1),
        "kObserver": (0, 1),
        "kStarMotion": (0, 1),
        "kHorizon": (0, 2),
    }
    # Every other column is a standard parameter and must sit in 0..1.
    standard_columns = {n: (0.0, 1.0) for n in names if n not in discrete_columns}

    for column in list(discrete_columns) + list(standard_columns):
        if column not in names:
            print(f"the Param enum has no {column}; this check needs updating", file=sys.stderr)
            return 1

    column_of = {name: i for i, name in enumerate(names)}
    failures = 0
    rows = 0

    for match in re.finditer(r'\{\s*"([^"]+)",\s*\{(.*?)\}\s*\}', text, re.S):
        name, body = match.group(1), match.group(2)
        rows += 1
        cleaned = "\n".join(line.split("//")[0] for line in body.splitlines())
        values = [v for v in cleaned.split(",") if v.strip()]

        if len(values) != expected:
            print(f"FAIL  {name}: {len(values)} values, expected {expected}")
            failures += 1
            continue

        if len(name) > 16:
            print(f"FAIL  {name}: {len(name)} characters; FFGL shows 16")
            failures += 1

        for column, (low, high) in discrete_columns.items():
            raw = values[column_of[column]].strip().rstrip("f")
            try:
                number = float(raw)
            except ValueError:
                print(f"FAIL  {name}: {column} is {raw!r}, which is not a number")
                failures += 1
                continue
            if number != int(number):
                print(f"FAIL  {name}: {column} = {raw} is a fraction; that parameter is discrete")
                failures += 1
            elif not low <= number <= high:
                print(f"FAIL  {name}: {column} = {raw} is outside its range {low}..{high}")
                failures += 1

        for column, (low, high) in standard_columns.items():
            raw = values[column_of[column]].strip().rstrip("f")
            number = float(raw)
            if not low <= number <= high:
                print(f"FAIL  {name}: {column} = {raw} is outside 0..1")
                failures += 1

    if rows == 0:
        print("no preset rows found -- has the table's shape changed?", file=sys.stderr)
        return 1
    if failures:
        print(f"\n{failures} problem(s) across {rows} preset rows")
        return 1
    print(f"ok    {rows} preset rows, {expected} values each")
    return 0


if __name__ == "__main__":
    sys.exit(main())
