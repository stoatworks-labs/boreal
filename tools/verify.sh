#!/usr/bin/env bash
#
# Everything, in the order that fails fastest.
#
# The build is universal on purpose. An arm64-only bundle builds and tests
# perfectly well here and then fails to load in an Intel Resolume, and the
# build log calls it a success either way -- so the architecture is checked
# with lipo, never with the log.
#
#     tools/verify.sh [BUILD_DIR]
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-$REPO/build-verify}"

cd "$REPO"

step() { printf '\n\033[1m== %s\033[0m\n' "$1"; }
fail() { printf '\033[31mFAIL\033[0m %s\n' "$1"; exit 1; }

#---------------------------------------------------------------------------
# The GLSL reserved words, as identifiers.
#
# glslc catches these, but glslc is optional and a machine without it skips the
# whole shader step -- so the one trap most likely to be walked into gets its
# own grep that always runs. `sample` in particular is a natural name in a
# plugin that samples a surface, and `filter`, `patch` and `layout` are just as
# easy to reach for.
#
# It looks for a declaration, not a mention, so the words may still appear in
# prose above the code.
#---------------------------------------------------------------------------
reserved_words() {
	local words="patch sample input output filter common active half layout flat smooth noperspective"
	local bad=0 word

	for word in $words; do
		if grep -nE "(float|int|uint|bool|vec[234]|ivec[234]|uvec[234]|mat[234])[[:space:]]+$word[[:space:]]*[;=,)]" \
		            source/Shaders.cpp >/dev/null 2>&1; then
			printf '   "%s" is declared as an identifier and is a GLSL reserved word\n' "$word"
			grep -nE "(float|int|uint|bool|vec[234])[[:space:]]+$word[[:space:]]*[;=,)]" source/Shaders.cpp \
				| sed 's/^/      /'
			bad=$(( bad + 1 ))
		fi
	done

	[ "$bad" -eq 0 ] && printf '   none of the reserved words is used as an identifier\n'
	return "$bad"
}

#---------------------------------------------------------------------------
step "GLSL reserved words"
#---------------------------------------------------------------------------
reserved_words || fail "a GLSL reserved word is used as an identifier"

#---------------------------------------------------------------------------
step "Shaders"
#---------------------------------------------------------------------------
tools/glslc.sh || fail "a shader does not compile"

#---------------------------------------------------------------------------
step "Submodule"
#---------------------------------------------------------------------------
if [[ ! -f external/ffgl/CMakeLists.txt ]]; then
	fail "FFGL SDK missing -- run: git submodule update --init --recursive"
fi
pin="$(git -C external/ffgl rev-parse --short=7 HEAD)"
[[ "$pin" == "b1afaf9" ]] || fail "FFGL SDK at $pin, not the fleet's b1afaf9"
echo "ok   FFGL SDK pinned at $pin"

#---------------------------------------------------------------------------
step "Build (universal)"
#---------------------------------------------------------------------------
cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu)" >/dev/null
echo "ok   built"

#---------------------------------------------------------------------------
step "Bundles"
#---------------------------------------------------------------------------
# Two bundles, one per registration: FFGL resolves one plugMain per binary.
check_bundle() {
	local bundle="$1" executable="$2" identifier="$3" id="$4" name="$5" kind="$6"
	local binary="$bundle/Contents/MacOS/$executable"
	[[ -f "$binary" ]] || fail "no binary at $binary"

	# Universal, checked with lipo: an arm64-only bundle builds, tests and then
	# does not appear in an Intel Resolume.
	local arches
	arches="$(lipo -archs "$binary")"
	[[ "$arches" == *arm64* ]]  || fail "$executable: no arm64 slice (got: $arches)"
	[[ "$arches" == *x86_64* ]] || fail "$executable: no x86_64 slice (got: $arches)"

	# Captured, then matched from a herestring -- never `nm ... | grep -q`:
	# under pipefail the grep's early exit SIGPIPEs nm and fails the pipeline.
	local symbols
	symbols=$( nm -gU "$binary" 2>/dev/null || true )
	grep -q '_plugMain' <<<"$symbols" || fail "$executable: plugMain not exported"
	echo "ok   $executable: $arches, plugMain exported"

	local plist="$bundle/Contents/Info.plist"
	[[ -f "$plist" ]] || fail "no Info.plist in $bundle"
	read_plist() { /usr/libexec/PlistBuddy -c "Print :$1" "$plist" 2>/dev/null || true; }
	local declared
	declared="$( sed -n 's/^[[:space:]]*VERSION \([0-9.]*\)$/\1/p' CMakeLists.txt | head -1 )"
	[[ "$( read_plist CFBundleIdentifier )" == "$identifier" ]] || fail "$executable: bundle id is '$( read_plist CFBundleIdentifier )'"
	[[ "$( read_plist CFBundleExecutable )" == "$executable" ]] || fail "$executable: CFBundleExecutable is wrong"
	[[ "$( read_plist CFBundlePackageType )" == "BNDL" ]] || fail "$executable: CFBundlePackageType is not BNDL"
	[[ "$( read_plist CFBundleVersion )" == "$declared" ]] || fail "$executable: plist version != CMakeLists $declared"
	grep -q "versionFallback = \"v$declared\"" source/StoatworksAbout.h \
		|| fail "StoatworksAbout.h's versionFallback is not v$declared"
	echo "ok   $identifier, BNDL, v$declared -- plist, CMakeLists and About agree"

	# Ad hoc, which is what a local build gets; the release workflow signs and
	# notarises properly. This proves the bundle is well enough formed to sign.
	codesign --force --sign - --timestamp=none "$bundle" >/dev/null 2>&1 || fail "$bundle could not be ad-hoc signed"
	codesign --verify --deep --strict "$bundle" >/dev/null 2>&1 || fail "$bundle's ad-hoc signature does not verify"
	echo "ok   ad-hoc signed and verified"

	# What a host reads: id, name, type. The FFGL name field is char[16] and
	# not null-terminated, so only something reading it back as a host does
	# notices a truncation.
	local OXBOW="${OXBOW:-$HOME/Projects/resolume/oxbow/build/oxbow}"
	if [[ -x "$OXBOW" ]]; then
		local probe
		probe="$( "$OXBOW" probe "$bundle" 2>&1 || true )"
		printf '%s\n' "$probe" | sed -n '1,4p' | sed 's/^/   /'
		grep -q "id:          $id" <<<"$probe" || fail "oxbow did not read the id $id"
		grep -q "name:        $name\$" <<<"$probe" || fail "oxbow did not read the name '$name'"
		grep -q "type:        $kind" <<<"$probe" || fail "oxbow did not read the type as $kind"
		local selftest
		selftest="$( "$OXBOW" selftest "$bundle" 2>&1 || true )"
		grep -q "selftest:    PASS" <<<"$selftest" || fail "oxbow selftest failed on $bundle"
		echo "ok   a host reads $name $id $kind, and oxbow selftest renders 120 frames through it"
	else
		echo "   skipped: no oxbow at $OXBOW (set OXBOW=...)"
	fi
}

check_bundle "$BUILD/Boreal.bundle" "Boreal" "com.stoatworks.ffgl.boreal" BR01 "SW Boreal" source
check_bundle "$BUILD/Boreal Over.bundle" "Boreal Over" "com.stoatworks.ffgl.boreal.over" BR02 "SW Boreal Over" effect

#---------------------------------------------------------------------------
step "Checks"
#---------------------------------------------------------------------------
# Every claim the README makes, in the order the README makes them.
for check in kh invariants knight deposition quench lifetime colour corona vanrhijn extinction \
             over-check determinism onset defaults names state; do
	"$BUILD/brtest" --$check || fail "brtest --$check"
done
python3 tools/check_presets.py || fail "the preset table"

#---------------------------------------------------------------------------
step "Offline (what CI runs)"
#---------------------------------------------------------------------------
# The no-GL subset, exactly as CI runs it, so the selector cannot rot here
# while CI goes on passing.
"$BUILD/brtest" --offline >/dev/null || fail "brtest --offline"
echo "ok   brtest --offline"

#---------------------------------------------------------------------------
step "Pipe"
#---------------------------------------------------------------------------
# The fleet's --pipe frame format, which the video renders through. Two and a
# half frames in must be exactly two out and a clean exit -- a partial frame is
# the end of the stream, never a frame -- a cue naming no parameter must be
# refused, and a reader that hangs up must end the run with exit 1, not
# SIGPIPE's silent 141.
frame=$(( 64 * 36 * 4 ))
raw=$( mktemp ); cues=$( mktemp )
head -c $(( frame * 5 / 2 )) /dev/zero > "$raw"
out=$( mktemp ); status=0
"$BUILD/brtest" --over --pipe --size 64x36 < "$raw" > "$out" 2>/dev/null || status=$?
got=$( wc -c < "$out" | tr -d ' ' ); rm -f "$out"
[[ "$status" -eq 0 && "$got" == "$(( frame * 2 ))" ]] \
	|| fail "2.5 frames in gave $got bytes out (want $(( frame * 2 ))), exit $status"
echo "ok   2.5 frames in, exactly 2 frames out, clean exit"
# Read from a file, not a pipe: a writer killed by SIGPIPE would fail the
# pipeline whatever brtest did, and the refusal would pass for the wrong reason.
printf '0 No Such Control 0.5\n' > "$cues"
status=0
"$BUILD/brtest" --over --pipe --size 64x36 --script "$cues" < "$raw" >/dev/null 2>&1 || status=$?
[[ "$status" -eq 2 ]] || fail "a cue naming no parameter gave exit $status, not 2"
echo "ok   a cue naming no parameter is refused (exit 2)"
head -c $(( frame * 20 )) /dev/zero > "$raw"
set +e
"$BUILD/brtest" --over --pipe --size 64x36 < "$raw" 2>/dev/null | head -c 1 >/dev/null
status=${PIPESTATUS[0]}
set -e
[[ "$status" -eq 1 ]] || fail "a closed stdout gave exit $status, not 1"
set +e
"$BUILD/brtest" --film 20 --size 64x36 2>/dev/null | head -c 1 >/dev/null
status=${PIPESTATUS[0]}
set -e
[[ "$status" -eq 1 ]] || fail "--film into a closed stdout gave exit $status, not 1"
echo "ok   a closed stdout ends --pipe and --film with exit 1, not SIGPIPE"
rm -f "$raw" "$cues"

#---------------------------------------------------------------------------
step "Negative controls"
#---------------------------------------------------------------------------
# Every check above, against a model that is deliberately wrong, required to
# FAIL. A check that cannot fail is not a check.
"$BUILD/brtest" --negative || fail "a negative control went undetected"

#---------------------------------------------------------------------------
step "Mutants"
#---------------------------------------------------------------------------
# One character of the shipped GLSL and of the engine, changed: a check must fail.
tools/mutate.sh || fail "a mutant survived"

#---------------------------------------------------------------------------
step "Dead controls"
#---------------------------------------------------------------------------
# The only thing that catches a uniform whose name does not match the C++.
python3 tools/sweep.py --build "$(basename "$BUILD")" || fail "a dead control"

#---------------------------------------------------------------------------
step "Cost"
#---------------------------------------------------------------------------
"$BUILD/brtest" --bench
"$BUILD/brtest" --engine

printf '\n\033[32mall green\033[0m\n'
