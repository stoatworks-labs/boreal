# boreal — for agents

The why behind the code. `CLAUDE.md` has the commands; this file has the
reasoning, the traps that were actually hit, and what is and is not known.
Built 2026-09-23 in one session from Allan's request: "boreal, a new resolume
plugin which creates effects like the aurora borealis and aurora australis".
The spec is `~/Projects/resolume/specs/SPEC-boreal.md`; the brief is
`~/Projects/resolume/specs/BRIEF.md`.

## The one idea

**An auroral arc is a sheet of charge drifting at E×B, and a charge sheet
drifting at E×B is a vortex sheet.** v = E×B/B² = ẑ×∇φ/B, so φ/B is a stream
function and the space charge is its vorticity; a thin arc obeys the 2-D Euler
equations of a vortex sheet (Hallinan & Davis 1970; Hallinan 1976), and the
sheet is Kelvin–Helmholtz unstable. Four more models follow it, in order:

1. **Dynamics** (`engine/Sheet.*`, `engine/Engine.*`): a δ-regularised periodic
   Birkhoff–Rott sheet, RK4 in double on a worker thread, in km and seconds of
   sky time.
2. **Precipitation** (`Engine::Precipitation`, the splat pass): the local sheet
   strength sets the field-aligned current, and by the Knight relation the
   electrons' characteristic energy and the energy flux.
3. **Atmosphere and emission** (`physics/*`, the update pass): energy
   deposition by Fang et al. over an NRLMSIS table; excitation, radiative rate
   and quenching per line; the O(¹S) and O(¹D) populations as state.
4. **Optics** (the march and composite passes): a ground camera on a spherical
   Earth looking through the 80–800 km shell along its own rays, with extinction
   and CIE colour.

Nothing is drawn with a noise function except the rays (below).

## The dynamics

### The kernel

Nodes carry circulation; the velocity at node i is

    u_i = -1/(2L) Σ_j W_j sinh Y / D,   v_i = +1/(2L) Σ_j W_j sin X / D
    D = cosh Y − cos X + c,   c = ½ (2π δ / L)²,   X, Y = 2π (Δx, Δy) / L

Krasny's (1986) periodic kernel, with the regularisation written so that near
a node D ≈ ½(2π/L)²(r² + δ²): δ is the planar regularisation length in km, and
it is what **Curl Size** sets. L = 4096 km, longer than any view (a curtain at
110 km is on the horizon 1185 km away), so an arc never ends in frame.

The pair sum is O(N²) and is the engine's whole cost. Each node's cos/sin of X
and cosh/sinh of Y are computed once and the pair terms by the addition
formulas, so a pair is a few multiplies and a divide; Y is taken about the mean
y so cosh stays near 1 and the subtraction cosh a cosh b − sinh a sinh b loses
nothing that matters in double (it would in float). Targets are split over
min(cores/2, 4) threads; each target's sum is taken in the same order however
it is split, so the result does not depend on the thread count.

### The growth rate, derived

Flat sheet, strength γ (circulation per km), perturbation η = a e^{iks},
ξ = b e^{iks}, k = 2πm/L. Linearising:

- **u′**: only (η₀ − η) enters to first order; the base denominator is
  1 − cos θ + c with θ = 2πd/L. Using the Poisson kernel,
  ∫₀^{2π} e^{−imθ} / (cosh μ − cos θ) dθ = 2π e^{−|m|μ} / sinh μ with
  cosh μ = 1 + c, so ∫ (1 − e^{−ikd}) / D dd = L (1 − e^{−mμ}) / sinh μ and
  u′ = −(πγ/L)(1 − e^{−mμ})/sinh μ · η.
- **v′**: only (ξ₀ − ξ) enters, through ∂_θ[sin θ/(cosh μ − cos θ)] =
  ∂_θ 2Σ e^{−nμ} sin nθ, whose m-th coefficient gives v′ = −(γk/2) e^{−mμ} ξ.

So ξ̇ = −Aη, η̇ = −Bξ and

    σ² = (γk/2)² e^{−mμ} (1 − e^{−mμ}) / (m sinh μ).

Limits: δ → 0 gives μ → 0, (1 − e^{−mμ})/(m sinh μ) → 1 and σ → γk/2, the
singular sheet. L → ∞ at fixed δ gives mμ → kδ and the planar
σ² = (γk/2)² e^{−kδ}(1 − e^{−kδ})/(kδ), whose fastest-growing wavenumber is
k* ≈ 1/δ (`FastestWavenumber`, golden section). That is why the curl scale is a
physical length and not a noise frequency. From a pure displacement the linear
solution is a cosh σt, which `--kh` fits exactly.

### Circulation lives on segments

G_j is the circulation of segment j (node j → j+1); a node's weight is the
trapezoid W_i = (G_{i−1} + G_i)/2. Insertion halves one G, which is exact in
binary, so the total is conserved to the last bit through any number of
insertions; removal sums two (one rounding). The new node is placed by a cubic
through the four surrounding nodes **in the circulation coordinate**, at the
gap's circulation midpoint.

### The arcs, the forcing, the events

N arcs (1–5), parallel at Arc Spacing, interact through the same sum. Left to
itself a sheet rolls up once and stays tangled; a display on a VJ's screen has
to keep going. So two terms stand for the magnetosphere, and are **off in every
conservation check**:

- **Relaxation**: y relaxes to the arc's base line with τ = 300 s, applied
  exactly after each RK4 step. The one non-Hamiltonian term.
- **Forcing**: every 12 s of sky time a small seeded wave packet near the
  fastest-growing wavelength is added to a random arc.

**Substorm** multiplies every circulation by 2.5 (decaying back over 240 s),
brightens the poleward arc ×4 (150 s), sends it west at 1.5 km/s (180 s) and
seeds a 400 km kink on it, which the same instability rolls up. **Calm**
rebuilds straight arcs with the seeded perturbation. **Drift** is a uniform
eastward velocity; it changes none of the invariants (checked).

The step is h = clamp(0.25 δ/(2.5γ), 0.02, 1) s. At most 6 steps a job; past
that the step grows to fit up to 2 s, and past that the sheet falls behind
the clock (the time is dropped, not owed, so a stall never becomes a lurch).
**The cap** is 4096 nodes. At the cap insertion stops and the curls are drawn
coarser; the engine counts refusals. The defaults sit at the cap after about a
minute of sky time, which is why the engine cost at the cap is the number that
matters.

### The worker

A frame takes the previous job's snapshot and posts its own. The render thread
always waits for the job before posting, so the worker overlaps the GPU and
the sequence of states is exactly the sequence of jobs: `--determinism` gets
bit-identical frames with the thread on. The cost is one frame of latency and,
at the cap, a render that waits when an RK4 step (14 ms) outlasts the GPU's
frame — the `--bench` "worst" column.

## Precipitation

Local sheet strength γ_i = |W_i| / (half the length of its two segments):
circulation per km of sheet, high where it is compressed in a curl, low where
it is stretched between curls. With ratio r = γ_i/γ₀ (clamped to 0.05..20):

    E0 = Energy × r^Knight,   Q = Flux × r^(2 Knight)

j∥ ∝ γ, the Knight relation j∥ = K ΔΦ makes E0 ∝ ΔΦ ∝ j∥, and the energy flux
j∥ΔΦ ∝ j∥². Knight is the exponent's weight, so 1 is the relation and 0 is
none. Energy and Flux are applied by the renderer (so Flux 0 is dark on the
next frame); the engine's nodes carry only r^K and r^2K.

Across the arc the flux is a Gaussian of Thickness σ, splatted segment by
segment into the footprint map; a curtain thinner than the map's texel is
widened to it and dimmed by the same factor, so the flux integrated across it
is kept.

**Rays are the one stochastic texture.** Eight PCG-seeded cosines of 0.5–5 km
wavelength along the sheet's Lagrangian label, drifting at 0.01–0.06 Hz,
modulate the flux as (1 − Rays) + Rays·s², s normalised so E[s²] = 1. They
stand for Alfvénic filamentation of the field-aligned current, which this
model does not resolve; the modulation moves light and makes none, and it
rides on the label so the rays travel with the sheet.

## The atmosphere

**NRLMSIS 2.1**, baked once by `tools/bake_atmosphere.py` (pymsis 0.13.0):
Tromsø 69.65°N 18.96°E, 2020-12-21 23:00 UT, 80–800 km every 2 km, at two
activity levels (F10.7 70/Ap 4 and F10.7 230/Ap 30). **Activity** interpolates
between them log-linearly in the densities and linearly in T; between nodes the
same in height. MSIS rather than US Standard 1976 because Activity has to move
the thermosphere, and the 1976 standard has one. Above ~500 km helium becomes
significant and the mean molecular mass (from ρ over the three majors) is
overstated; it enters only Fang's scale height, where nothing deposits.

**Deposition**: Fang et al. 2008 (Maxwellian, Table 1 transcribed from the page
image — the PDF's text layer drops the minus signs), eq. (2)/(4)/(6)/(7):
q = Q₀/(2Δε) f(y,E₀)/H, y = (ρH/4×10⁻⁶)^0.606/E₀, Δε = 35 eV. Independently
Fang et al. 2010 (monoenergetic, Table 1, y = (2/E)(ρH/6×10⁻⁶)^0.7) integrated
over the Maxwellian of their eq. (6) in 400 log bins, for the cross-check. The
table the GPU uses is renormalised so ∫ q·35 eV dh = Q exactly over 80–800 km;
Fang's own profile puts 97.0% (1 keV), 99.5% (5 keV), 100.4% (10 keV) there.

**Lines** (`physics/Emission.cpp`). Radiative rates from the NIST ASD (retrieved
2026-09-23): A(557.7) = 1.26 s⁻¹, O(¹S) total 1.336 s⁻¹ (τ = 0.749 s);
A(630.0) = 5.651×10⁻³, A(636.4) = 1.823×10⁻³, O(¹D) total 7.475×10⁻³ s⁻¹
(τ = 134 s). **The spec's "A ≈ 0.0091 total, τ ≈ 110 s" is an older value;
NIST's current one is used.** Quenching as used in GLOW (`gchem.f90`, with its
attributions): O(¹D)+N₂ 2.0×10⁻¹¹e^{107.8/T} and +O₂ 2.9×10⁻¹¹e^{67.5/T}
(Streit 1976), +O 3.0×10⁻¹² (Abreu 1986); O(¹S)+O₂ 4.0×10⁻¹²e^{−865/T}
(Slanger 1972), +O 2.0×10⁻¹⁴ (Slanger & Black 1981). Electron quenching of
O(¹D) is left out: there is no ionosphere model.

Yields — **what is sourced and what is assumed**:

| | value | status |
| --- | --- | --- |
| ionisation split N₂ : O₂ : O | 0.92 : 1 : 0.56 relative cross sections | Rees 1989, **from memory, not re-checked** |
| N₂⁺(B) per N₂ ionisation | 0.11 | GLOW B36 (Borst & Zipf 1970) |
| 427.8 / 391.4 branches | 0.20 / 0.65 | GLOW B38/B37 (Shemansky & Broadfoot 1971) |
| O(¹S) via N₂(A)+O | k 3.1×10⁻¹¹, vs A 0.77 s⁻¹ and O₂ 4.1×10⁻¹² | GLOW k26/k29/A10 |
| O(¹S) yield | solved so 557.7 = 1 kR per erg at 5 keV | **calibration** to the textbook ~1 kR/erg |
| O(¹D) yield | 0.6 per ion pair (O₂⁺ recombination, GLOW B2 = 1.2 × an assumed half) + 1.0 per O ionisation | **assumed** |
| N₂ 1P, four heads 654.5/662.4/670.5/678.9 nm | 1.5 × the 427.8 yield, equal shares | **assumed** (heads approximate) |
| airglow layer | 97 km, σ 3.5 km | **assumed** typical |

The consequences are measured: 630.0 peaks at 221 km and 557.7 at 104 km for
E₀ = 5 keV; the red line climbs with Activity (to ~270 km at solar maximum);
the pink/violet lower border of hard precipitation falls out, because O(¹S)
comes through O and O thins below 100 km while N₂ does not.

**Line trims (Green/Red/Blue/Pink gains) were NOT added.** Energy, Activity and
Flux reach every look the spec names — green arcs, red storms, the pink lower
border (Energy ≥ 10 keV), violet-tinged tops — without a non-physical control.

### Lifetime is state, and the factorisation

Per footprint texel the update pass carries (S₁S, S₁S lnE₀, S₁D, S₁D lnE₀),
advanced every frame by the exact solution of dS/dt = P − S/τ:
S′ = S e^{−Δt/τ} + Pτ(1 − e^{−Δt/τ}). The ln E₀ moment rides with it, so a
population remembers the energy it was excited at after the precipitation has
moved. 427.8 and 1P are prompt: read from the production map directly.

Emission at a 3-D point = A × S(footprint) × Φ(h; E₀), with Φ the normalised
steady-state vertical shape of the population at that E₀. That factorisation
is exact in steady state and approximate in transients: a real column decays
as a sum of exponentials, one per height, and the quenched bottom of the red
profile (τ ~ ms at 100 km) dies long before its top (τ ~ 100 s at 300 km).

**The single τ must be the one the EMISSION decays with**: τ_E = ∫pτ²/∫pτ, the
population-weighted mean lifetime. The obvious choice, the production-weighted
∫pτ/∫p, is dominated by the quenched bottom and gave O(¹D) 0.33 s at 6 keV —
the red would have vanished in a third of a second. With τ_E it is 41 s at
6 keV and longer for softer, higher precipitation. The production entry in the
table is then ∫pτ/τ_E, so the steady state S = P′τ_E = ∫pτ is exact.

**Neutral Wind** advects the red population semi-Lagrangianly, with a five-tap
diffusion of D = 0.5 km²/s (the order of O's molecular diffusion near 250 km;
**assumed**). Bilinear resampling adds numerical diffusion on top, most on the
coarse texels near the horizon. Transport is not checked by the harness.

## The optics

**The footprint map** is 1024², asinh-stretched on each axis about the
observer (a = 80 km, ±2000 km): a texel is ~constant in angle as seen from the
ground — 0.6 km overhead, ~8 km at 1000 km. Curtains thinner than a texel are
widened and dimmed (above).

**Geometry**: spherical Earth (6371 km), observer at the ground, every
altitude and intersection written without forming R² − R² (in float that
cancellation is kilometres near the horizon). Field lines are straight over
the volume with Dip and Declination; going DOWN a field line moves poleward in
either hemisphere, so a point's footprint is found by intersecting its field
line with the 110 km sphere, then azimuthal-equidistant (east, poleward) km.
**Australis** turns the poleward direction round while magnetic east stays
east: the (east, poleward) frame becomes left-handed, so the same dynamics
appear mirrored and the curls turn the other way; the substorm still surges
west.

**The march**: 40 km windows of ray; for each, the occupancy map's mip level
covering the footprints the window crosses (mean of a non-negative map is zero
exactly where every texel is) decides whether to skip; otherwise steps of at
most ~0.8 texel of footprint motion and 2 km of altitude, midpoint rule, up to
400 samples a ray. The airglow is integrated separately, in the altitude
variable (64-point Simpson of the Gaussian × dt/dh): its van Rhijn brightening
is that Jacobian, not a formula.

**Resolution**: the march runs at Detail × the output (default Half) and the
composite upsamples by joint bilateral, keyed on the occupancy where the ray
meets 110 km. Detail Full skips the upsample and is what the pixel checks use.

**Extinction** per spectral component and per star channel: Rayleigh (Hansen &
Travis 1974) + an Ångström aerosol 0.05(λ/550)^−1.3 (**assumed** clean site),
along the Kasten & Young 1989 airmass, scaled by Extinction.

**Colour**: each component through the CIE 1931 2° CMFs (CVRL, 1 nm, linearly
interpolated) weighted by photon energy → XYZ in cd m⁻² → linear sRGB (IEC
61966-2-1). Every line is outside sRGB (557.7 nm sits at xy 0.357, 0.640); the
gamut map moves an out-of-gamut colour toward the grey of the same luminance Y
until its lowest component is zero — Y kept exactly, hue kept along the line
through white. Then × 2^Exposure × 250 and the sRGB curve.

**Observer: Eye** applies CIE 191:2010 mesopic photometry per pixel (a = 0.7670,
b = 0.3334, V(λ₀) = 683/1699, m = 0 below 0.005 cd m⁻², 1 above 5): luminance
from the mesopic mix of photopic and scotopic (V′ from CVRL), chroma faded by
m. A faint display is below 0.005 cd m⁻², so the Eye sees it grey; 630 nm has
V′ = 0.0033 against V = 0.265, so the red nearly vanishes. The per-pixel
adaptation is a simplification: the standard adapts to the visual field.

**Stars**: cube-mapped cells on the celestial sphere, at most one star per cell
(30%), magnitudes from N(<m) ∝ 10^{0.5m} to m = 6.5, a mag-0 star 2.54×10⁻⁶ lux
(Allen), PSF σ 0.6 px, extinct per channel at 610/550/465 nm. Star Motion turns
the sky at the sidereal rate about a pole at ±69.65° (the atmosphere table's
latitude; Australis uses the mirror latitude, an Antarctic-coast site).

**Horizon**: None (below the horizon is transparent — the source composites),
Flat, or Hills (5-octave PCG value noise in azimuth). The ground is snow of
albedo 0.8 lit by the all-sky pass, so it takes the display's colour.

**Over**: the aurora is ADDED as light. Sky Mask: Everything, Alpha (the clip's
transparent sky becomes the night sky; opaque pixels are the clip exactly),
Dark Areas (luma below Mask Threshold, smoothstep ±0.05). Illumination
multiplies the clip by 1 + k E, E the cosine-weighted mean of a 32×32 all-sky
march (the same function, mipmapped to 1×1). Where nothing is added and
nothing lit, the clip is returned bit-exact — decode/encode is never applied
to a pixel that does not change.

## The traps

Ordered by how much time they cost.

**The discrete Hamiltonian left out the diagonal.** −(1/4π)Σ_{i<j} W_iW_j ln D
omits ½Σ W_i² ln c, which is constant between insertions (the dynamics never see
it) but is part of the double integral's quadrature. Without it every insertion
changed H by an O(spacing) amount: 0.9% over 10 minutes, halving the spacing
halved it — first order, which is what gave it away. With it, 3.3×10⁻⁶, and
halving the spacing shrinks it 3.2×. A 25-minute run at 28,000 nodes was spent
before the check was made cheap enough to iterate on.

**One lifetime per line: the production-weighted mean is the wrong one** (see
"Lifetime is state"). `--lifetime` would have passed either way — it checks the
integrator against the table's τ — so this was found by printing the number
and seeing 0.333 s for a line with a 134 s radiative lifetime. Read the numbers
a check prints, not only its verdict.

**Cutting the shell at 500 km drew a ceiling across the sky.** At solar maximum
the red line is still 6–14% of its peak at 500 km; a hard top made a straight
edge in every view that looked up far enough. The table, the emission tables
and the shell go to 800 km.

**A sampler bound to texture 0 is "unloadable" to Apple's GL**, which logs
"unit 3 ... using zero texture" once. The "unit" is the program's Nth active
sampler, not a texture unit: it was the source's unused clip sampler (unit 5),
not the shapes table on texture unit 3, which cost an hour of checking the
wrong texture. The source binds a real texture there.

**Insertion needs the circulation coordinate.** A uniform-parameter
(Catmull-Rom) cubic is first-order across a segment whose neighbour was halved
earlier; the cubic is now in the (non-uniform) circulation coordinate. This
alone did not fix the Hamiltonian (the diagonal did), but it is the right cubic.

**The onset check first triggered in a quiet moment**, where an unprimed
analyser is barely deafened, and the negative control passed. It now triggers
just after a hit, which is exactly the fleet's original failure.

**A literal `\n` written through Python became a newline inside a C string**
and broke the harness build while the old binary kept running — the stale
binary's output looked like the new code's. Check the build before reading a
result.

**Resolume parameter names are 16 characters.** "Curtain Thickness" (the
spec's) is 17; the control is **Thickness**, in the Precipitation group.

Carried from the fleet and respected here: the 3+-unit scoped-binding unwind
(`unbindTextureUnits`, units bound by hand), no geometry shader, no half floats
for anything fed back, `ScopedFBOBinding` does not restore the viewport,
allocate before binding, `FFGLFBO::Release` leaks (PassBuffer), the OBJECT
library, `SetTextParameter` for About, integer hashing only, the clock-unit vote
and the settle-jump, primed onsets.

## Would this hold on another rasteriser, at another raster?

Every numeric check, where its tolerance comes from, and the raster it runs at.
CI's raster is 320x180; every pixel check either runs there or says why its
raster does not matter. CI itself runs only `brtest --offline` (the rows marked
CPU, plus their negative controls) and glslc: a macOS runner has no accelerated
GL. The pixel rows run in `tools/verify.sh`, on this Mac's GPU.

| check | bound | why that number | raster / rasteriser |
| --- | --- | --- | --- |
| `--kh` | σ(3·RK4 gap + (N/m)e^{−(N−2m)μ} + (kA)²) | RK4's growth factor against e^z; the derivative kernel's aliasing; nonlinearity | CPU double: neither |
| `--invariants` circulation | 1e-12 relative | halving exact, merge one rounding | CPU |
| impulse between insertions | 1e-11 of Γ×100 km | antisymmetric kernel; RK4 keeps linear invariants | CPU |
| impulse through insertions | 1e-5 of scale | the cubic's departure from the chord | CPU |
| Hamiltonian, fixed nodes | 1e-6 and h-ratio 8..32 | RK4 is h⁴: 16 | CPU |
| Hamiltonian, insertions | 1e-4 and ≥ 2.5× per halving of spacing | O(h²) quadrature predicts 4 | CPU |
| `--knight` | 1e-6 | float storage of ln E₀ | CPU |
| `--deposition` peaks | 3 km | Fig. 3a read to ±1.5 km, 1 km table, a different MSIS atmosphere | CPU |
| `--deposition` energy | 1e-12; Fang's raw ±5% | trapezoid identity; the paper's stated accuracy | CPU |
| `--quench` | 1e-5 | the GPU texture is float32 (read back) | 64×64; texture memory, not rasterisation: raster-free |
| `--lifetime` | 3e-7 per frame | a float exp() of a float ratio each frame, compounding | 320×180; any GPU with IEEE float exp to a few ulp |
| `--colour` | 2e-4 in xy | float XYZ | 256×144; one raster suffices (the chromaticity of a flat field) |
| `--corona` | 1 px | the VP from ~9 principal axes; float geometry is 1e-3 px | 320×180, 640×360 and 1280×720 (misses 0.07–0.32 px); a least-squares meeting of whole streaks, so the bound does not coarsen with the pixel |
| `--vanrhijn` | layer-thickness correction + 2e-5 relative | the Gaussian layer vs a sheet, computed in double | 257² and 513² fisheye |
| `--extinction` | 1e-5 airmass, 1e-4 transmission | float pow/exp | probe of the shipped GLSL |
| `--over-check` | bit-exact | the clip is returned untouched | 320×180; exact on any GPU (no arithmetic on the path) |
| `--determinism` | bit-exact | same machine, same driver | 320×180; across machines NOT claimed |
| `--onset` | exactly 1 | deterministic feed | 160×90; the analyser is CPU, the render only clocks it |
| `--defaults`, `--names` | exact | the preset table and the 16-byte name field | CPU (`--offline`) |
| `--state` | exact | the GL state the host hands over is the state it gets back | 320×180; state, not pixels: raster-free |
| `--quadrants` | footprints in every quadrant of the sky | the half-sky defect it was written for | square fisheye; a count, not a tolerance |

Physics checks run in km/s/nm on the CPU and are raster-independent by
construction; the pixel checks run at 320×180 or say why the raster is moot.

**Resize mid-run.** Every buffer that carries state across frames (the
production map, the two lifetime states, occupancy, the all-sky) is sized by
`kMapSize` / `kAllSkySize`, never by the host's raster; only the march buffer,
which is rewritten whole every frame, follows the raster. So a resize cannot
clear the previous frame (photofinish's bug), and no resize-mid-run check was
added: there is nothing raster-sized for it to catch. Revisit if a state
buffer ever becomes raster-sized.

### The recorded mutation

`tools/mutate.sh` (run by verify.sh) changes one character of shipped code and
requires a named check to fail. The GLSL mutants, and what caught them:

- `float keep = exp( -Dt / tau );` → `exp( -Dt * tau )` in the update pass:
  caught by `--lifetime` (the decay no longer matches the exact integrator).
- Kasten & Young's `0.50572` → `0.50672` in the march's airmass: caught by
  `--extinction`, which probes the shipped GLSL, not a copy.
- (engine) `sin( a - b )` expanded with the wrong sign in Sheet.cpp: caught by
  `--kh`.

This proves the harness drives the shaders the plugin ships, not a second copy.

## Shape of the code

    source/engine/Sheet.*      the vortex sheet: kernel, RK4, insertion, invariants, σ(k,δ), PCG
    source/engine/Engine.*     arcs, forcing, events, the worker, the Knight relation
    source/physics/Atmosphere.* + AtmosphereTable.cpp   NRLMSIS, baked
    source/physics/Emission.*  Fang 2008/2010, rates, yields, the GPU tables
    source/physics/Optics.*    CMFs, sRGB, gamut map, extinction, van Rhijn, mesopic
    source/Shaders.*           kCommon (library) + six passes
    source/Boreal.*            the plugin: parameters, clock, audio, buffers, passes
    source/SourcePlugin.cpp, EffectPlugin.cpp   the two registrations
    source/Presets.h           seven rows, row 1 the defaults
    tools/brtest/              the harness
    tools/sweep.py, check_presets.py, mutate.sh, verify.sh
    tools/bake_atmosphere.py, bake_colour.py    how the data were made

## Decisions taken without asking

- **Two bundles** (`Boreal.bundle`, `Boreal Over.bundle`), downpour's shape;
  the effect's bundle id is `com.stoatworks.ffgl.boreal.over`.
- **Over parameters after the About block**, so the source declares a prefix of
  the effect's ids. They appear last in the effect's inspector.
- **Seven presets**: row 1 "Boreal" (the defaults) and the spec's six. Events
  cannot be preset; "Corona" is a substorm's look, the Substorm press is the
  operator's.
- **Defaults to be seen**: two arcs 250 km north, 25 erg, 4× sky time, looking
  north 25° up. 1× is on the Speed slider (and honest).
- **NIST A values over the spec's 0.0091**; **Thickness** over "Curtain
  Thickness" (length); **no line trims**.
- **Provisional About/ATTRIBUTIONS** hand copies with `guide = ""`, as the rest
  of the unreleased tranche.
- The dynamics papers (Hallinan & Davis 1970, Hallinan 1976, Knight 1973,
  Krasny 1986) are cited from memory, not re-fetched; the Fang papers, NIST,
  GLOW and CVRL were read this session.

## What is genuinely verified, and what is assumed

Verified on this machine (M4 Max, macOS 26): everything in the README's Status
table, `tools/verify.sh` green (both bundles universal, oxbow probe reads
`SW Boreal`/`BR01`/source and `SW Boreal Over`/`BR02`/effect, oxbow selftest
renders 120 frames through each).

Assumed, or not done:

- **Never loaded into Resolume.** Unknown there: how 42/46 parameters in eight
  groups present, the clock unit, the FFT bins, whether events arrive as 1 then 0.
- **Windows never built or run.** RG32F additive blending, RGBA32F filtering and
  the worker thread are standard but untested there.
- **The yields marked assumed above**, and so the absolute brightness of red
  and pink against green. The shapes and the altitudes are physics; the
  ratios between lines are partly calibration.
- **The factorisation** (footprint × one shape per line) is exact only in
  steady state.
- **Transport of the red population** (wind, diffusion) is not checked.
- **Cost**: at the default Detail the median frame is 6–12 ms at 1080p on this Mac
  (shared with other builds while measured), 15–32 ms at 4K — use Quarter at 4K.
  At the node cap an RK4 step is 14 ms, which the render waits for about once
  every 12 frames at 4×.
- **The GPU-less CI runner** has never run the harness; CI only builds.
