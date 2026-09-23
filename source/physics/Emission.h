#pragma once

#include "physics/Atmosphere.h"

#include <vector>

/**
    Precipitation -> energy deposition -> excitation -> emission, per line.

    Every constant is named here with its source; AGENTS.md has the same list
    with what is confirmed and what is an assumption. Units: km for height,
    keV for energy, erg cm^-2 s^-1 for energy flux, cm^-3 s^-1 for volume
    rates, photons cm^-2 s^-1 for columns (1 R = 1e6 of those).
*/
namespace boreal::emission
{
//---------------------------------------------------------------------------
// Energy deposition
//---------------------------------------------------------------------------

/// Mean energy lost per ion pair, keV (Rees 1989; the 35 eV both Fang papers
/// normalise to).
constexpr double kIonPairKeV = 0.035;

/// keV in one erg.
constexpr double kKeVPerErg = 6.241509074e8;

/// Total ionisation rate, cm^-3 s^-1, for an isotropic MAXWELLIAN of
/// characteristic energy E0 (keV) and energy flux Q (erg cm^-2 s^-1): Fang et
/// al. 2008, JGR 113 A09311, equations (2), (4), (6), (7) and Table 1.
double IonisationFang2008( const atmosphere::Air& air, double e0KeV, double fluxErg );

/// The same for a MONOENERGETIC beam of E keV: Fang et al. 2010, GRL 37
/// L22106, equations (1), (3)-(5) and Table 1.
double IonisationFang2010Mono( const atmosphere::Air& air, double eKeV, double fluxErg );

/// Fang 2010 integrated over the Maxwellian of Fang 2010 eq. (6), in 400
/// logarithmic bins from 0.1 keV to 1 MeV -- the decomposition the 2010 paper
/// recommends. Independent of Fang 2008 except for the atmosphere, so the two
/// can be compared.
double IonisationFang2010Maxwellian( const atmosphere::Air& air, double e0KeV, double fluxErg );

//---------------------------------------------------------------------------
// Relative ionisation of each major species (Rees 1989, the 0.92 : 1 : 0.56
// relative cross-section weights for N2 : O2 : O).
//---------------------------------------------------------------------------
double FractionN2( const atmosphere::Air& air );
double FractionO( const atmosphere::Air& air );

//---------------------------------------------------------------------------
// Radiative rates: NIST Atomic Spectra Database (Kramida et al., ASD v5),
// O I, retrieved 2026-09-23.
//---------------------------------------------------------------------------
constexpr double kA5577 = 1.26;                                   ///< O(1S)->O(1D), 557.7339 nm
constexpr double kA1S   = 1.26 + 7.54e-2 + 2.42e-4;               ///< all of O(1S): + 297.2, 295.8 nm
constexpr double kA6300 = 5.63e-3 + 2.11e-5;                      ///< O(1D)->O(3P2), 630.0304 nm (M1 + E2)
constexpr double kA6364 = 1.82e-3 + 3.39e-6;                      ///< O(1D)->O(3P1), 636.3776 nm
constexpr double kA1D   = kA6300 + kA6364 + 8.60e-7;              ///< all of O(1D): + 639.2 nm

//---------------------------------------------------------------------------
// Quenching, cm^3 s^-1, as used in GLOW (Solomon et al. 1988; NCAR/GLOW
// gchem.f90), with GLOW's own attributions.
//---------------------------------------------------------------------------
double QuenchO1D_N2( double t );///< 2.0e-11 exp(107.8/T), Streit et al. 1976 (GLOW k8)
double QuenchO1D_O2( double t );///< 2.9e-11 exp(67.5/T),  Streit et al. 1976 (GLOW k9)
constexpr double kQuenchO1D_O = 3.0e-12;///< Abreu et al. 1986 (GLOW k27)
double QuenchO1S_O2( double t );///< 4.0e-12 exp(-865/T), Slanger et al. 1972 (GLOW k36)
constexpr double kQuenchO1S_O = 2.0e-14;///< Slanger & Black 1981 (GLOW k11)

/// N2(A) + O -> O(1S), the energy-transfer source of the green line, and the
/// competing N2(A) losses (GLOW k26, k29, A10; B18 is folded into kYield1S).
constexpr double kN2A_O   = 3.1e-11; ///< Piper et al. 1981b
constexpr double kN2A_O2  = 4.1e-12; ///< Piper et al. 1981a
constexpr double kA_N2A   = 0.77;    ///< Vegard-Kaplan, Shemansky 1969, s^-1

//---------------------------------------------------------------------------
// Yields.
//---------------------------------------------------------------------------

/// N2+ first negative: N2+(B) per N2 ionisation (GLOW B36, Borst & Zipf 1970)
/// and the (0,1) 427.8 and (0,0) 391.4 branches (GLOW B38, B37, Shemansky &
/// Broadfoot 1971).
constexpr double kN2BPerIonisation = 0.11;
constexpr double kBranch4278       = 0.20;
constexpr double kBranch3914       = 0.65;

/// O(1D) per ion pair: dissociative recombination of O2+ (GLOW B2 = 1.2 per
/// recombination, times an ASSUMED half of all ion pairs ending as O2+) plus
/// an ASSUMED 1.0 per ionisation of O (excitation by the secondaries that come
/// with it). Neither half is a measured yield; see AGENTS.md.
constexpr double kYield1D_DR = 0.6;
constexpr double kYield1D_O  = 1.0;

/// N2 first positive, the four visible band heads together, photons per N2
/// ionisation. ASSUMED -- 1.5x the 427.8 yield -- not taken from a source.
constexpr double kYield1P = 1.5 * kN2BPerIonisation * kBranch4278;

/// The green line's calibration target: 557.7 column per unit energy flux at
/// E0 = 5 keV in the quiet atmosphere, photons cm^-2 s^-1 per erg cm^-2 s^-1.
/// The textbook efficiency of about 1 kR per erg (e.g. Rees & Luckey 1974). The
/// O(1S) yield is SOLVED for, so this is a calibration, not a first principle.
constexpr double kGreenPerErgTarget = 1.0e9;

/// The O(1S) yield per N2(A) channel ion pair that meets that target. Solved
/// once on first use.
double Yield1S();

//---------------------------------------------------------------------------
// Per-height volume rates for one precipitation, cm^-3 s^-1.
//---------------------------------------------------------------------------
struct Volume
{
	double ionisation;///< q
	double prod1S, loss1S;///< O(1S) production, loss rate s^-1
	double prod1D, loss1D;///< O(1D)
	double e5577, e6300, e6364;///< steady-state emission
	double e4278, e3914, e1P;  ///< prompt
};

/// The rates at every table height (kHeights of them, 80..500 km by 1 km).
/// The ionisation is Fang 2008 scaled so that the table conserves energy:
/// sum over heights of q * 35 eV * 1 km = Q exactly (trapezoid). `rawFraction`
/// receives what Fang's own profile integrated to before that, as a fraction
/// of Q -- the part below 80 km, above 500 km, and the parameterisation's own
/// few-percent fit error.
std::vector< Volume > Profile( double activity, double e0KeV, double fluxErg, double* rawFraction = nullptr );

/// The trapezoid weight of table row `i` for a 1 km step (half at the ends).
double TrapezoidWeight( int i );

//---------------------------------------------------------------------------
// The tables the GPU samples.
//---------------------------------------------------------------------------
constexpr int kHeights     = 421;  ///< 80..500 km every 1 km
constexpr int kEnergies    = 64;   ///< ln E0 from ln 0.1 to ln 30 keV
constexpr double kLowKeV   = 0.1;
constexpr double kHighKeV  = 30.0;

double EnergyAt( int index );///< keV at table column `index`
double HeightAt( int index );///< km at table row `index`

struct Tables
{
	double activity = 0.0;

	/// kHeights x kEnergies x 4, height fastest: normalised per km vertical
	/// shapes of (O(1S) population, O(1D) population, N2+ 1N emission, N2 1P
	/// emission). Each integrates to 1 over 80-500 km.
	std::vector< float > shape;

	/// kEnergies x 4: O(1S) effective production column per erg, its
	/// emission-weighted lifetime tau_E = int p tau^2 / int p tau (s), and the
	/// same for O(1D). Their product is the true steady population column.
	std::vector< float > column;

	/// kEnergies x 4: 427.8, 391.4 and 1P column photons per erg; the fraction
	/// of Fang's deposition that fell inside 80..500 km before normalising.
	std::vector< float > prompt;
};

Tables BuildTables( double activity );

/// Where the ionisation peaks for E0, km, found from the 1 km table and
/// refined by a parabola through the top three nodes.
double PeakHeight( const std::vector< double >& profile );

} // namespace boreal::emission
