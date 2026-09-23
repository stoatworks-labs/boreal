#include "physics/Emission.h"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace boreal::emission
{
namespace
{
/// Fang et al. 2008, Table 1 (Maxwellian, isotropic). P[i][j], i = 1..8, j = 0..3.
/// Transcribed from the published table (the page image; the text layer drops
/// the minus signs). `brtest --deposition` compares the result with Fang 2010
/// and with the paper's Figure 3.
constexpr double kP2008[ 8 ][ 4 ] = {
	{ 3.49979e-1, -6.18200e-2, -4.08124e-2, 1.65414e-2 },
	{ 5.85425e-1, -5.00793e-2, 5.69309e-2, -4.02491e-3 },
	{ 1.69692e-1, -2.58981e-2, 1.96822e-2, 1.20505e-3 },
	{ -1.22271e-1, -1.15532e-2, 5.37951e-6, 1.20189e-3 },
	{ 1.57018e0, 2.87896e-1, -4.14857e-1, 5.18158e-2 },
	{ 8.83195e-1, 4.31402e-2, -8.33599e-2, 1.02515e-2 },
	{ 1.90953e0, -4.74704e-2, -1.80200e-1, 2.46652e-2 },
	{ -1.29566e0, -2.10952e-1, 2.73106e-1, -2.92752e-2 },
};

/// Fang et al. 2010, Table 1 (monoenergetic, isotropic).
constexpr double kP2010[ 8 ][ 4 ] = {
	{ 1.24616e0, 1.45903e0, -2.42269e-1, 5.95459e-2 },
	{ 2.23976e0, -4.22918e-7, 1.36458e-2, 2.53332e-3 },
	{ 1.41754e0, 1.44597e-1, 1.70433e-2, 6.39717e-4 },
	{ 2.48775e-1, -1.50890e-1, 6.30894e-9, 1.23707e-3 },
	{ -4.65119e-1, -1.05081e-1, -8.95701e-2, 1.22450e-2 },
	{ 3.86019e-1, 1.75430e-3, -7.42960e-4, 4.60881e-4 },
	{ -6.45454e-1, 8.49555e-4, -4.28581e-2, -2.99302e-3 },
	{ 9.48930e-1, 1.97385e-1, -2.50660e-3, -2.06938e-3 },
};

void coefficients( const double ( &p )[ 8 ][ 4 ], double energy, double ( &c )[ 8 ] )
{
	const double l = std::log( energy );
	for( int i = 0; i < 8; ++i )
		c[ i ] = std::exp( p[ i ][ 0 ] + l * ( p[ i ][ 1 ] + l * ( p[ i ][ 2 ] + l * p[ i ][ 3 ] ) ) );
}

/// The energy dissipation function, both papers' eq. (6) / (4):
/// f = C1 y^C2 exp(-C3 y^C4) + C5 y^C6 exp(-C7 y^C8).
double dissipation( const double ( &c )[ 8 ], double y )
{
	if( y <= 0.0 )
		return 0.0;
	return c[ 0 ] * std::pow( y, c[ 1 ] ) * std::exp( -c[ 2 ] * std::pow( y, c[ 3 ] ) )
	       + c[ 4 ] * std::pow( y, c[ 5 ] ) * std::exp( -c[ 6 ] * std::pow( y, c[ 7 ] ) );
}

double lossO1S( const atmosphere::Air& air )
{
	return kA1S + QuenchO1S_O2( air.t ) * air.o2 + kQuenchO1S_O * air.o;
}

double lossO1D( const atmosphere::Air& air )
{
	return kA1D + QuenchO1D_N2( air.t ) * air.n2 + QuenchO1D_O2( air.t ) * air.o2 + kQuenchO1D_O * air.o;
}

/// The fraction of N2(A) that ends by exciting O(1S)-capable energy transfer
/// to O rather than radiating or being quenched by O2.
double n2aToO( const atmosphere::Air& air )
{
	const double toO = kN2A_O * air.o;
	return toO / ( kA_N2A + toO + kN2A_O2 * air.o2 );
}

std::vector< Volume > profileWithYield( double activity, double e0, double flux, double yield1S, double* rawFraction )
{
	std::vector< Volume > rows( kHeights );
	double raw = 0.0;
	std::vector< atmosphere::Air > air( kHeights );
	for( int i = 0; i < kHeights; ++i )
	{
		air[ i ]              = atmosphere::At( HeightAt( i ), activity );
		rows[ i ].ionisation = IonisationFang2008( air[ i ], e0, flux );
		raw += rows[ i ].ionisation * TrapezoidWeight( i );
	}

	//Energy conservation of the table: q * 35 eV integrated over height is Q.
	const double wanted = flux * kKeVPerErg / kIonPairKeV;//ion pairs cm^-2 s^-1
	const double have   = raw * 1e5;                        //1 km = 1e5 cm
	const double scale  = have > 0.0 ? wanted / have : 0.0;
	if( rawFraction )
		*rawFraction = wanted > 0.0 ? have / wanted : 0.0;

	for( int i = 0; i < kHeights; ++i )
	{
		Volume& r        = rows[ i ];
		const auto& a    = air[ i ];
		r.ionisation *= scale;
		const double fN2 = FractionN2( a );
		const double fO  = FractionO( a );

		r.prod1S = yield1S * r.ionisation * fN2 * n2aToO( a );
		r.loss1S = lossO1S( a );
		r.prod1D = r.ionisation * ( kYield1D_DR + kYield1D_O * fO );
		r.loss1D = lossO1D( a );

		r.e5577 = r.prod1S * kA5577 / r.loss1S;
		r.e6300 = r.prod1D * kA6300 / r.loss1D;
		r.e6364 = r.prod1D * kA6364 / r.loss1D;
		r.e4278 = r.ionisation * fN2 * kN2BPerIonisation * kBranch4278;
		r.e3914 = r.ionisation * fN2 * kN2BPerIonisation * kBranch3914;
		r.e1P   = r.ionisation * fN2 * kYield1P;
	}
	return rows;
}
} // namespace

//---------------------------------------------------------------------------
double IonisationFang2008( const atmosphere::Air& air, double e0, double flux )
{
	double c[ 8 ];
	coefficients( kP2008, e0, c );
	const double y = std::pow( air.rho * air.scaleHeight / 4e-6, 0.606 ) / e0;//eq. (4)
	const double q0 = flux * kKeVPerErg;                                        //keV cm^-2 s^-1
	return q0 / ( 2.0 * kIonPairKeV ) * dissipation( c, y ) / air.scaleHeight;//eq. (2)
}

double IonisationFang2010Mono( const atmosphere::Air& air, double e, double flux )
{
	double c[ 8 ];
	coefficients( kP2010, e, c );
	const double y  = 2.0 / e * std::pow( air.rho * air.scaleHeight / 6e-6, 0.7 );//eq. (1)
	const double q0 = flux * kKeVPerErg;
	return q0 / kIonPairKeV * dissipation( c, y ) / air.scaleHeight;//eq. (3)
}

double IonisationFang2010Maxwellian( const atmosphere::Air& air, double e0, double flux )
{
	//Number flux phi(E) = Q0 / (2 E0^3) E exp(-E/E0) (Fang 2010 eq. 6); a bin
	//of width dE carries energy flux E phi dE.
	constexpr int kBins = 400;
	const double lo = std::log( 0.1 ), hi = std::log( 1000.0 );
	double q = 0.0;
	for( int b = 0; b < kBins; ++b )
	{
		const double le = lo + ( hi - lo ) * ( b + 0.5 ) / kBins;
		const double e  = std::exp( le );
		const double de = e * ( hi - lo ) / kBins;
		const double binFlux = flux * e * e * std::exp( -e / e0 ) / ( 2.0 * e0 * e0 * e0 ) * de;
		q += IonisationFang2010Mono( air, e, binFlux );
	}
	return q;
}

double FractionN2( const atmosphere::Air& a )
{
	return 0.92 * a.n2 / ( 0.92 * a.n2 + a.o2 + 0.56 * a.o );
}

double FractionO( const atmosphere::Air& a )
{
	return 0.56 * a.o / ( 0.92 * a.n2 + a.o2 + 0.56 * a.o );
}

double QuenchO1D_N2( double t )
{
	return 2.0e-11 * std::exp( 107.8 / t );
}
double QuenchO1D_O2( double t )
{
	return 2.9e-11 * std::exp( 67.5 / t );
}
double QuenchO1S_O2( double t )
{
	return 4.0e-12 * std::exp( -865.0 / t );
}

double TrapezoidWeight( int i )
{
	return ( i == 0 || i == kHeights - 1 ) ? 0.5 : 1.0;
}

double EnergyAt( int index )
{
	return kLowKeV * std::pow( kHighKeV / kLowKeV, static_cast< double >( index ) / ( kEnergies - 1 ) );
}

double HeightAt( int index )
{
	return atmosphere::kBottomKm + index;
}

double Yield1S()
{
	static double yield = 0.0;
	static std::once_flag once;
	std::call_once( once, [] {
		const std::vector< Volume > rows = profileWithYield( 0.0, 5.0, 1.0, 1.0, nullptr );
		double column = 0.0;
		for( int i = 0; i < kHeights; ++i )
			column += rows[ i ].e5577 * TrapezoidWeight( i ) * 1e5;
		yield = column > 0.0 ? kGreenPerErgTarget / column : 0.0;
	} );
	return yield;
}

std::vector< Volume > Profile( double activity, double e0, double flux, double* rawFraction )
{
	return profileWithYield( activity, e0, flux, Yield1S(), rawFraction );
}

//---------------------------------------------------------------------------
Tables BuildTables( double activity )
{
	Tables t;
	t.activity = activity;
	t.shape.assign( static_cast< size_t >( kHeights ) * kEnergies * 4, 0.0f );
	t.column.assign( static_cast< size_t >( kEnergies ) * 4, 0.0f );
	t.prompt.assign( static_cast< size_t >( kEnergies ) * 4, 0.0f );

	for( int e = 0; e < kEnergies; ++e )
	{
		double raw = 0.0;
		const std::vector< Volume > rows = Profile( activity, EnergyAt( e ), 1.0, &raw );

		//Columns, cm^-2 s^-1 per erg cm^-2 s^-1.
		double p1S = 0.0, n1S = 0.0, m1S = 0.0, p1D = 0.0, n1D = 0.0, m1D = 0.0, c4278 = 0.0, c3914 = 0.0, c1P = 0.0;
		for( int i = 0; i < kHeights; ++i )
		{
			const double w = TrapezoidWeight( i ) * 1e5;
			p1S += rows[ i ].prod1S * w;
			n1S += rows[ i ].prod1S / rows[ i ].loss1S * w;
			m1S += rows[ i ].prod1S / ( rows[ i ].loss1S * rows[ i ].loss1S ) * w;
			p1D += rows[ i ].prod1D * w;
			n1D += rows[ i ].prod1D / rows[ i ].loss1D * w;
			m1D += rows[ i ].prod1D / ( rows[ i ].loss1D * rows[ i ].loss1D ) * w;
			c4278 += rows[ i ].e4278 * w;
			c3914 += rows[ i ].e3914 * w;
			c1P += rows[ i ].e1P * w;
		}

		//Shapes, per km, each integrating to 1 by the same trapezoid.
		for( int i = 0; i < kHeights; ++i )
		{
			float* s   = &t.shape[ ( static_cast< size_t >( e ) * kHeights + i ) * 4 ];
			const double km = 1e5;//the columns above are in cm; a shape is per km
			s[ 0 ] = n1S > 0 ? static_cast< float >( rows[ i ].prod1S / rows[ i ].loss1S * km / n1S ) : 0.0f;
			s[ 1 ] = n1D > 0 ? static_cast< float >( rows[ i ].prod1D / rows[ i ].loss1D * km / n1D ) : 0.0f;
			s[ 2 ] = c4278 > 0 ? static_cast< float >( rows[ i ].e4278 * km / c4278 ) : 0.0f;
			s[ 3 ] = c1P > 0 ? static_cast< float >( rows[ i ].e1P * km / c1P ) : 0.0f;
		}

		//The factorisation keeps ONE lifetime per line and column, so it is the
		//one the EMISSION decays with that matters: tau_E = int p tau^2 /
		//int p tau, the mean lifetime of the population weighted by what it
		//radiates. (The production-weighted mean, int p tau / int p, is
		//dominated by the quenched bottom of the profile -- 0.3 s for O(1D) at
		//5 keV -- and made the red vanish in a third of a second.) The
		//production entry is then chosen so the steady state is exact:
		//S = P' tau_E = int p tau, the true steady population column.
		const double tauS = n1S > 0 ? m1S / n1S : 0.0;
		const double tauD = n1D > 0 ? m1D / n1D : 0.0;
		float* c = &t.column[ static_cast< size_t >( e ) * 4 ];
		c[ 0 ]   = static_cast< float >( tauS > 0 ? n1S / tauS : 0.0 );
		c[ 1 ]   = static_cast< float >( tauS );
		c[ 2 ]   = static_cast< float >( tauD > 0 ? n1D / tauD : 0.0 );
		c[ 3 ]   = static_cast< float >( tauD );

		float* p = &t.prompt[ static_cast< size_t >( e ) * 4 ];
		p[ 0 ]   = static_cast< float >( c4278 );
		p[ 1 ]   = static_cast< float >( c3914 );
		p[ 2 ]   = static_cast< float >( c1P );
		p[ 3 ]   = static_cast< float >( raw );
	}
	return t;
}

double PeakHeight( const std::vector< double >& profile )
{
	if( profile.size() < 3 )
		return 0.0;
	size_t best = 0;
	for( size_t i = 1; i < profile.size(); ++i )
		if( profile[ i ] > profile[ best ] )
			best = i;
	if( best == 0 || best + 1 >= profile.size() )
		return HeightAt( static_cast< int >( best ) );
	const double a = profile[ best - 1 ], b = profile[ best ], c = profile[ best + 1 ];
	const double denom = a - 2.0 * b + c;
	const double shift = denom != 0.0 ? 0.5 * ( a - c ) / denom : 0.0;
	return HeightAt( static_cast< int >( best ) ) + std::clamp( shift, -0.5, 0.5 );
}

} // namespace boreal::emission
