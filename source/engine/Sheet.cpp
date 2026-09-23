#include "engine/Sheet.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace boreal::engine
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

/// The velocity of targets [begin, end) induced by every node. The per-node
/// trig is precomputed by the caller, so a pair costs a handful of multiplies
/// and one divide: cos(a-b) = ca cb + sa sb and cosh(a-b) = cha chb - sha shb.
/// Y is taken relative to a common origin near the arcs, so cosh stays near 1
/// and the subtraction loses nothing that matters (AGENTS.md, "cancellation").
struct Trig
{
	std::vector< double > cx, sx, ch, sh, w;
};

void induce( const Trig& t, int n, int begin, int end, double c, double scale, double* u, double* v )
{
	const double* cx = t.cx.data();
	const double* sx = t.sx.data();
	const double* ch = t.ch.data();
	const double* sh = t.sh.data();
	const double* w  = t.w.data();
	for( int i = begin; i < end; ++i )
	{
		const double cxi = cx[ i ], sxi = sx[ i ], chi = ch[ i ], shi = sh[ i ];
		double su = 0.0, sv = 0.0;
		for( int j = 0; j < n; ++j )
		{
			const double cosX  = cxi * cx[ j ] + sxi * sx[ j ];
			const double sinX  = sxi * cx[ j ] - cxi * sx[ j ];
			const double coshY = chi * ch[ j ] - shi * sh[ j ];
			const double sinhY = shi * ch[ j ] - chi * sh[ j ];
			const double r     = w[ j ] / ( coshY - cosX + c );
			su += sinhY * r;
			sv += sinX * r;
		}
		u[ i ] = -scale * su;
		v[ i ] = scale * sv;
	}
}
} // namespace

//---------------------------------------------------------------------------
int Sheet::Count() const
{
	int n = 0;
	for( const Arc& arc : arcs )
		n += static_cast< int >( arc.x.size() );
	return n;
}

void Sheet::Weights( std::vector< double >& w ) const
{
	w.clear();
	for( const Arc& arc : arcs )
	{
		const size_t n = arc.g.size();
		for( size_t i = 0; i < n; ++i )
			w.push_back( 0.5 * ( arc.g[ ( i + n - 1 ) % n ] + arc.g[ i ] ) );
	}
}

void Sheet::flatten( std::vector< double >& x, std::vector< double >& y ) const
{
	x.clear();
	y.clear();
	for( const Arc& arc : arcs )
	{
		x.insert( x.end(), arc.x.begin(), arc.x.end() );
		y.insert( y.end(), arc.y.begin(), arc.y.end() );
	}
}

//---------------------------------------------------------------------------
void Sheet::Velocity( const std::vector< double >& x, const std::vector< double >& y, std::vector< double >& u,
                      std::vector< double >& v ) const
{
	const int n = static_cast< int >( x.size() );
	u.assign( static_cast< size_t >( n ), 0.0 );
	v.assign( static_cast< size_t >( n ), 0.0 );
	if( n == 0 )
		return;

	const double L     = settings.period;
	const double k     = 2.0 * kPi / L;
	const double c     = 0.5 * ( k * settings.delta ) * ( k * settings.delta );
	const double scale = 1.0 / ( 2.0 * L );

	//A common origin for Y, so cosh and sinh are evaluated near zero.
	double origin = 0.0;
	for( double value : y )
		origin += value;
	origin /= n;

	Trig t;
	t.cx.resize( static_cast< size_t >( n ) );
	t.sx.resize( static_cast< size_t >( n ) );
	t.ch.resize( static_cast< size_t >( n ) );
	t.sh.resize( static_cast< size_t >( n ) );
	t.w = w_;
	for( int i = 0; i < n; ++i )
	{
		t.cx[ i ] = std::cos( k * x[ i ] );
		t.sx[ i ] = std::sin( k * x[ i ] );
		t.ch[ i ] = std::cosh( k * ( y[ i ] - origin ) );
		t.sh[ i ] = std::sinh( k * ( y[ i ] - origin ) );
	}

	//Split the targets across threads. Each target's sum is taken in the same
	//order whatever the split, so the answer does not depend on the thread
	//count -- determinism is a property of the sum, not of the scheduler.
	const int threads = std::clamp( settings.threads, 1, 16 );
	if( threads == 1 || n < 256 )
	{
		induce( t, n, 0, n, c, scale, u.data(), v.data() );
	}
	else
	{
		std::vector< std::thread > pool;
		const int chunk = ( n + threads - 1 ) / threads;
		for( int p = 1; p < threads; ++p )
		{
			const int begin = p * chunk, end = std::min( n, begin + chunk );
			if( begin < end )
				pool.emplace_back( induce, std::cref( t ), n, begin, end, c, scale, u.data(), v.data() );
		}
		induce( t, n, 0, std::min( n, chunk ), c, scale, u.data(), v.data() );
		for( std::thread& worker : pool )
			worker.join();
	}

	for( int i = 0; i < n; ++i )
		u[ i ] += settings.driftU;
}

//---------------------------------------------------------------------------
void Sheet::Step( double h )
{
	Weights( w_ );
	flatten( x0_, y0_ );
	const size_t n = x0_.size();
	if( n == 0 )
		return;

	auto stage = [ & ]( double f, const std::vector< double >& du, const std::vector< double >& dv,
	                    std::vector< double >& ou, std::vector< double >& ov ) {
		xs_.resize( n );
		ys_.resize( n );
		for( size_t i = 0; i < n; ++i )
		{
			xs_[ i ] = x0_[ i ] + f * du[ i ];
			ys_[ i ] = y0_[ i ] + f * dv[ i ];
		}
		Velocity( xs_, ys_, ou, ov );
	};

	Velocity( x0_, y0_, k1u_, k1v_ );
	stage( 0.5 * h, k1u_, k1v_, k2u_, k2v_ );
	stage( 0.5 * h, k2u_, k2v_, k3u_, k3v_ );
	stage( h, k3u_, k3v_, k4u_, k4v_ );

	size_t i = 0;
	for( Arc& arc : arcs )
	{
		//The relaxation stands for the magnetosphere re-forming the arc. It is
		//applied exactly, after the Birkhoff-Rott step, and is OFF in every
		//conservation check: it is the one non-Hamiltonian term.
		const double keep = settings.relaxTime > 0.0 ? std::exp( -h / settings.relaxTime ) : 1.0;
		for( size_t j = 0; j < arc.x.size(); ++j, ++i )
		{
			arc.x[ j ] = x0_[ i ] + h / 6.0 * ( k1u_[ i ] + 2.0 * k2u_[ i ] + 2.0 * k3u_[ i ] + k4u_[ i ] );
			const double y = y0_[ i ] + h / 6.0 * ( k1v_[ i ] + 2.0 * k2v_[ i ] + 2.0 * k3v_[ i ] + k4v_[ i ] );
			arc.y[ j ]     = arc.baseY + ( y - arc.baseY ) * keep;
		}
	}
}

//---------------------------------------------------------------------------
int Sheet::Refine()
{
	const double L = settings.period;
	int changed    = 0;
	int total      = Count();

	for( Arc& arc : arcs )
	{
		//Removal first, so that room made under the cap is available to the
		//insertions below in the same pass.
		if( settings.remove )
		{
			const double tooClose = 0.25 * settings.spacingMax;
			for( size_t i = 1; arc.x.size() > 16 && i + 1 < arc.x.size(); )
			{
				auto length = [ & ]( size_t a, size_t b ) {
					return std::hypot( arc.x[ b ] - arc.x[ a ], arc.y[ b ] - arc.y[ a ] );
				};
				if( length( i - 1, i ) < tooClose && length( i, i + 1 ) < tooClose )
				{
					//Merge node i's two segments: G_{i-1} + G_i, one rounding.
					arc.g[ i - 1 ] += arc.g[ i ];
					arc.x.erase( arc.x.begin() + static_cast< long >( i ) );
					arc.y.erase( arc.y.begin() + static_cast< long >( i ) );
					arc.g.erase( arc.g.begin() + static_cast< long >( i ) );
					arc.a.erase( arc.a.begin() + static_cast< long >( i ) );
					--total;
					++changed;
					++i;//never remove two neighbours in one pass
				}
				else
					++i;
			}
		}

		if( !settings.insert )
			continue;

		std::vector< double > nx, ny, ng, na;
		const size_t n = arc.x.size();
		nx.reserve( n * 2 );
		auto node = [ & ]( long i, double& x, double& y ) {
			//Periodic neighbours, unwrapped: node n is node 0 moved one period east.
			const long m     = static_cast< long >( n );
			const long wraps = ( i >= 0 ) ? i / m : -( ( -i + m - 1 ) / m );
			const long j     = i - wraps * m;
			x                = arc.x[ static_cast< size_t >( j ) ] + static_cast< double >( wraps ) * L;
			y                = arc.y[ static_cast< size_t >( j ) ];
		};
		for( size_t i = 0; i < n; ++i )
		{
			nx.push_back( arc.x[ i ] );
			ny.push_back( arc.y[ i ] );
			na.push_back( arc.a[ i ] );

			double x0, y0, x1, y1;
			node( static_cast< long >( i ), x0, y0 );
			node( static_cast< long >( i ) + 1, x1, y1 );
			if( std::hypot( x1 - x0, y1 - y0 ) <= settings.spacingMax )
			{
				ng.push_back( arc.g[ i ] );
				continue;
			}
			if( total >= settings.cap )
			{
				//At the cap the sheet is left under-resolved: the segment keeps
				//its length and the curl it is part of is drawn coarser.
				++refused;
				ng.push_back( arc.g[ i ] );
				continue;
			}

			//Catmull-Rom through the four nodes around the gap, at its middle:
			//the Lagrangian midpoint, to third order in the node parameter.
			double xm, ym, x2, y2;
			node( static_cast< long >( i ) - 1, xm, ym );
			node( static_cast< long >( i ) + 2, x2, y2 );
			const double px = ( -xm + 9.0 * x0 + 9.0 * x1 - x2 ) / 16.0;
			const double py = ( -ym + 9.0 * y0 + 9.0 * y1 - y2 ) / 16.0;

			const double half = 0.5 * arc.g[ i ];//exact
			ng.push_back( half );
			nx.push_back( px );
			ny.push_back( py );
			const double a1 = ( i + 1 < n ) ? arc.a[ i + 1 ] : arc.a[ 0 ] + L;
			na.push_back( 0.5 * ( arc.a[ i ] + a1 ) );
			ng.push_back( half );
			++total;
			++changed;
		}
		arc.x.swap( nx );
		arc.y.swap( ny );
		arc.g.swap( ng );
		arc.a.swap( na );
	}
	return changed;
}

//---------------------------------------------------------------------------
double Sheet::Circulation() const
{
	double total = 0.0;
	for( const Arc& arc : arcs )
		for( double g : arc.g )
			total += g;
	return total;
}

double Sheet::Impulse() const
{
	std::vector< double > w;
	Weights( w );
	double total = 0.0;
	size_t i     = 0;
	for( const Arc& arc : arcs )
		for( double y : arc.y )
			total += w[ i++ ] * y;
	return total;
}

double Sheet::Hamiltonian() const
{
	std::vector< double > w, x, y;
	Weights( w );
	flatten( x, y );
	const double k = 2.0 * kPi / settings.period;
	const double c = 0.5 * ( k * settings.delta ) * ( k * settings.delta );
	const size_t n = x.size();
	double total   = 0.0;
	for( size_t i = 0; i < n; ++i )
	{
		double row = 0.0;
		for( size_t j = i + 1; j < n; ++j )
		{
			const double d = std::cosh( k * ( y[ i ] - y[ j ] ) ) - std::cos( k * ( x[ i ] - x[ j ] ) ) + c;
			row += w[ j ] * std::log( d );
		}
		total += w[ i ] * row;
	}
	return -total / ( 4.0 * kPi );
}

void Sheet::Rewrap()
{
	const double L = settings.period;
	for( Arc& arc : arcs )
	{
		if( arc.x.empty() )
			continue;
		const double shift = std::floor( ( arc.x[ 0 ] + 0.5 * L ) / L ) * L;
		if( shift == 0.0 )
			continue;
		for( double& x : arc.x )
			x -= shift;
		for( double& a : arc.a )
			a -= shift;
	}
}

//---------------------------------------------------------------------------
double GrowthRate( double gamma, double k, double delta, double period )
{
	const double m     = k * period / ( 2.0 * kPi );
	const double kk    = 2.0 * kPi / period;
	const double c     = 0.5 * ( kk * delta ) * ( kk * delta );
	const double mu    = std::acosh( 1.0 + c );
	if( mu <= 0.0 )
		return 0.5 * gamma * k;
	const double e     = std::exp( -m * mu );
	return 0.5 * gamma * k * std::sqrt( e * ( 1.0 - e ) / ( m * std::sinh( mu ) ) );
}

double GrowthRatePlanar( double gamma, double k, double delta )
{
	const double x = k * delta;
	if( x <= 0.0 )
		return 0.5 * gamma * k;
	const double e = std::exp( -x );
	return 0.5 * gamma * k * std::sqrt( e * ( 1.0 - e ) / x );
}

double FastestWavenumber( double delta )
{
	//sigma ~ (1/delta) sqrt( x e^{-x} (1 - e^{-x}) ): maximise g(x) by golden section.
	auto g = []( double x ) { return x * std::exp( -x ) * ( 1.0 - std::exp( -x ) ); };
	double a = 0.05, b = 5.0;
	const double r = 0.5 * ( std::sqrt( 5.0 ) - 1.0 );
	for( int i = 0; i < 100; ++i )
	{
		const double c = b - r * ( b - a ), d = a + r * ( b - a );
		if( g( c ) > g( d ) )
			b = d;
		else
			a = c;
	}
	return 0.5 * ( a + b ) / delta;
}

//---------------------------------------------------------------------------
Pcg::Pcg( uint64_t seed, uint64_t stream ) : increment( ( stream << 1u ) | 1u )
{
	Next();
	state += seed;
	Next();
}

uint32_t Pcg::Next()
{
	const uint64_t old = state;
	state              = old * 6364136223846793005ULL + increment;
	const uint32_t xorshifted = static_cast< uint32_t >( ( ( old >> 18u ) ^ old ) >> 27u );
	const uint32_t rot        = static_cast< uint32_t >( old >> 59u );
	return ( xorshifted >> rot ) | ( xorshifted << ( ( -rot ) & 31u ) );
}

double Pcg::Uniform()
{
	return Next() / 4294967296.0;
}

double Pcg::Normal()
{
	const double u1 = std::max( Uniform(), 1e-300 );
	const double u2 = Uniform();
	return std::sqrt( -2.0 * std::log( u1 ) ) * std::cos( 2.0 * kPi * u2 );
}

uint32_t PcgHash( uint32_t v )
{
	//= mirrored in Shaders.cpp, pcg()
	const uint32_t state = v * 747796405u + 2891336453u;
	const uint32_t word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}

} // namespace boreal::engine
