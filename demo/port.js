/**
 * Boreal's CPU half, ported to JavaScript.
 *
 * **This file is a PORT, and nothing checks it but a reader.** It is a hand
 * translation, function for function, of:
 *
 *   source/Controls.cpp          every ...FromParam
 *   source/engine/Sheet.cpp      the periodic Birkhoff-Rott sheet: kernel, RK4,
 *                                insertion/removal, the growth rate, PCG
 *   source/engine/Engine.cpp     arcs, forcing, Substorm, Calm, the Knight relation
 *   source/physics/Atmosphere.cpp   the NRLMSIS interpolation (the table itself is
 *                                   data, copied by tools/bake_atmosphere.py)
 *   source/physics/Emission.cpp  Fang 2008, the rates, the yields, BuildTables()
 *   source/physics/Optics.cpp    the component table and each component's XYZ
 *
 * `brtest --kh`, `--invariants`, `--knight`, `--deposition`, `--quench` and
 * `--lifetime` check the C++ originals and have never heard of this file. When
 * one of those C++ files changes, change it here too.
 *
 * What is REDUCED here, and why: the sheet's node cap. The plugin caps the
 * sheet at 4096 nodes and runs the O(N^2) pair sum on up to four worker
 * threads, in double; at the cap an RK4 step is ~14 ms on an M4 Max in C++.
 * The page runs it on the main thread in one JavaScript thread, so the cap is
 * the caller's (plugin.js passes a smaller one). Everything the plugin does AT
 * its cap -- insertion refused, the curls drawn coarser, the refusals counted --
 * this does at the page's, by the same code path. Arithmetic is double, as it
 * is in the C++; the one place the C++ stores float (the Node) is rounded with
 * Math.fround.
 */

import { TABLE_ROWS, TABLE_BOTTOM_KM, TABLE_STEP_KM, QUIET, ACTIVE } from './atmosphere.js';

const PI = 3.14159265358979323846;
const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);
const clamp01 = (v) => clamp(v, 0, 1);

//===========================================================================
// Controls.cpp
//===========================================================================
const geometric = (v, low, high) => low * Math.pow(high / low, clamp01(v));
const linear = (v, low, high) => low + (high - low) * clamp01(v);
const kSpeedLow = 0.25;
const kSpeedHigh = 64.0;
const kFluxLow = 0.05;
const kFluxHigh = 50.0;

export const OvalDistanceFromParam = (v) => linear(v, -600.0, 1400.0);
export const DipFromParam = (v) => linear(v, 60.0, 85.0);
export const DeclinationFromParam = (v) => linear(v, -30.0, 30.0);
//Exactly zero at the bottom: a frozen sky is a thing an operator wants.
export const SpeedFromParam = (v) => (v <= 0.0 ? 0.0 : geometric(v, kSpeedLow, kSpeedHigh));
/** std::lround, which rounds half away from zero (Math.round rounds half up). */
export const lround = (v) => (v < 0 ? -Math.round(-v) : Math.round(v));
export const SeedFromParam = (v) => clamp(lround(v), 0, 9999) >>> 0;
export const ArcSpacingFromParam = (v) => geometric(v, 15.0, 400.0);
export const SheetStrengthFromParam = (v) => geometric(v, 0.1, 6.0);
export const CurlSizeFromParam = (v) => geometric(v, 2.0, 60.0);
export const DisturbanceFromParam = (v) => { const c = clamp01(v); return 40.0 * c * c; };
export const DriftFromParam = (v) => linear(v, -2.0, 2.0);
export const EnergyFromParam = (v) => geometric(v, 0.2, 20.0);
export const FluxFromParam = (v) => (v <= 0.0 ? 0.0 : geometric(v, kFluxLow, kFluxHigh));
export const KnightFromParam = (v) => clamp01(v);
export const ThicknessFromParam = (v) => geometric(v, 0.2, 10.0);
export const RaysFromParam = (v) => clamp01(v);
export const ActivityFromParam = (v) => clamp01(v);
export const WindFromParam = (v) => linear(v, -0.3, 0.3);
export const AirglowFromParam = (v) => { const c = clamp01(v); return 1.0 * c * c; };
export const ExtinctionFromParam = (v) => linear(v, 0.0, 3.0);
export const LookAzimuthFromParam = (v) => linear(v, -180.0, 180.0);
export const LookElevationFromParam = (v) => linear(v, -10.0, 90.0);
export const FovFromParam = (v) => linear(v, 15.0, 150.0);
export const RollFromParam = (v) => linear(v, -45.0, 45.0);
export const ExposureFromParam = (v) => linear(v, -6.0, 10.0);
export const StarsFromParam = (v) => clamp01(v);
export const MaskThresholdFromParam = (v) => clamp01(v);
export const IlluminationFromParam = (v) => linear(v, 0.0, 20.0);

//===========================================================================
// Sheet.cpp: PCG
//===========================================================================

/** The integer hash both sides share; mirrored in the shader's pcg(). */
export function PcgHash(v) {
  const state = (Math.imul(v >>> 0, 747796405) + 2891336453) >>> 0;
  const word = Math.imul(((state >>> ((state >>> 28) + 4)) ^ state) >>> 0, 277803737) >>> 0;
  return ((word >>> 22) ^ word) >>> 0;
}

const MASK64 = (1n << 64n) - 1n;

/** PCG32 (O'Neill 2014), 64-bit state in BigInt so it is the C++'s bit for bit. */
export class Pcg {
  constructor(seed = 0x853c49e6748fea9bn, stream = 0xda3e39cb94b95bdbn) {
    this.state = 0n;
    this.increment = ((BigInt(stream) << 1n) | 1n) & MASK64;
    this.Next();
    this.state = (this.state + BigInt(seed)) & MASK64;
    this.Next();
  }

  Next() {
    const old = this.state;
    this.state = (old * 6364136223846793005n + this.increment) & MASK64;
    const xorshifted = Number((((old >> 18n) ^ old) >> 27n) & 0xffffffffn) >>> 0;
    const rot = Number(old >> 59n);
    return ((xorshifted >>> rot) | (xorshifted << ((-rot) & 31))) >>> 0;
  }

  Uniform() {
    return this.Next() / 4294967296.0;
  }
}

//===========================================================================
// Sheet.cpp: the vortex sheet
//===========================================================================

/** One arc. x is UNWRAPPED (node N would be node 0 + L); g is per SEGMENT. */
function makeArc() {
  return { x: [], y: [], g: [], a: [], baseY: 0.0 };
}

export class Sheet {
  constructor() {
    this.arcs = [];
    this.settings = {
      period: 4096.0,
      delta: 8.0,
      driftU: 0.0,
      spacingMax: 4.0,
      cap: 4096,
      insert: true,
      remove: true,
      relaxTime: 0.0,
    };
    this.refused = 0;
    this._size = 0;
  }

  Count() {
    let n = 0;
    for (const arc of this.arcs) n += arc.x.length;
    return n;
  }

  _ensure(n) {
    if (this._size >= n) return;
    const size = Math.max(n, 64);
    const f = () => new Float64Array(size);
    this.w = f(); this.x0 = f(); this.y0 = f(); this.xs = f(); this.ys = f();
    this.cx = f(); this.sx = f(); this.ch = f(); this.sh = f();
    this.k1u = f(); this.k1v = f(); this.k2u = f(); this.k2v = f();
    this.k3u = f(); this.k3v = f(); this.k4u = f(); this.k4v = f();
    this._size = size;
  }

  /** Node weights, trapezoid of the segment circulations, flattened. */
  _weights() {
    let k = 0;
    for (const arc of this.arcs) {
      const n = arc.g.length;
      for (let i = 0; i < n; i += 1) this.w[k++] = 0.5 * (arc.g[(i + n - 1) % n] + arc.g[i]);
    }
  }

  /**
   * Velocities of every node. The per-node trig is taken once and the pair
   * terms by the addition formulas; Y is about the mean y so cosh stays near 1.
   * Each target's sum runs j = 0..n-1 in order, as the C++ does.
   */
  Velocity(x, y, n, u, v) {
    if (n === 0) return;
    const L = this.settings.period;
    const k = (2.0 * PI) / L;
    const c = 0.5 * (k * this.settings.delta) * (k * this.settings.delta);
    const scale = 1.0 / (2.0 * L);

    let origin = 0.0;
    for (let i = 0; i < n; i += 1) origin += y[i];
    origin /= n;

    const { cx, sx, ch, sh, w } = this;
    for (let i = 0; i < n; i += 1) {
      cx[i] = Math.cos(k * x[i]);
      sx[i] = Math.sin(k * x[i]);
      ch[i] = Math.cosh(k * (y[i] - origin));
      sh[i] = Math.sinh(k * (y[i] - origin));
    }
    for (let i = 0; i < n; i += 1) {
      const cxi = cx[i], sxi = sx[i], chi = ch[i], shi = sh[i];
      let su = 0.0, sv = 0.0;
      for (let j = 0; j < n; j += 1) {
        const cosX = cxi * cx[j] + sxi * sx[j];
        const sinX = sxi * cx[j] - cxi * sx[j];
        const coshY = chi * ch[j] - shi * sh[j];
        const sinhY = shi * ch[j] - chi * sh[j];
        const r = w[j] / (coshY - cosX + c);
        su += sinhY * r;
        sv += sinX * r;
      }
      u[i] = -scale * su + this.settings.driftU;
      v[i] = scale * sv;
    }
  }

  /** One classical RK4 step of h seconds. Circulations are constant within it. */
  Step(h) {
    const n = this.Count();
    if (n === 0) return;
    this._ensure(n);
    this._weights();
    const { x0, y0, xs, ys, k1u, k1v, k2u, k2v, k3u, k3v, k4u, k4v } = this;
    let q = 0;
    for (const arc of this.arcs) {
      for (let j = 0; j < arc.x.length; j += 1, q += 1) { x0[q] = arc.x[j]; y0[q] = arc.y[j]; }
    }
    const stage = (f, du, dv, ou, ov) => {
      for (let i = 0; i < n; i += 1) { xs[i] = x0[i] + f * du[i]; ys[i] = y0[i] + f * dv[i]; }
      this.Velocity(xs, ys, n, ou, ov);
    };
    this.Velocity(x0, y0, n, k1u, k1v);
    stage(0.5 * h, k1u, k1v, k2u, k2v);
    stage(0.5 * h, k2u, k2v, k3u, k3v);
    stage(h, k3u, k3v, k4u, k4v);

    let i = 0;
    for (const arc of this.arcs) {
      //The relaxation: the magnetosphere re-forming the arc, applied exactly
      //after the Birkhoff-Rott step. The one non-Hamiltonian term.
      const keep = this.settings.relaxTime > 0.0 ? Math.exp(-h / this.settings.relaxTime) : 1.0;
      for (let j = 0; j < arc.x.length; j += 1, i += 1) {
        arc.x[j] = x0[i] + (h / 6.0) * (k1u[i] + 2.0 * k2u[i] + 2.0 * k3u[i] + k4u[i]);
        const yy = y0[i] + (h / 6.0) * (k1v[i] + 2.0 * k2v[i] + 2.0 * k3v[i] + k4v[i]);
        arc.y[j] = arc.baseY + (yy - arc.baseY) * keep;
      }
    }
  }

  /** Insert where neighbours are too far apart (up to the cap), remove where much too close. */
  Refine() {
    const L = this.settings.period;
    let changed = 0;
    let total = this.Count();

    for (const arc of this.arcs) {
      if (this.settings.remove) {
        const tooClose = 0.25 * this.settings.spacingMax;
        const length = (a, b) => Math.hypot(arc.x[b] - arc.x[a], arc.y[b] - arc.y[a]);
        for (let i = 1; arc.x.length > 16 && i + 1 < arc.x.length;) {
          if (length(i - 1, i) < tooClose && length(i, i + 1) < tooClose) {
            //Merge node i's two segments: G_{i-1} + G_i, one rounding.
            arc.g[i - 1] += arc.g[i];
            arc.x.splice(i, 1);
            arc.y.splice(i, 1);
            arc.g.splice(i, 1);
            arc.a.splice(i, 1);
            total -= 1;
            changed += 1;
            i += 1;//never remove two neighbours in one pass
          } else {
            i += 1;
          }
        }
      }

      if (!this.settings.insert) continue;

      const nx = [], ny = [], ng = [], na = [];
      const n = arc.x.length;
      //Periodic neighbours, unwrapped: node n is node 0 moved one period east.
      const node = (i) => {
        const wraps = Math.floor(i / n);
        const j = i - wraps * n;
        return [arc.x[j] + wraps * L, arc.y[j]];
      };
      for (let i = 0; i < n; i += 1) {
        nx.push(arc.x[i]);
        ny.push(arc.y[i]);
        na.push(arc.a[i]);

        const [x0, y0] = node(i);
        const [x1, y1] = node(i + 1);
        if (Math.hypot(x1 - x0, y1 - y0) <= this.settings.spacingMax) {
          ng.push(arc.g[i]);
          continue;
        }
        if (total >= this.settings.cap) {
          //At the cap the sheet is left under-resolved: the segment keeps its
          //length and the curl it is part of is drawn coarser.
          this.refused += 1;
          ng.push(arc.g[i]);
          continue;
        }

        //A cubic through the four nodes around the gap in the circulation
        //coordinate, at the gap's circulation midpoint.
        const [xm, ym] = node(i - 1);
        const [x2, y2] = node(i + 2);
        const gPrev = arc.g[(i + n - 1) % n], gHere = arc.g[i], gNext = arc.g[(i + 1) % n];
        const s = [-gPrev, 0.0, gHere, gHere + gNext];
        const at = 0.5 * gHere;
        const w = [1, 1, 1, 1];
        for (let a = 0; a < 4; a += 1) {
          for (let b = 0; b < 4; b += 1) if (b !== a) w[a] *= (at - s[b]) / (s[a] - s[b]);
        }
        const px = w[0] * xm + w[1] * x0 + w[2] * x1 + w[3] * x2;
        const py = w[0] * ym + w[1] * y0 + w[2] * y1 + w[3] * y2;

        const half = 0.5 * arc.g[i];//exact
        ng.push(half);
        nx.push(px);
        ny.push(py);
        const a1 = i + 1 < n ? arc.a[i + 1] : arc.a[0] + L;
        na.push(0.5 * (arc.a[i] + a1));
        ng.push(half);
        total += 1;
        changed += 1;
      }
      arc.x = nx;
      arc.y = ny;
      arc.g = ng;
      arc.a = na;
    }
    return changed;
  }

  /** Keep every arc's x within one period of the origin, by whole periods. */
  Rewrap() {
    const L = this.settings.period;
    for (const arc of this.arcs) {
      if (arc.x.length === 0) continue;
      const shift = Math.floor((arc.x[0] + 0.5 * L) / L) * L;
      if (shift === 0.0) continue;
      for (let i = 0; i < arc.x.length; i += 1) arc.x[i] -= shift;
      for (let i = 0; i < arc.a.length; i += 1) arc.a[i] -= shift;
    }
  }
}

/** The wavenumber of fastest growth on the planar sheet, by golden section. */
export function FastestWavenumber(delta) {
  const g = (x) => x * Math.exp(-x) * (1.0 - Math.exp(-x));
  let a = 0.05, b = 5.0;
  const r = 0.5 * (Math.sqrt(5.0) - 1.0);
  for (let i = 0; i < 100; i += 1) {
    const c = b - r * (b - a), d = a + r * (b - a);
    if (g(c) > g(d)) b = d;
    else a = c;
  }
  return (0.5 * (a + b)) / delta;
}

//===========================================================================
// Engine.cpp
//===========================================================================
const kPeriod = 4096.0;
const kMaxStepsPerJob = 6;
const kMaxStep = 2.0;
const kSubstormBoost = 2.5;
const kSubstormDecay = 240.0;
const kSurgeSpeed = 1.5;
const kSurgeDecay = 180.0;
const kPolewardBrighten = 4.0;
const kBrightenDecay = 150.0;
const kForcingInterval = 12.0;
export const kPluginCap = 4096;

const spacingFor = (delta) => clamp(0.5 * delta, 2.0, 8.0);

export function StepFor(p) {
  return clamp((0.25 * p.delta) / Math.max(p.gamma * kSubstormBoost, 1e-3), 0.02, 1.0);
}

/**
 * The sky's dynamics. In the plugin this runs one frame behind on a worker
 * thread, and the render always waits for it, so the sequence of states is the
 * sequence of jobs. Here `Run` is called on the main thread with the same
 * one-frame lag (plugin.js keeps the previous snapshot), which gives the same
 * sequence without the thread.
 */
export class Engine {
  constructor() {
    this.sheet = new Sheet();
    this.snapshot = null;
    this.random = new Pcg();
    this.current = null;
    this.started = false;
    this.time = 0.0;
    this.boost = 1.0;
    this.surgeSpeed = 0.0;
    this.polewardGain = 1.0;
    this.nextForcing = 0.0;
    this.builtArcs = -1;
    this.builtSpacing = -1.0;
    this.builtSeed = 0;
  }

  perturb(arc, amplitude, wavelength, centre, width) {
    const k = (2.0 * PI) / wavelength;
    const phase = 2.0 * PI * this.random.Uniform();
    for (let i = 0; i < arc.x.length; i += 1) {
      let d = arc.x[i] - centre;
      d -= kPeriod * Math.floor(d / kPeriod + 0.5);
      const envelope = width > 0.0 ? Math.exp((-0.5 * d * d) / (width * width)) : 1.0;
      arc.y[i] += amplitude * envelope * Math.sin(k * arc.x[i] + phase);
    }
  }

  reset(p) {
    this.random = new Pcg(BigInt(p.seed >>> 0), 0x5eedn);
    this.sheet = new Sheet();
    this.sheet.settings.period = kPeriod;
    this.time = 0.0;
    this.boost = this.polewardGain = 1.0;
    this.surgeSpeed = 0.0;
    this.nextForcing = kForcingInterval;

    const arcs = clamp(p.arcs, 1, 5);
    const space = spacingFor(p.delta);
    const perArc = clamp(Math.trunc(kPeriod / space), 64, Math.max(64, Math.trunc(p.cap / arcs)));
    for (let a = 0; a < arcs; a += 1) {
      const arc = makeArc();
      arc.baseY = a * p.spacing;
      const offset = (a * 0.37 * kPeriod) / perArc;
      for (let i = 0; i < perArc; i += 1) {
        const x = -0.5 * kPeriod + offset + (kPeriod * i) / perArc;
        arc.x.push(x);
        arc.y.push(arc.baseY);
        arc.g.push((p.gamma * kPeriod) / perArc);
        arc.a.push(x);
      }
      const fastest = (2.0 * PI) / FastestWavenumber(p.delta);
      for (let m = 0; m < 4; m += 1) {
        this.perturb(arc, p.disturbance * 0.5, fastest * (0.7 + 0.6 * this.random.Uniform()), 0.0, 0.0);
      }
      this.perturb(arc, p.disturbance, 600.0 + 900.0 * this.random.Uniform(), 0.0, 0.0);
      this.sheet.arcs.push(arc);
    }

    this.builtArcs = arcs;
    this.builtSpacing = p.spacing;
    this.builtSeed = p.seed;
    this.current = { ...p };
  }

  /** Advance to job.skyTime with these parameters and events; return the snapshot. */
  Run(job) {
    const start = performance.now();
    const p = job.params;

    if (job.calm || this.builtArcs !== clamp(p.arcs, 1, 5) || this.builtSpacing !== p.spacing || this.builtSeed !== p.seed) {
      this.reset(p);
      this.time = job.skyTime;
      this.nextForcing = this.time + kForcingInterval;
    }

    //The sheet strength follows the control: every circulation scaled together.
    const wanted = p.gamma * this.boost;
    const have = this.current.gamma > 0.0 && this.started ? this.current.gamma * this.boost : wanted;
    if (have > 0.0 && Math.abs(wanted / have - 1.0) > 1e-12) {
      for (const arc of this.sheet.arcs) for (let i = 0; i < arc.g.length; i += 1) arc.g[i] *= wanted / have;
    }

    for (let s = 0; s < job.substorms; s += 1) {
      const jump = kSubstormBoost / this.boost;
      this.boost = kSubstormBoost;
      for (const arc of this.sheet.arcs) for (let i = 0; i < arc.g.length; i += 1) arc.g[i] *= jump;
      this.polewardGain = kPolewardBrighten;
      this.surgeSpeed = kSurgeSpeed;
      if (this.sheet.arcs.length) {
        this.perturb(this.sheet.arcs[this.sheet.arcs.length - 1], Math.max(0.4 * p.spacing, 15.0), 400.0,
          600.0 * (this.random.Uniform() - 0.5), 250.0);
      }
    }

    this.current = { ...p };
    this.started = true;

    const st = this.sheet.settings;
    st.period = kPeriod;
    st.delta = p.delta;
    st.spacingMax = spacingFor(p.delta);
    st.cap = p.cap;
    st.relaxTime = p.relaxTime;

    let h = StepFor(p);
    const gap = job.skyTime - this.time;
    let steps = gap > 0.0 ? Math.floor(gap / h + 1e-9) : 0;
    if (steps > kMaxStepsPerJob) {
      h = Math.min(gap / kMaxStepsPerJob, kMaxStep);
      steps = Math.floor(gap / h + 1e-9);
      steps = Math.min(steps, kMaxStepsPerJob);
    }

    for (let s = 0; s < steps; s += 1) {
      st.driftU = p.drift;
      this.sheet.Step(h);
      if (this.surgeSpeed > 0.0 && this.sheet.arcs.length) {
        const xs = this.sheet.arcs[this.sheet.arcs.length - 1].x;
        for (let i = 0; i < xs.length; i += 1) xs[i] -= this.surgeSpeed * h;
      }
      this.time += h;

      const relaxed = 1.0 + (this.boost - 1.0) * Math.exp(-h / kSubstormDecay);
      if (Math.abs(relaxed - this.boost) > 0.0) {
        for (const arc of this.sheet.arcs) for (let i = 0; i < arc.g.length; i += 1) arc.g[i] *= relaxed / this.boost;
        this.boost = relaxed;
      }
      this.surgeSpeed *= Math.exp(-h / kSurgeDecay);
      this.polewardGain = 1.0 + (this.polewardGain - 1.0) * Math.exp(-h / kBrightenDecay);

      if (p.forcing && p.disturbance > 0.0 && this.time >= this.nextForcing) {
        this.nextForcing += kForcingInterval;
        if (this.sheet.arcs.length) {
          const arc = this.sheet.arcs[this.random.Next() % this.sheet.arcs.length];
          const fastest = (2.0 * PI) / FastestWavenumber(p.delta);
          this.perturb(arc, 0.3 * p.disturbance, fastest * (0.7 + 0.6 * this.random.Uniform()),
            kPeriod * (this.random.Uniform() - 0.5), 300.0);
        }
      }

      this.sheet.Refine();
      this.sheet.Rewrap();
    }

    if (gap > 0.0 && steps > 0) this.time = Math.max(this.time, job.skyTime - h);

    const gains = this.sheet.arcs.map(() => 1.0);
    if (gains.length) gains[gains.length - 1] *= this.polewardGain;
    const snapshot = Precipitation(this.sheet, p, p.gamma * this.boost, gains);
    snapshot.skyTime = this.time;
    snapshot.steps = steps;
    snapshot.gammaNow = p.gamma * this.boost;
    snapshot.count = this.sheet.Count();
    snapshot.refused = this.sheet.refused;
    snapshot.cpuMs = performance.now() - start;
    this.snapshot = snapshot;
    return snapshot;
  }
}

/** Per node, the Knight relation. Six floats a node: x, y, flux, lnE0, label, gamma. */
export function Precipitation(sheet, p, gammaNow, arcGain) {
  const L = sheet.settings.period;
  const total = sheet.Count();
  const nodes = new Float32Array(total * 6);
  const arcStart = [];
  let k = 0;
  for (let a = 0; a < sheet.arcs.length; a += 1) {
    const arc = sheet.arcs[a];
    arcStart.push(k);
    const n = arc.x.length;
    const length = (i, j, wrap) => Math.hypot(arc.x[j] + wrap - arc.x[i], arc.y[j] - arc.y[i]);
    for (let i = 0; i < n; i += 1) {
      const prev = (i + n - 1) % n;
      const next = (i + 1) % n;
      const before = length(prev, i, i === 0 ? L : 0.0);
      const after = length(i, next, i + 1 === n ? L : 0.0);
      const w = 0.5 * (arc.g[prev] + arc.g[i]);
      const gamma = Math.abs(w) / Math.max(0.5 * (before + after), 1e-6);
      const ratio = clamp(gamma / Math.max(gammaNow, 1e-9), 0.05, 20.0);
      //The Knight relation: E0 goes as the sheet strength, the energy flux as its square.
      const e0 = Math.pow(ratio, p.knight);
      const flux = Math.pow(ratio, 2.0 * p.knight) * arcGain[a];

      let x = arc.x[i];
      x -= L * Math.floor(x / L + 0.5);
      const o = k * 6;
      nodes[o] = x;
      nodes[o + 1] = arc.y[i];
      nodes[o + 2] = flux;
      nodes[o + 3] = Math.log(e0);
      nodes[o + 4] = arc.a[i] - L * Math.floor(arc.a[i] / L + 0.5);
      nodes[o + 5] = gamma;
      k += 1;
    }
  }
  arcStart.push(k);
  return { nodes, arcStart };
}

//===========================================================================
// Atmosphere.cpp
//===========================================================================
const kBoltzmann = 1.380649e-16;
const kGravity0 = 980.665;
const kEarthRadiusKm = 6371.0;
export const kBottomKm = 80.0;

const logLerp = (a, b, f) => Math.exp(Math.log(Math.max(a, 1e-300)) * (1.0 - f) + Math.log(Math.max(b, 1e-300)) * f);

function mixRow(q, a, f) {
  return {
    n2: logLerp(q.n2, a.n2, f),
    o2: logLerp(q.o2, a.o2, f),
    o: logLerp(q.o, a.o, f),
    rho: logLerp(q.rho, a.rho, f),
    t: q.t + (a.t - q.t) * f,
  };
}

const row = (table, i) => ({ n2: table[i * 5], o2: table[i * 5 + 1], o: table[i * 5 + 2], t: table[i * 5 + 3], rho: table[i * 5 + 4] });

export function airAt(km, activity) {
  const f = clamp(activity, 0.0, 1.0);
  const pos = clamp((km - TABLE_BOTTOM_KM) / TABLE_STEP_KM, 0.0, TABLE_ROWS - 1);
  const i0 = Math.min(Math.trunc(pos), TABLE_ROWS - 2);
  const w = pos - i0;
  const a = mixRow(row(QUIET, i0), row(ACTIVE, i0), f);
  const b = mixRow(row(QUIET, i0 + 1), row(ACTIVE, i0 + 1), f);
  const r = mixRow(a, b, w);
  const n = r.n2 + r.o2 + r.o;
  const mass = r.rho / Math.max(n, 1e-300);
  const g = kGravity0 * Math.pow(kEarthRadiusKm / (kEarthRadiusKm + km), 2.0);
  return { n2: r.n2, o2: r.o2, o: r.o, t: r.t, rho: r.rho, scaleHeight: (kBoltzmann * r.t) / (mass * g) };
}

//===========================================================================
// Emission.cpp
//===========================================================================
const kIonPairKeV = 0.035;
const kKeVPerErg = 6.241509074e8;

const kP2008 = [
  [3.49979e-1, -6.18200e-2, -4.08124e-2, 1.65414e-2],
  [5.85425e-1, -5.00793e-2, 5.69309e-2, -4.02491e-3],
  [1.69692e-1, -2.58981e-2, 1.96822e-2, 1.20505e-3],
  [-1.22271e-1, -1.15532e-2, 5.37951e-6, 1.20189e-3],
  [1.57018e0, 2.87896e-1, -4.14857e-1, 5.18158e-2],
  [8.83195e-1, 4.31402e-2, -8.33599e-2, 1.02515e-2],
  [1.90953e0, -4.74704e-2, -1.80200e-1, 2.46652e-2],
  [-1.29566e0, -2.10952e-1, 2.73106e-1, -2.92752e-2],
];

export const kA5577 = 1.26;
const kA1S = 1.26 + 7.54e-2 + 2.42e-4;
export const kA6300 = 5.63e-3 + 2.11e-5;
export const kA6364 = 1.82e-3 + 3.39e-6;
const kA1D = kA6300 + kA6364 + 8.60e-7;
const kQuenchO1D_O = 3.0e-12;
const kQuenchO1S_O = 2.0e-14;
const kN2A_O = 3.1e-11;
const kN2A_O2 = 4.1e-12;
const kA_N2A = 0.77;
const kN2BPerIonisation = 0.11;
const kBranch4278 = 0.20;
const kBranch3914 = 0.65;
const kYield1D_DR = 0.6;
const kYield1D_O = 1.0;
const kYield1P = 1.5 * kN2BPerIonisation * kBranch4278;
const kGreenPerErgTarget = 1.0e9;

export const kHeights = 721;
export const kEnergies = 64;
export const kLowKeV = 0.1;
export const kHighKeV = 30.0;

const QuenchO1D_N2 = (t) => 2.0e-11 * Math.exp(107.8 / t);
const QuenchO1D_O2 = (t) => 2.9e-11 * Math.exp(67.5 / t);
const QuenchO1S_O2 = (t) => 4.0e-12 * Math.exp(-865.0 / t);

function coefficients(p, energy) {
  const l = Math.log(energy);
  return p.map((r) => Math.exp(r[0] + l * (r[1] + l * (r[2] + l * r[3]))));
}

function dissipation(c, y) {
  if (y <= 0.0) return 0.0;
  return c[0] * Math.pow(y, c[1]) * Math.exp(-c[2] * Math.pow(y, c[3]))
    + c[4] * Math.pow(y, c[5]) * Math.exp(-c[6] * Math.pow(y, c[7]));
}

function IonisationFang2008(air, e0, flux, c) {
  const y = Math.pow((air.rho * air.scaleHeight) / 4e-6, 0.606) / e0;
  const q0 = flux * kKeVPerErg;
  return ((q0 / (2.0 * kIonPairKeV)) * dissipation(c, y)) / air.scaleHeight;
}

const FractionN2 = (a) => (0.92 * a.n2) / (0.92 * a.n2 + a.o2 + 0.56 * a.o);
const FractionO = (a) => (0.56 * a.o) / (0.92 * a.n2 + a.o2 + 0.56 * a.o);
const lossO1S = (a) => kA1S + QuenchO1S_O2(a.t) * a.o2 + kQuenchO1S_O * a.o;
const lossO1D = (a) => kA1D + QuenchO1D_N2(a.t) * a.n2 + QuenchO1D_O2(a.t) * a.o2 + kQuenchO1D_O * a.o;
const n2aToO = (a) => { const toO = kN2A_O * a.o; return toO / (kA_N2A + toO + kN2A_O2 * a.o2); };
const TrapezoidWeight = (i) => (i === 0 || i === kHeights - 1 ? 0.5 : 1.0);
export const EnergyAt = (index) => kLowKeV * Math.pow(kHighKeV / kLowKeV, index / (kEnergies - 1));
const HeightAt = (index) => kBottomKm + index;

/** The air at every table height, for one activity. The C++ recomputes it per energy; same numbers. */
function airColumn(activity) {
  const air = new Array(kHeights);
  for (let i = 0; i < kHeights; i += 1) air[i] = airAt(HeightAt(i), activity);
  return air;
}

function profileWithYield(air, e0, flux, yield1S) {
  const rows = new Array(kHeights);
  const c = coefficients(kP2008, e0);
  let raw = 0.0;
  const q = new Float64Array(kHeights);
  for (let i = 0; i < kHeights; i += 1) {
    q[i] = IonisationFang2008(air[i], e0, flux, c);
    raw += q[i] * TrapezoidWeight(i);
  }
  const wanted = (flux * kKeVPerErg) / kIonPairKeV;
  const have = raw * 1e5;
  const scale = have > 0.0 ? wanted / have : 0.0;
  const rawFraction = wanted > 0.0 ? have / wanted : 0.0;

  for (let i = 0; i < kHeights; i += 1) {
    const a = air[i];
    const ionisation = q[i] * scale;
    const fN2 = FractionN2(a);
    const fO = FractionO(a);
    const r = { ionisation };
    r.prod1S = yield1S * ionisation * fN2 * n2aToO(a);
    r.loss1S = lossO1S(a);
    r.prod1D = ionisation * (kYield1D_DR + kYield1D_O * fO);
    r.loss1D = lossO1D(a);
    r.e5577 = (r.prod1S * kA5577) / r.loss1S;
    r.e4278 = ionisation * fN2 * kN2BPerIonisation * kBranch4278;
    r.e3914 = ionisation * fN2 * kN2BPerIonisation * kBranch3914;
    r.e1P = ionisation * fN2 * kYield1P;
    rows[i] = r;
  }
  return { rows, rawFraction };
}

let yield1S = 0.0;
function Yield1S() {
  if (yield1S === 0.0) {
    const { rows } = profileWithYield(airColumn(0.0), 5.0, 1.0, 1.0);
    let column = 0.0;
    for (let i = 0; i < kHeights; i += 1) column += rows[i].e5577 * TrapezoidWeight(i) * 1e5;
    yield1S = column > 0.0 ? kGreenPerErgTarget / column : 0.0;
  }
  return yield1S;
}

/**
 * The tables the GPU samples, as Emission.cpp's BuildTables(): per energy, the
 * normalised vertical shapes (height fastest), the O(1S)/O(1D) production
 * columns with their emission-weighted lifetimes, and the prompt columns.
 */
export function BuildTables(activity) {
  const shape = new Float32Array(kHeights * kEnergies * 4);
  const column = new Float32Array(kEnergies * 4);
  const prompt = new Float32Array(kEnergies * 4);
  const air = airColumn(activity);
  const y1S = Yield1S();

  for (let e = 0; e < kEnergies; e += 1) {
    const { rows, rawFraction } = profileWithYield(air, EnergyAt(e), 1.0, y1S);
    let p1S = 0, n1S = 0, m1S = 0, p1D = 0, n1D = 0, m1D = 0, c4278 = 0, c3914 = 0, c1P = 0;
    for (let i = 0; i < kHeights; i += 1) {
      const w = TrapezoidWeight(i) * 1e5;
      const r = rows[i];
      p1S += r.prod1S * w;
      n1S += (r.prod1S / r.loss1S) * w;
      m1S += (r.prod1S / (r.loss1S * r.loss1S)) * w;
      p1D += r.prod1D * w;
      n1D += (r.prod1D / r.loss1D) * w;
      m1D += (r.prod1D / (r.loss1D * r.loss1D)) * w;
      c4278 += r.e4278 * w;
      c3914 += r.e3914 * w;
      c1P += r.e1P * w;
    }
    for (let i = 0; i < kHeights; i += 1) {
      const r = rows[i];
      const o = (e * kHeights + i) * 4;
      const km = 1e5;
      shape[o] = n1S > 0 ? ((r.prod1S / r.loss1S) * km) / n1S : 0;
      shape[o + 1] = n1D > 0 ? ((r.prod1D / r.loss1D) * km) / n1D : 0;
      shape[o + 2] = c4278 > 0 ? (r.e4278 * km) / c4278 : 0;
      shape[o + 3] = c1P > 0 ? (r.e1P * km) / c1P : 0;
    }
    //One lifetime per line: the emission-weighted tau_E = int p tau^2 / int p tau.
    const tauS = n1S > 0 ? m1S / n1S : 0.0;
    const tauD = n1D > 0 ? m1D / n1D : 0.0;
    column[e * 4] = tauS > 0 ? n1S / tauS : 0;
    column[e * 4 + 1] = tauS;
    column[e * 4 + 2] = tauD > 0 ? n1D / tauD : 0;
    column[e * 4 + 3] = tauD;
    prompt[e * 4] = c4278;
    prompt[e * 4 + 1] = c3914;
    prompt[e * 4 + 2] = c1P;
    prompt[e * 4 + 3] = rawFraction;
  }
  return { activity, shape, column, prompt };
}

//===========================================================================
// Optics.cpp
//===========================================================================
const kPlanck = 6.62607015e-34;
const kLight = 299792458.0;
const k1PShare = 0.25;
export const kAirglowKm = 97.0;
export const kAirglowSigma = 3.5;

/** channel, weight, nm, xbar, ybar, zbar, V'. CVRL 1 nm tables, linearly interpolated. */
export const COMPONENTS = [
  [0, 1.0, 557.7339, 0.556814, 0.998586, 0.004631, 0.360785],
  [1, kA6300 / (kA6300 + kA6364), 630.0304, 0.641765, 0.264689, 0.000050, 0.003327],
  [1, kA6364 / (kA6300 + kA6364), 636.3776, 0.515405, 0.204909, 0.000027, 0.002002],
  [2, 1.0, 427.81, 0.256137, 0.009604, 1.244894, 0.174253],
  [2, kBranch3914 / kBranch4278, 391.44, 0.005012, 0.000142, 0.023696, 0.002719],
  [3, k1PShare, 654.5, 0.224714, 0.083932, 0.0, 0.000478],
  [3, k1PShare, 662.4, 0.142734, 0.052643, 0.0, 0.000261],
  [3, k1PShare, 670.5, 0.084650, 0.030981, 0.0, 0.000143],
  [3, k1PShare, 678.9, 0.050134, 0.018237, 0.0, 0.000077],
];

const RadiancePerKR = (nm) => ((1e13 / (4.0 * PI)) * kPlanck * kLight) / (nm * 1e-9);

/** The uniforms the march library takes for its nine components. */
export function componentUniforms() {
  const xyz = new Float32Array(27);
  const scot = new Float32Array(9);
  const nm = new Float32Array(9);
  const channel = new Int32Array(9);
  COMPONENTS.forEach(([ch, weight, wl, xbar, ybar, zbar, vprime], c) => {
    const radiance = RadiancePerKR(wl) * weight;
    xyz[c * 3] = 683.0 * xbar * radiance;
    xyz[c * 3 + 1] = 683.0 * ybar * radiance;
    xyz[c * 3 + 2] = 683.0 * zbar * radiance;
    scot[c] = 1700.0 * vprime * radiance;
    nm[c] = wl;
    channel[c] = ch;
  });
  return { xyz, scot, nm, channel };
}
