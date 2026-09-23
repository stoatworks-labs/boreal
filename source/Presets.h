#pragma once

/**
    Factory presets: a whole display an operator can reach in one gesture.

    **Presets are an OVERRIDE, not a write** (graticule's model). Resolume does
    not consume value events, so while the dropdown is on anything but Custom
    the row's values are laid over the operator's at read time, in
    `BorealPlugin::Effective()`, and the inspector is, for those columns, not
    the truth. Element 0 of the dropdown is Custom and is not in this table.

    **Row 1 is the constructor's defaults**, and `brtest --defaults` fails when
    the two drift apart.

    A preset covers the sky, the arcs, the precipitation, the atmosphere and
    the camera. It stays off the Seed (which sky, not what kind), Detail (the
    machine's budget), the audio amounts and the Over group (how the clip is
    treated is the operator's). Events cannot be preset: "Corona" is a
    substorm's look -- a strong, disturbed sheet overhead -- and pressing
    Substorm on top of it is the operator's gesture.

    Standard columns hold the host's 0..1; option, integer and boolean columns
    hold their real value (tools/check_presets.py checks both).
*/
namespace boreal::presets
{
enum Param
{
	kHemisphere,
	kOvalDistance,
	kDip,
	kDeclination,
	kSpeed,
	kArcs,
	kArcSpacing,
	kSheetStrength,
	kCurlSize,
	kDisturbance,
	kDrift,
	kEnergy,
	kFlux,
	kKnight,
	kThickness,
	kRays,
	kActivity,
	kWind,
	kAirglow,
	kExtinction,
	kCamera,
	kLookAzimuth,
	kLookElevation,
	kFov,
	kRoll,
	kExposure,
	kObserver,
	kStars,
	kStarMotion,
	kHorizon,
	kParamCount
};

struct Preset
{
	const char* name;
	float v[ kParamCount ];
};

// Physical values of the defaults (Controls.cpp has the curves): oval 250 km
// north, dip 77, declination 0, 4x, two arcs 60 km apart, 1 km/s, curls 8 km,
// 3 km disturbance, 0.2 km/s east; 3 keV, 25 erg, Knight 0.6, 2 km thick,
// rays 0.35; activity 0.4, wind 80 m/s, airglow 100 R, clean air; rectilinear
// looking north 25 degrees up, 90 degree field, 0 stops, camera, stars,
// sidereal motion on, hills.
inline constexpr Preset kPresets[] = {
	//                 hem  oval    dip   decl  speed arcs spacing strength curl   dist   drift energy flux    knight thick  rays   activ  wind   glow  ext    cam look  elev  fov    roll  expo   obs stars smot hor
	{ "Boreal",      { 0, 0.425f, 0.68f, 0.5f, 0.5f, 2, 0.4223f, 0.5624f, 0.4076f, 0.274f, 0.55f, 0.588f, 0.8997f, 0.6f, 0.5886f, 0.35f, 0.4f, 0.6333f, 0.3162f, 0.3333f, 0, 0.5f, 0.35f, 0.5556f, 0.5f, 0.375f, 0, 0.7f, 1, 2 } },
	//One quiet homogeneous arc low in the north: soft sheet, few curls.
	{ "Quiet Arc",   { 0,   0.62f,  0.68f, 0.5f, 0.375f, 1, 0.4223f, 0.33f,   0.55f,   0.16f,  0.52f, 0.63f,  0.5f,    0.3f, 0.66f,  0.12f, 0.2f, 0.55f,   0.5f, 0.3333f, 0, 0.5f, 0.22f, 0.4f,    0.5f, 0.45f,  0, 0.8f, 1, 2 } },
	//Overhead, looking up the field: three disturbed arcs converging on the
	//magnetic zenith, which is 13 degrees south of the zenith at dip 77.
	{ "Corona",      { 0,   0.29f,  0.68f, 0.5f, 0.5f,  3,  0.3f,    0.78f,   0.35f,   0.45f,  0.5f,  0.68f,  0.8f,    0.85f, 0.35f, 0.7f,  0.5f, 0.6333f, 0.5f, 0.3333f, 0, 1.0f, 0.87f, 0.78f,   0.5f, 0.3f,   0, 0.7f, 1, 0 } },
	//Solar maximum, soft precipitation: the red line dominates and lingers.
	{ "Red Storm",   { 0,   0.45f,  0.68f, 0.5f, 0.5f,  2,  0.68f,   0.4f,    0.5f,    0.35f,  0.5f,  0.2f,   0.75f,   0.4f, 0.7f,   0.2f,  1.0f, 0.83f,   0.5f, 0.3333f, 0, 0.5f, 0.4f,  0.6f,    0.5f, 0.5f,   0, 0.6f, 1, 2 } },
	//A single band of strong rays.
	{ "Rayed Band",  { 0,   0.44f,  0.68f, 0.5f, 0.5f,  1,  0.4223f, 0.48f,   0.75f,   0.22f,  0.55f, 0.65f,  0.72f,   0.5f, 0.3f,   0.92f, 0.4f, 0.6333f, 0.5f, 0.3333f, 0, 0.5f, 0.35f, 0.5556f, 0.5f, 0.375f, 0, 0.7f, 1, 2 } },
	//The southern lights over hills: the oval to the south, the curls turned
	//the other way, the rays converging north of the zenith.
	{ "Australis",   { 1,   0.5f,   0.68f, 0.5f, 0.5f,  2,  0.45f,   0.56f,   0.42f,   0.3f,  0.45f, 0.55f,  0.65f,   0.6f, 0.5f,   0.4f,  0.5f, 0.6333f, 0.5f, 0.3333f, 0, 0.0f, 0.3f,  0.6f,    0.5f, 0.4f,   0, 0.7f, 1, 2 } },
	//The defaults as the dark-adapted eye sees them: mesopic, grey-green,
	//and the red nearly gone.
	{ "Eye",         { 0, 0.425f, 0.68f, 0.5f, 0.5f, 2, 0.4223f, 0.5624f, 0.4076f, 0.274f, 0.55f, 0.588f, 0.8997f, 0.6f, 0.5886f, 0.35f, 0.4f, 0.6333f, 0.3162f, 0.3333f, 0, 0.5f, 0.35f, 0.5556f, 0.5f, 0.375f, 1, 0.7f, 1, 2 } },
};

inline constexpr int kCount = static_cast< int >( sizeof( kPresets ) / sizeof( kPresets[ 0 ] ) );

} // namespace boreal::presets
