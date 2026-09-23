# Attributions

boreal is built on other people's work. This file lists what that work is,
who did it, and what it is doing here.

> **Provisional.** Across the fleet this file is generated from master lists in
> `stoatworks-backend` by `scripts/sync-attributions.py`. boreal is not
> registered there yet, so this copy is hand-written. Register it before release
> — and note that the script's `--only` flag truncates the file rather than
> filtering it.

## Third-party code this project uses

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>
Licence: BSD-3-Clause
Copyright: FreeFrame

Vendored as a git submodule at `external/ffgl`, pinned to `b1afaf9`.

The plugin ABI itself. An FFGL plugin is defined by this SDK's headers — there
is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Windows only, from vcpkg, statically linked. macOS uses the system OpenGL
framework instead.

### zlib

<https://zlib.net>
Licence: zlib
Copyright: Jean-loup Gailly and Mark Adler

Ships with macOS. The offline harness links it to deflate its PNG output.
Nothing in the shipped plugin uses it.

## Data

### NRLMSIS 2.1

Emmert et al., "NRLMSIS 2.0: A whole-atmosphere empirical model of temperature
and neutral species densities", *Earth and Space Science* 8 (2021),
[doi:10.1029/2020EA001321](https://doi.org/10.1029/2020EA001321). A US Naval
Research Laboratory model (US Government work). Evaluated once, offline,
through [pymsis](https://github.com/SWxTREC/pymsis) 0.13.0 by
`tools/bake_atmosphere.py`; the result is the table in
`source/physics/AtmosphereTable.cpp`. Neither model nor wrapper ships.

### CIE colour-matching functions

The CIE 1931 2° colour-matching functions and the CIE 1951 scotopic luminous
efficiency, from the 1 nm tables at the [CVRL](http://www.cvrl.org) database
(`ciexyz31_1.csv`, `scvle_1.csv`), interpolated to the line wavelengths by
`tools/bake_colour.py`. Nine rows of numbers are baked into `Optics.cpp`.

### Atomic data

Einstein A coefficients for O I 557.7, 297.2, 295.8, 630.0, 636.4 and
639.2 nm from the NIST Atomic Spectra Database (Kramida, Ralchenko, Reader and
NIST ASD Team), retrieved 2026-09-23.

### Rate coefficients and yields as used in GLOW

Quenching rates for O(¹S) and O(¹D), the N₂(A) energy-transfer rates, the N₂⁺
first-negative branching and the O₂⁺ recombination yield of O(¹D), with the
original attributions GLOW gives them (Streit et al. 1976; Slanger et al.
1972; Slanger & Black 1981; Abreu et al. 1986; Piper et al. 1981; Borst & Zipf
1970; Shemansky & Broadfoot 1971), read from `gchem.f90` in
[NCAR/GLOW](https://github.com/NCAR/GLOW) (S. C. Solomon). Numbers only; no
code is taken.

## Work from elsewhere in the fleet

### millpond, rosette — the audio analyser and the clock

<https://github.com/stoatworks-labs/rosette>
Licence: MIT
Copyright: Stoatworks Labs

`source/Audio.{h,cpp}` is millpond's copy of rosette's analyser (itself from
macroblock's), with its primed first frame, which `brtest --onset` checks. The
host-clock unit vote in `UpdateClock` is rosette's.

### downpour, graticule, tinsel, vectrix, millpond

<https://github.com/stoatworks-labs/downpour>
Licence: MIT
Copyright: Stoatworks Labs

One core registered as a source and an Over effect (downpour); presets as an
override with row 1 the defaults (graticule); `GLState.h` (vectrix, from
resolume-scopes); `PassBuffer`, `Diag`, the CMake shape, the harness shape,
the `--pipe`/`--script` format, `tools/sweep.py` and `tools/verify.sh` (tinsel,
millpond).

## Method

Physics described in papers, implemented here from the equations:

- Auroral arcs as vortex sheets: Hallinan & Davis, *Planet. Space Sci.* 18,
  1735 (1970); Hallinan, *J. Geophys. Res.* 81, 3959 (1976).
- The δ-regularised periodic vortex sheet and point insertion: R. Krasny,
  *J. Fluid Mech.* 167, 65 (1986). The linear growth rate of the regularised
  periodic kernel is derived in AGENTS.md.
- The Knight relation: S. Knight, *Planet. Space Sci.* 21, 741 (1973).
- Energy deposition: X. Fang et al., *J. Geophys. Res.* 113, A09311 (2008),
  [doi:10.1029/2008JA013384](https://doi.org/10.1029/2008JA013384), and
  *Geophys. Res. Lett.* 37, L22106 (2010),
  [doi:10.1029/2010GL045406](https://doi.org/10.1029/2010GL045406). Both
  papers' coefficient tables are transcribed into `Emission.cpp`.
- Rayleigh optical depth: Hansen & Travis, *Space Sci. Rev.* 16, 527 (1974).
- Relative airmass: Kasten & Young, *Applied Optics* 28, 4735 (1989).
- The van Rhijn function (1921).
- Mesopic photometry: CIE 191:2010.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or
you would rather not be listed — open an issue and it will be fixed.
