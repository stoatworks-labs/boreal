# Attributions

Boreal is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is generated — the master lists live in the `stoatworks-backend` repo and are
pushed out by `scripts/sync-attributions.py`. Edit it there, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### Audio analyser and host-clock vote — Stoatworks rosette

<https://github.com/stoatworks-labs/rosette>  
Licence: MIT  
Copyright: Stoatworks Labs

source/Audio.{h,cpp} is millpond's copy of rosette's analyser (itself from macroblock's), with its primed first frame, which brtest --onset checks. The host-clock unit vote in UpdateClock is rosette's.

### Source-plus-Over shape, presets, GL state and harness — Stoatworks downpour

<https://github.com/stoatworks-labs/downpour>  
Licence: MIT  
Copyright: Stoatworks Labs

One core registered as a source and an Over effect is downpour's shape; presets as an override with row 1 the defaults are graticule's; GLState.h is vectrix's (from resolume-scopes); PassBuffer, Diag, the CMake shape, the harness shape, the --pipe/--script format, tools/sweep.py and tools/verify.sh are tinsel's and millpond's.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl (third_party/ffgl in oxbow).

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

## Inspirations

What this set out to be. No code, assets or binaries from any of these were used or examined — the debt is to the idea.

### Auroral arcs as vortex sheets

Hallinan & Davis, Planet. Space Sci. 18, 1735 (1970), and Hallinan, J. Geophys. Res. 81, 3959 (1976); the regularised periodic vortex sheet and point insertion after R. Krasny, J. Fluid Mech. 167, 65 (1986). Implemented from the equations; the growth rate of the regularised kernel is derived in AGENTS.md.

## Standards and published specifications

What the implementation is measured against.

- **S. Knight, "Parallel electric fields", Planet. Space Sci. 21, 741 (1973)** — The current-voltage relation that sets the electrons' energy from the sheet's field-aligned current.
- **X. Fang et al., J. Geophys. Res. 113, A09311 (2008) and Geophys. Res. Lett. 37, L22106 (2010)** — The ionisation-rate parameterisations; both papers' coefficient tables are transcribed into Emission.cpp.
- **NRLMSIS 2.1 (Emmert et al., Earth and Space Science 8, 2021), via pymsis 0.13.0** — Evaluated once offline by tools/bake_atmosphere.py; the result is the table in source/physics/AtmosphereTable.cpp. Neither the model nor the wrapper ships.
- **NIST Atomic Spectra Database, and the rate coefficients as NCAR GLOW uses them** — Einstein A coefficients for the O I lines; quenching and energy-transfer rates and yields read from GLOW's gchem.f90 with their original attributions. Numbers only; no code is taken.
- **CIE 1931 colour-matching functions, CIE 1951 scotopic efficiency (CVRL tables) and CIE 191:2010 mesopic photometry** — Line chromaticities baked by tools/bake_colour.py; the Eye observer follows CIE 191.
- **Hansen & Travis (1974), Kasten & Young (1989) and van Rhijn (1921)** — Rayleigh optical depth, relative airmass and the airglow's brightening toward the horizon.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
