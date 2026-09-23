#pragma once

/**
    The camera's side: colour, extinction, the airglow layer's geometry.

    The shaders do all of this per pixel; this is the CPU reference the harness
    checks them against, and the source of every number the shaders are handed
    as a uniform. Nothing here is fitted to what the GPU prints.
*/
namespace boreal::optics
{
//---------------------------------------------------------------------------
// The spectral components. Each channel the ray march integrates is one or
// more lines with a fixed split; the composite colours each component by the
// CIE 1931 2-degree colour-matching functions at its wavelength.
//
// CMF and scotopic V'(lambda) values: CVRL (cvrl.org) ciexyz31_1.csv and
// scvle_1.csv, the 1 nm CIE tables, interpolated linearly to the wavelength
// (tools/bake_colour.py prints them).
//---------------------------------------------------------------------------
enum Channel
{
	kGreen = 0,///< O(1S) 557.7 nm (and the airglow)
	kRed,      ///< O(1D) 630.0 + 636.4 nm, split by their A values
	kBlue,     ///< N2+ 1N 427.8 nm, with 391.4 nm at 0.65/0.20 of it
	kPink,     ///< N2 1P, four visible band heads, equal weights (ASSUMED)
	kChannels
};

struct Component
{
	int channel;
	double weight;///< photons of this component per photon of the channel's column
	double nm;
	double xbar, ybar, zbar, vprime;
};

constexpr int kComponents = 9;
extern const Component kComponentTable[ kComponents ];

/// photons cm^-2 s^-1 (1 kR = 1e9) at `nm` -> radiance W m^-2 sr^-1.
double RadiancePerKR( double nm );

/// XYZ (cd m^-2, 683 lm/W) and scotopic luminance (scotopic cd m^-2, 1700
/// lm/W) of 1 kR of one component.
void ComponentXYZ( const Component& c, double xyz[ 3 ], double& scotopic );

/// IEC 61966-2-1 (sRGB) XYZ -> linear RGB, D65.
void XYZToLinearSRGB( const double xyz[ 3 ], double rgb[ 3 ] );

/// The gamut map: a colour with a negative component is moved toward the
/// grey of the same luminance (Y) until its lowest component is zero. Y is
/// linear in RGB and grey has the same Y, so luminance is kept exactly and
/// hue is kept in the sense of the line through white. `//= mirrored` in
/// Shaders.cpp, gamutMap().
void GamutMap( double rgb[ 3 ] );

//---------------------------------------------------------------------------
// Extinction
//---------------------------------------------------------------------------

/// Rayleigh optical depth of the whole atmosphere at sea level, Hansen &
/// Travis 1974 (Space Sci. Rev. 16, 527): 0.008569 l^-4 (1 + 0.0113 l^-2 + 0.00013 l^-4), l in
/// micrometres.
double RayleighDepth( double nm );

/// Aerosol, an Angstrom law: 0.05 (l / 550 nm)^-1.3. The 0.05 and 1.3 are a
/// clean continental site's order of magnitude (ASSUMED, not a site
/// measurement).
double AerosolDepth( double nm );

/// Relative airmass, Kasten & Young 1989 (Applied Optics 28, 4735):
/// 1 / ( cos z + 0.50572 (96.07995 - z)^-1.6364 ), z the zenith angle in degrees.
double KastenYoung( double zenithDegrees );

/// The transmission at `nm` looking at `zenithDegrees`, with the zenith optical
/// depth scaled by `scale` (the Extinction control).
double Transmission( double nm, double zenithDegrees, double scale );

//---------------------------------------------------------------------------
// The airglow layer
//---------------------------------------------------------------------------
constexpr double kAirglowKm    = 97.0;///< the 557.7 nm night airglow layer's peak
constexpr double kAirglowSigma = 3.5; ///< km; FWHM ~8 km (ASSUMED typical)

/// van Rhijn 1921: the brightening of a thin layer at height h with zenith
/// angle, 1 / sqrt( 1 - (R / (R + h))^2 sin^2 z ).
double VanRhijn( double zenithDegrees, double layerKm );

//---------------------------------------------------------------------------
// Mesopic vision, CIE 191:2010
//---------------------------------------------------------------------------
constexpr double kMesopicA   = 0.7670;
constexpr double kMesopicB   = 0.3334;
constexpr double kMesopicV0  = 683.0 / 1699.0;///< V(lambda_0) at 555.017 nm
constexpr double kMesopicLow = 0.005;         ///< cd m^-2: below, m = 0 (scotopic)
constexpr double kMesopicTop = 5.0;           ///< above, m = 1 (photopic)

/// The adaptation coefficient m and the mesopic luminance for a photopic and
/// scotopic luminance pair, by the standard's iteration.
double MesopicM( double photopic, double scotopic, double* mesopic = nullptr );

} // namespace boreal::optics
