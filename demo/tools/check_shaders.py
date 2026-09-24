"""The demo's shaders must be the plugin's shaders, character for character.

    python3 demo/tools/check_shaders.py

Called from `tools/verify.sh`. Exit code 1 means a copy has drifted.

------------------------------------------------------------------- why

`demo/plugin.js` holds ten GLSL pieces and so does `source/Shaders.cpp`. That
is two copies of the same text, and two copies drift -- quietly, because a
demo that renders a *plausible* sky looks exactly like one that renders the
right one. The whole claim of the page is that it runs the plugin's own
shaders rather than something written to look similar, so the claim needs
something enforcing it. `brtest` drives the real plugin class and has no idea
the page exists, and `tools/glslc.sh` compiles the C++ copies and never looks
at the JS one.

------------------------------------------------------------------- what it does

1. Pulls each `R"( ... )"` body out of `source/Shaders.cpp` and the matching
   backtick literal out of `demo/plugin.js`, and compares them exactly -- no
   whitespace normalisation, no comment stripping. The comments in this repo
   carry the reasoning (why the airglow is integrated in the altitude variable,
   why the ray modulation puts the lost variance back), and a comment updated
   on one side only is exactly the drift worth catching. `kVersion`, a plain
   string, is compared too.

   The one transformation is a decode, not a normalisation: five comments quote
   an identifier in backticks (`km`, `down`, `uv`, `tanHalf`, `width`) and a
   backtick cannot appear raw inside a template literal, so plugin.js escapes
   it as \\`. This undoes that and REJECTS any other backslash on the JS side;
   there is none in the C++ bodies, so a second escape could only be somebody
   hiding a difference.

2. The NRLMSIS table: `demo/atmosphere.js` against
   `source/physics/AtmosphereTable.cpp`, every number. It is data, copied by
   `demo/tools/bake_atmosphere.py`, not ported.

3. The preset rows: `PRESET_ROWS` in plugin.js against `kPresets` in
   `source/Presets.h`, name for name and number for number. The page's
   `Preset` override and its constructor defaults (row 1) are both read from
   that table, so a drifted row is a drifted default.

------------------------------------------------------------------- what it cannot

Nothing here checks the PORTED half. `Sheet`, `Engine`, `Precipitation`,
`BuildTables`, `airAt`, `COMPONENTS` and every `...FromParam` in
demo/port.js are a hand translation of engine/Sheet.cpp, engine/Engine.cpp,
physics/Emission.cpp, physics/Atmosphere.cpp, physics/Optics.cpp and
Controls.cpp, and only a reader can tell whether they still agree. When you
change one of those, change it there too.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
# No __pycache__ beside the tools: it would sit untracked in demo/ after every verify.
sys.dont_write_bytecode = True

import bake_atmosphere  # noqa: E402

# JS constant, C++ symbol (all in source/Shaders.cpp).
SHADERS = [
    ("COMMON", "kCommon"),
    ("QUAD_VERTEX", "kQuadVertex"),
    ("SPLAT_VERTEX", "kSplatVertex"),
    ("SPLAT_FRAGMENT", "kSplatFragment"),
    ("UPDATE_FRAGMENT", "kUpdateFragment"),
    ("OCCUPANCY_FRAGMENT", "kOccupancyFragment"),
    ("MARCH_LIBRARY", "kMarchLibrary"),
    ("MARCH_FRAGMENT", "kMarchFragment"),
    ("ALLSKY_FRAGMENT", "kAllSkyFragment"),
    ("COMPOSITE_FRAGMENT", "kCompositeFragment"),
]


def read(*parts):
    with open(os.path.join(REPO, *parts)) as handle:
        return handle.read()


def from_cpp(source, symbol):
    match = re.search(r'const char\* const ' + symbol + r' = R"\((.*?)\)";', source, re.S)
    return None if match is None else match.group(1)


def from_js(source, name):
    match = re.search(r'^const ' + name + r' = `(.*?)`;$', source, re.S | re.M)
    if match is None:
        return None, None
    body = match.group(1)
    stray = re.search(r"\\(?!`)", body)
    if stray is not None:
        line = body[: stray.start()].count("\n") + 1
        return None, f"backslash that is not an escaped backtick, at line {line}"
    return body.replace("\\`", "`"), None


def first_difference(a, b):
    left, right = a.splitlines(), b.splitlines()
    for i in range(max(len(left), len(right))):
        x = left[i] if i < len(left) else "<missing>"
        y = right[i] if i < len(right) else "<missing>"
        if x != y:
            return i + 1, x, y
    return None


def check_shaders(cpp, js):
    problems = 0

    version_cpp = re.search(r'const char\* const kVersion = "(.*?)";', cpp)
    version_js = re.search(r"^const VERSION = '(.*?)';$", js, re.M)
    if version_cpp is None or version_js is None or version_cpp.group(1) != version_js.group(1):
        print("FAIL  VERSION does not match kVersion")
        problems += 1
    else:
        print(f"ok    {'VERSION':<20} matches kVersion")

    for name, symbol in SHADERS:
        cpp_text = from_cpp(cpp, symbol)
        js_text, complaint = from_js(js, name)
        if cpp_text is None:
            print(f"FAIL  {symbol} not found in source/Shaders.cpp")
            problems += 1
        elif complaint is not None:
            print(f"FAIL  {name} in demo/plugin.js has a {complaint}")
            problems += 1
        elif js_text is None:
            print(f"FAIL  {name} not found in demo/plugin.js")
            problems += 1
        elif cpp_text == js_text:
            print(f"ok    {name:<20} matches {symbol} ({len(cpp_text)} chars)")
        else:
            problems += 1
            print(f"FAIL  {name} has drifted from {symbol}")
            where = first_difference(cpp_text, js_text)
            if where:
                print(f"        first difference at line {where[0]}")
                print(f"          C++: {where[1]}")
                print(f"          js : {where[2]}")
    return problems


def js_numbers(block):
    block = "\n".join(line.split("//")[0] for line in block.splitlines())
    return [float(v) for v in re.findall(r"[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?", block)]


def check_atmosphere():
    rows, bottom, step, quiet, active = bake_atmosphere.parse(read("source", "physics", "AtmosphereTable.cpp"))
    try:
        js = read("demo", "atmosphere.js")
    except FileNotFoundError:
        print("FAIL  demo/atmosphere.js is missing -- run demo/tools/bake_atmosphere.py")
        return 1
    problems = 0
    header = {
        "TABLE_ROWS": float(rows),
        "TABLE_BOTTOM_KM": float(bottom),
        "TABLE_STEP_KM": float(step),
    }
    for key, want in header.items():
        m = re.search(r"export const " + key + r" = ([0-9.]+);", js)
        if m is None or float(m.group(1)) != want:
            print(f"FAIL  atmosphere.js {key} is not {want:g}")
            problems += 1
    for name, table in (("QUIET", quiet), ("ACTIVE", active)):
        m = re.search(r"export const " + name + r" = new Float64Array\(\[(.*?)\]\);", js, re.S)
        if m is None:
            print(f"FAIL  atmosphere.js has no {name}")
            problems += 1
            continue
        got = js_numbers(m.group(1))
        want = [float(f) for fields in table for f in fields[:5]]
        if got != want:
            bad = next((i for i, (a, b) in enumerate(zip(got, want)) if a != b), min(len(got), len(want)))
            print(f"FAIL  atmosphere.js {name} differs from the C++ at value {bad} (row {bad // 5})"
                  f" -- rerun demo/tools/bake_atmosphere.py")
            problems += 1
        else:
            print(f"ok    atmosphere {name:<9} matches kTable{name.title()} ({len(want) // 5} rows)")
    return problems


def check_presets(js):
    text = read("source", "Presets.h")
    want = []
    for m in re.finditer(r'\{\s*"([^"]+)",\s*\{(.*?)\}\s*\}', text, re.S):
        body = "\n".join(line.split("//")[0] for line in m.group(2).splitlines())
        want.append((m.group(1), [float(v.strip().rstrip("f")) for v in body.split(",") if v.strip()]))

    block = re.search(r"^const PRESET_ROWS = \[(.*?)^\];$", js, re.S | re.M)
    if block is None:
        print("FAIL  PRESET_ROWS not found in demo/plugin.js")
        return 1
    got = []
    for m in re.finditer(r"\['([^']+)',\s*\[(.*?)\]\]", block.group(1), re.S):
        got.append((m.group(1), js_numbers(m.group(2))))

    if got != want:
        for i in range(max(len(got), len(want))):
            a = got[i] if i < len(got) else None
            b = want[i] if i < len(want) else None
            if a != b:
                print(f"FAIL  preset row {i + 1} differs: C++ {b[0] if b else None!r}, js {a[0] if a else None!r}")
                break
        return 1
    print(f"ok    PRESET_ROWS          matches kPresets ({len(want)} rows)")
    return 0


def main():
    cpp = read("source", "Shaders.cpp")
    js = read("demo", "plugin.js")

    problems = check_shaders(cpp, js) + check_atmosphere() + check_presets(js)
    print()
    if problems:
        print(f"{problems} copy(ies) differ -- copy the C++ across, do not edit plugin.js by hand")
        return 1
    print(f"all {len(SHADERS) + 1} shader pieces, the atmosphere table and the preset rows are the plugin's")
    return 0


if __name__ == "__main__":
    sys.exit(main())
