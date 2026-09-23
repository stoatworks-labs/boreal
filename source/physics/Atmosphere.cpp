#include "physics/Atmosphere.h"

#include <algorithm>
#include <cmath>

namespace boreal::atmosphere
{
namespace
{
constexpr double kBoltzmann = 1.380649e-16;///< erg / K (CODATA 2018, exact)
constexpr double kGravity0  = 980.665;     ///< cm s^-2, standard gravity

double logLerp( double a, double b, double f )
{
	return std::exp( std::log( std::max( a, 1e-300 ) ) * ( 1.0 - f ) + std::log( std::max( b, 1e-300 ) ) * f );
}

Row mix( const Row& quiet, const Row& active, double f )
{
	Row r;
	r.n2  = logLerp( quiet.n2, active.n2, f );
	r.o2  = logLerp( quiet.o2, active.o2, f );
	r.o   = logLerp( quiet.o, active.o, f );
	r.rho = logLerp( quiet.rho, active.rho, f );
	r.t   = quiet.t + ( active.t - quiet.t ) * f;
	return r;
}
} // namespace

Air At( double km, double activity )
{
	const double f   = std::clamp( activity, 0.0, 1.0 );
	const double pos = std::clamp( ( km - kTableBottomKm ) / kTableStepKm, 0.0, static_cast< double >( kTableRows - 1 ) );
	const int i0     = std::min( static_cast< int >( pos ), kTableRows - 2 );
	const double w   = pos - i0;

	const Row a = mix( kTableQuiet[ i0 ], kTableActive[ i0 ], f );
	const Row b = mix( kTableQuiet[ i0 + 1 ], kTableActive[ i0 + 1 ], f );
	const Row r = mix( a, b, w );

	Air air;
	air.n2  = r.n2;
	air.o2  = r.o2;
	air.o   = r.o;
	air.t   = r.t;
	air.rho = r.rho;

	//The MSIS mass density includes the minor species; the mean molecular
	//mass is taken from it over the three majors. Below ~500 km they carry
	//>99% of the number; above, helium grows (at solar minimum it rivals O by
	//800 km) and this overstates the mass. It enters only Fang's scale height,
	//up where no precipitation in the table deposits anything that matters.
	const double n    = r.n2 + r.o2 + r.o;
	const double mass = r.rho / std::max( n, 1e-300 );
	const double g    = kGravity0 * std::pow( kEarthRadiusKm / ( kEarthRadiusKm + km ), 2.0 );
	air.scaleHeight   = kBoltzmann * r.t / ( mass * g );
	return air;
}

} // namespace boreal::atmosphere
