#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace boreal
{
namespace
{
float clamp01( float v )
{
	return std::clamp( v, 0.0f, 1.0f );
}
float geometric( float v, float low, float high )
{
	return low * std::pow( high / low, clamp01( v ) );
}
float linear( float v, float low, float high )
{
	return low + ( high - low ) * clamp01( v );
}
constexpr float kSpeedLow  = 0.25f;
constexpr float kSpeedHigh = 64.0f;
constexpr float kFluxLow   = 0.05f;
constexpr float kFluxHigh  = 50.0f;
} // namespace

float OvalDistanceFromParam( float v )
{
	return linear( v, -600.0f, 1400.0f );
}
float DipFromParam( float v )
{
	return linear( v, 60.0f, 85.0f );
}
float DeclinationFromParam( float v )
{
	return linear( v, -30.0f, 30.0f );
}
float SpeedFromParam( float v )
{
	//Exactly zero at the bottom: a frozen sky is a thing an operator wants.
	if( v <= 0.0f )
		return 0.0f;
	return geometric( v, kSpeedLow, kSpeedHigh );
}
float ParamFromSpeed( float speed )
{
	if( speed <= 0.0f )
		return 0.0f;
	return std::log( std::clamp( speed, kSpeedLow, kSpeedHigh ) / kSpeedLow ) / std::log( kSpeedHigh / kSpeedLow );
}
unsigned SeedFromParam( float v )
{
	return static_cast< unsigned >( std::clamp( std::lround( v ), 0L, 9999L ) );
}

float ArcSpacingFromParam( float v )
{
	return geometric( v, 15.0f, 400.0f );
}
float SheetStrengthFromParam( float v )
{
	return geometric( v, 0.1f, 6.0f );
}
float CurlSizeFromParam( float v )
{
	return geometric( v, 2.0f, 60.0f );
}
float DisturbanceFromParam( float v )
{
	const float c = clamp01( v );
	return 40.0f * c * c;
}
float DriftFromParam( float v )
{
	return linear( v, -2.0f, 2.0f );
}

float EnergyFromParam( float v )
{
	return geometric( v, 0.2f, 20.0f );
}
float FluxFromParam( float v )
{
	if( v <= 0.0f )
		return 0.0f;
	return geometric( v, kFluxLow, kFluxHigh );
}
float ParamFromFlux( float flux )
{
	if( flux <= 0.0f )
		return 0.0f;
	return std::log( std::clamp( flux, kFluxLow, kFluxHigh ) / kFluxLow ) / std::log( kFluxHigh / kFluxLow );
}
float KnightFromParam( float v )
{
	return clamp01( v );
}
float ThicknessFromParam( float v )
{
	return geometric( v, 0.2f, 10.0f );
}
float RaysFromParam( float v )
{
	return clamp01( v );
}

float ActivityFromParam( float v )
{
	return clamp01( v );
}
float WindFromParam( float v )
{
	return linear( v, -0.3f, 0.3f );
}
float AirglowFromParam( float v )
{
	const float c = clamp01( v );
	return 1.0f * c * c;
}
float ParamFromAirglow( float kR )
{
	return std::sqrt( std::clamp( kR, 0.0f, 1.0f ) );
}
float ExtinctionFromParam( float v )
{
	return linear( v, 0.0f, 3.0f );
}

float LookAzimuthFromParam( float v )
{
	return linear( v, -180.0f, 180.0f );
}
float LookElevationFromParam( float v )
{
	return linear( v, -10.0f, 90.0f );
}
float FovFromParam( float v )
{
	return linear( v, 15.0f, 150.0f );
}
float RollFromParam( float v )
{
	return linear( v, -45.0f, 45.0f );
}
float ExposureFromParam( float v )
{
	return linear( v, -6.0f, 10.0f );
}
float StarsFromParam( float v )
{
	return clamp01( v );
}

float MaskThresholdFromParam( float v )
{
	return clamp01( v );
}
float IlluminationFromParam( float v )
{
	return linear( v, 0.0f, 20.0f );
}

} // namespace boreal
