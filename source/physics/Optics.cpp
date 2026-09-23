#include "physics/Optics.h"

#include "physics/Atmosphere.h"
#include "physics/Emission.h"

#include <algorithm>
#include <cmath>

namespace boreal::optics
{
namespace
{
constexpr double kPi     = 3.14159265358979323846;
constexpr double kPlanck = 6.62607015e-34;///< J s (exact)
constexpr double kLight  = 299792458.0;   ///< m/s (exact)

constexpr double k1PShare = 0.25;
} // namespace

//CIE 1931 2-degree CMFs and CIE 1951 V'(lambda), CVRL 1 nm tables, linearly
//interpolated. The 630.0/636.4 split is the NIST A values' ratio; 391.4 is
//GLOW's B37/B38 = 0.65/0.20 of 427.8.
const Component kComponentTable[ kComponents ] = {
	{ kGreen, 1.0, 557.7339, 0.556814, 0.998586, 0.004631, 0.360785 },
	{ kRed, emission::kA6300 / ( emission::kA6300 + emission::kA6364 ), 630.0304, 0.641765, 0.264689, 0.000050,
	  0.003327 },
	{ kRed, emission::kA6364 / ( emission::kA6300 + emission::kA6364 ), 636.3776, 0.515405, 0.204909, 0.000027,
	  0.002002 },
	{ kBlue, 1.0, 427.81, 0.256137, 0.009604, 1.244894, 0.174253 },
	{ kBlue, emission::kBranch3914 / emission::kBranch4278, 391.44, 0.005012, 0.000142, 0.023696, 0.002719 },
	{ kPink, k1PShare, 654.5, 0.224714, 0.083932, 0.0, 0.000478 },
	{ kPink, k1PShare, 662.4, 0.142734, 0.052643, 0.0, 0.000261 },
	{ kPink, k1PShare, 670.5, 0.084650, 0.030981, 0.0, 0.000143 },
	{ kPink, k1PShare, 678.9, 0.050134, 0.018237, 0.0, 0.000077 },
};

double RadiancePerKR( double nm )
{
	//1 kR = 1e9 photons cm^-2 s^-1 column = 1e13 m^-2 s^-1, spread over 4 pi sr.
	const double photons = 1e13 / ( 4.0 * kPi );
	return photons * kPlanck * kLight / ( nm * 1e-9 );
}

void ComponentXYZ( const Component& c, double xyz[ 3 ], double& scotopic )
{
	const double radiance = RadiancePerKR( c.nm ) * c.weight;
	xyz[ 0 ]              = 683.0 * c.xbar * radiance;
	xyz[ 1 ]              = 683.0 * c.ybar * radiance;
	xyz[ 2 ]              = 683.0 * c.zbar * radiance;
	scotopic              = 1700.0 * c.vprime * radiance;
}

void XYZToLinearSRGB( const double xyz[ 3 ], double rgb[ 3 ] )
{
	rgb[ 0 ] = 3.2406 * xyz[ 0 ] - 1.5372 * xyz[ 1 ] - 0.4986 * xyz[ 2 ];
	rgb[ 1 ] = -0.9689 * xyz[ 0 ] + 1.8758 * xyz[ 1 ] + 0.0415 * xyz[ 2 ];
	rgb[ 2 ] = 0.0557 * xyz[ 0 ] - 0.2040 * xyz[ 1 ] + 1.0570 * xyz[ 2 ];
}

void GamutMap( double rgb[ 3 ] )
{
	//= mirrored in Shaders.cpp, gamutMap()
	const double y  = 0.2126 * rgb[ 0 ] + 0.7152 * rgb[ 1 ] + 0.0722 * rgb[ 2 ];
	const double lo = std::min( { rgb[ 0 ], rgb[ 1 ], rgb[ 2 ] } );
	if( lo >= 0.0 || y <= 0.0 )
	{
		if( y <= 0.0 )
			rgb[ 0 ] = rgb[ 1 ] = rgb[ 2 ] = std::max( 0.0, y );
		return;
	}
	const double t = y / ( y - lo );
	for( int i = 0; i < 3; ++i )
		rgb[ i ] = y + t * ( rgb[ i ] - y );
}

//---------------------------------------------------------------------------
double RayleighDepth( double nm )
{
	const double l = nm * 1e-3;
	const double l2 = l * l, l4 = l2 * l2;
	return 0.008569 / l4 * ( 1.0 + 0.0113 / l2 + 0.00013 / l4 );
}

double AerosolDepth( double nm )
{
	return 0.05 * std::pow( nm / 550.0, -1.3 );
}

double KastenYoung( double z )
{
	const double zc = std::clamp( z, 0.0, 90.0 );
	return 1.0 / ( std::cos( zc * kPi / 180.0 ) + 0.50572 * std::pow( 96.07995 - zc, -1.6364 ) );
}

double Transmission( double nm, double zenithDegrees, double scale )
{
	return std::exp( -scale * ( RayleighDepth( nm ) + AerosolDepth( nm ) ) * KastenYoung( zenithDegrees ) );
}

double VanRhijn( double zenithDegrees, double layerKm )
{
	const double r = atmosphere::kEarthRadiusKm / ( atmosphere::kEarthRadiusKm + layerKm );
	const double s = std::sin( zenithDegrees * kPi / 180.0 );
	return 1.0 / std::sqrt( 1.0 - r * r * s * s );
}

double MesopicM( double photopic, double scotopic, double* mesopic )
{
	double m = 0.5, l = 0.0;
	for( int i = 0; i < 20; ++i )
	{
		l = ( m * photopic + ( 1.0 - m ) * scotopic * kMesopicV0 ) / ( m + ( 1.0 - m ) * kMesopicV0 );
		double next;
		if( l <= kMesopicLow )
			next = 0.0;
		else if( l >= kMesopicTop )
			next = 1.0;
		else
			next = std::clamp( kMesopicA + kMesopicB * std::log10( l ), 0.0, 1.0 );
		if( std::fabs( next - m ) < 1e-9 )
		{
			m = next;
			break;
		}
		m = next;
	}
	l = ( m * photopic + ( 1.0 - m ) * scotopic * kMesopicV0 ) / ( m + ( 1.0 - m ) * kMesopicV0 );
	if( mesopic )
		*mesopic = l;
	return m;
}

} // namespace boreal::optics
