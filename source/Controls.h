#pragma once

/**
    The host's parameters, and what they mean in physical units.

    Every ranged parameter the host sees is 0..1, because `SetParamInfo`
    clamps an `FF_TYPE_STANDARD` default into 0..1 before `SetParamRange` could
    widen it. The conversions live in Controls.cpp, one function per control.
    Option, boolean and event parameters hold the element value itself.

    Units throughout: km, seconds of sky time, keV, erg cm^-2 s^-1, degrees.
*/

namespace boreal
{
/**
    Parameter ids. **Append only**: `SetParamGroup` collapses runs of
    consecutive same-group ids, and saved compositions store parameters by index.

    The Over group sits AFTER the About block, on purpose: the source plugin
    declares ids 0 .. PT_SOURCE_COUNT-1 and the effect declares all of them, so
    the two share every id they have in common and the About block's
    static_assert holds for both.
*/
enum ParamId : unsigned int
{
	// -- Sky ---------------------------------------------------------------
	PT_HEMISPHERE = 0,
	PT_OVAL_DISTANCE,
	PT_DIP,
	PT_DECLINATION,
	PT_SPEED,
	PT_SEED,

	// -- Arcs --------------------------------------------------------------
	PT_ARCS,
	PT_ARC_SPACING,
	PT_SHEET_STRENGTH,
	PT_CURL_SIZE,
	PT_DISTURBANCE,
	PT_DRIFT,
	PT_SUBSTORM,
	PT_CALM,

	// -- Precipitation -----------------------------------------------------
	PT_ENERGY,
	PT_FLUX,
	PT_KNIGHT,
	PT_THICKNESS,
	PT_RAYS,

	// -- Atmosphere --------------------------------------------------------
	PT_ACTIVITY,
	PT_WIND,
	PT_AIRGLOW,
	PT_EXTINCTION,

	// -- Camera ------------------------------------------------------------
	PT_CAMERA,
	PT_LOOK_AZIMUTH,
	PT_LOOK_ELEVATION,
	PT_FOV,
	PT_ROLL,
	PT_EXPOSURE,
	PT_OBSERVER,
	PT_STARS,
	PT_STAR_MOTION,
	PT_HORIZON,
	PT_DETAIL,

	// -- Audio -------------------------------------------------------------
	PT_AUDIO,
	PT_AUDIO_SUBSTORM,
	PT_AUDIO_FLUX,

	// -- Preset ------------------------------------------------------------
	PT_PRESET,

	// -- The Stoatworks About block ------------------------------------------
	PT_ABOUT_TEXT,
	PT_ABOUT_BUTTON_1,
	PT_ABOUT_BUTTON_2,
	PT_ABOUT_BUTTON_3,
	PT_SOURCE_COUNT,

	// -- Over (the effect only) ----------------------------------------------
	PT_SKY_MASK = PT_SOURCE_COUNT,
	PT_MASK_THRESHOLD,
	PT_ILLUMINATION,
	PT_MIX,

	PT_COUNT
};

enum class Hemisphere
{
	Borealis = 0,
	Australis,
	Count
};

enum class CameraKind
{
	Rectilinear = 0,
	Fisheye,
	Count
};

enum class Observer
{
	Camera = 0,
	Eye,
	Count
};

enum class Horizon
{
	None = 0,
	Flat,
	Hills,
	Count
};

enum class SkyMask
{
	Everything = 0,
	Alpha,
	DarkAreas,
	Count
};

/// The ray march's raster as a fraction of the output.
constexpr float kDetailFractions[] = { 0.25f, 0.5f, 0.75f, 1.0f };
constexpr int kDetailCount         = 4;

//---------------------------------------------------------------------------
// The mappings.
//---------------------------------------------------------------------------

/// km poleward of the observer to the equatorward arc: -600 (behind, the oval
/// overhead and south of you) to +1400 (a band on the northern horizon).
float OvalDistanceFromParam( float v );
/// Magnetic inclination, 60 to 85 degrees.
float DipFromParam( float v );
/// Magnetic declination, -30 to +30 degrees (east of geographic north).
float DeclinationFromParam( float v );
/// Sky time per host second: 0 at the bottom (frozen), then 0.25x to 64x,
/// geometrically. 1x is at ParamFromSpeed( 1 ) and is real time.
float SpeedFromParam( float v );
float ParamFromSpeed( float speed );
/// 0..1 -> 0..9999, the seed.
unsigned SeedFromParam( float v );

/// Arcs is an integer parameter, 1..5 (real value).
/// km between neighbouring arcs, 15 to 400.
float ArcSpacingFromParam( float v );
/// The shear across the sheet (circulation per km), 0.1 to 6 km/s.
float SheetStrengthFromParam( float v );
/// delta, the regularisation length that sets the curl scale, 2 to 60 km.
float CurlSizeFromParam( float v );
/// km of seeded perturbation, 0 to 40, as v^2.
float DisturbanceFromParam( float v );
/// Eastward convection, -2 to +2 km/s.
float DriftFromParam( float v );

/// Base characteristic energy, 0.2 to 20 keV.
float EnergyFromParam( float v );
/// Energy flux at the base sheet strength, 0 then 0.05 to 50 erg cm^-2 s^-1.
float FluxFromParam( float v );
float ParamFromFlux( float flux );
/// The Knight coupling's exponent weight, 0..1 (linear).
float KnightFromParam( float v );
/// 1-sigma half-thickness of the curtain across the arc, 0.2 to 10 km.
float ThicknessFromParam( float v );
/// Depth of the ray modulation, 0..1 (linear).
float RaysFromParam( float v );

/// Activity, 0..1 (quiet sun .. solar maximum), linear.
float ActivityFromParam( float v );
/// Neutral wind at the red line's height, -300 to +300 m/s eastward. km/s.
float WindFromParam( float v );
/// Airglow 557.7 nm zenith column, 0 to 1000 R, as v^2. In kR.
float AirglowFromParam( float v );
float ParamFromAirglow( float kR );
/// Zenith optical depth scale, 0 to 3 (1 = a clean sea-level site).
float ExtinctionFromParam( float v );

/// -180..180 degrees, 0 = geographic north.
float LookAzimuthFromParam( float v );
/// -10 to 90 degrees.
float LookElevationFromParam( float v );
/// Field of view (the frame's height, rectilinear), 15 to 150 degrees.
float FovFromParam( float v );
/// -45..45 degrees.
float RollFromParam( float v );
/// Stops, -6 to +10.
float ExposureFromParam( float v );
/// Star brightness multiplier 0..1 (0 = no stars).
float StarsFromParam( float v );

float MaskThresholdFromParam( float v );
/// k in 1 + k E, 0 to 20.
float IlluminationFromParam( float v );

} // namespace boreal
