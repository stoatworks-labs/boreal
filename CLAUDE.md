# boreal

The aurora borealis and australis for Resolume Arena/Avenue, as two FFGL plugins
from one core: `SW Boreal` (`BR01`, source: the night sky) and `SW Boreal Over`
(`BR02`, effect: the aurora added to the clip as light). C++/GLSL, CMake MODULE →
two universal `.bundle`s (macOS) + Windows `.dll`s. MIT. Bundle ids
`com.stoatworks.ffgl.boreal` and `com.stoatworks.ffgl.boreal.over`.

Read `AGENTS.md` before touching the sheet, the emission tables, the update pass
or anything that decides what one lifetime per line means.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build`
- Install to Resolume: `cmake --install build` (never from `~/Projects`)
- Render the source: `./build/brtest --out /tmp/sky.png --frames 600 --substorm 300`
- Render the Over effect on the harness's card: `./build/brtest --over --out /tmp/o.png`
- List parameters: `./build/brtest --list` (`--over` for the effect's)
- Set anything by name: `./build/brtest --set "Preset=3" --set "Camera=1"`
- Film: `./build/brtest --film 1800 --size 1280x720 --script docs/demo.cues | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 60 -i - -c:v libx264 -pix_fmt yuv420p docs/demo.mp4`
- A clip through the effect: `ffmpeg -i in.mov -f rawvideo -pix_fmt rgba - | ./build/brtest --over --pipe --size WxH | ffmpeg …`
  A cue line is `frame  Parameter Name  value` (`#` starts a comment), in the same
  units as `--set`. Values interpolate linearly between a name's cues and hold
  before the first and after the last, so a step needs two cues a frame apart and
  a button press is three (0, 1, 0). Frame *n* is clocked at n / 60 s. An unknown
  name exits 2 before any frame; a partial frame at EOF ends the stream with exit
  0; a reader that hangs up ends `--pipe`/`--film` with exit 1 (SIGPIPE is
  ignored), never a silent 141.
- Rebake the atmosphere (needs `pip install pymsis`): `python3 tools/bake_atmosphere.py`

## Verify
- Everything: `tools/verify.sh` (~3 min: shaders through glslc, fresh universal
  build, both bundles through lipo/plist/codesign/oxbow probe+selftest, every
  check, the negative controls, the mutants, the sweep, the bench)
- **The sheet**: `--kh` (growth rate against the derived σ(k, δ)), `--invariants`
  (circulation, impulse, Hamiltonian over 10 min), `--knight`.
- **The atmosphere**: `--deposition` (Fang 2008 against Fang 2010 and the
  paper's figure), `--quench`, `--lifetime`.
- **The camera**: `--colour`, `--corona` (three rasters, 320x180 up), `--vanrhijn` (two
  rasters), `--extinction` (probes the shipped GLSL).
- **The plugin**: `--over-check`, `--determinism`, `--onset`, `--defaults`,
  `--names`, `--state`.
- **The checks can fail**: `--negative` (14 wrong models), `tools/mutate.sh`
  (one character of GLSL and engine).
- What CI runs: `--offline` (the checks that need no GL context, and their
  negative controls; says loudly that the pixel checks were not run) and
  `tools/glslc.sh`. verify.sh runs both too, plus the `--pipe` format checks.
- No dead controls: `python3 tools/sweep.py` (39 parameters, both plugins).
- Preset rows: `python3 tools/check_presets.py`.
- Cost: `--bench` (720p/1080p/4K), `--engine` (CPU per RK4 step at N = 512,
  2048, 4096).

## Notes
- **Units are km, seconds of sky time, keV, erg cm⁻² s⁻¹.** Every host
  parameter is 0..1 (Arcs and Seed are real integers) and mapped in
  `Controls.cpp`.
- **The engine runs one frame behind, on a worker.** A frame takes the previous
  job's snapshot and posts its own; the render always waits for the job, so the
  picture is a function of frame index and controls, not machine speed.
- **Energy and Flux are applied by the renderer**, not the engine, so Flux 0 is
  dark on the very next frame. The engine's nodes carry the Knight factors only.
- **One lifetime per line is the emission-weighted one**, τ = ∫pτ²/∫pτ. The
  production-weighted mean is dominated by the quenched bottom (0.3 s for red).
- **The discrete Hamiltonian includes the diagonal** (½Σ W² ln c). Leaving it
  out is an O(spacing) error that makes every insertion look like a leak.
- **Everything the populations touch is 32-bit float**: a per-frame decay of
  0.9999 is not representable in half.
- **A sampler bound to texture 0 is "unloadable"** to this driver, which logs
  "unit N" — N counts the program's active samplers, not texture units. The
  source binds a real texture to the unused clip unit.
- **Units 0-7 are bound by hand** and released with `unbindTextureUnits(n)`;
  the scoped bindings cannot unwind that many (millpond's trap).
- The Over group's ids come after the About block, so the source declares a
  prefix of the effect's parameters and the About static_assert holds for both.
- Presets are an OVERRIDE (`Effective()`); row 1 is the defaults.
- GLSL reserved words must not be identifiers; `verify.sh` greps for them.
- Randomness is PCG integer hashing on both sides, never `fract(sin(...))`.
- `boreal_core` is an OBJECT library: the registrations are file-scope
  constructors nothing references.
- Local repo only: no GitHub remote, no tag, not registered on the website.

## Not done yet
- Never loaded into Resolume (oxbow selftest only). No OpenFX port, no browser
  demo, no user guide. Never built on Windows.
- `StoatworksAbout.h` and `ATTRIBUTIONS.md` are provisional hand copies.

## Diagnostics

`source/Diag.{h,cpp}` is a log file only, with no crash handler (this runs
inside Resolume). It records which shader failed to compile and the GL
vendor/renderer.

    ~/Library/Logs/boreal/boreal.YYYY-MM-DD.log
