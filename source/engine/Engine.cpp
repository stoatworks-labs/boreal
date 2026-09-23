#include "engine/Engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace boreal::engine
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

/// The period of the sheet, km. Longer than any view (a curtain at 110 km is on
/// the horizon 1185 km away; one at 300 km, 1970 km), so an arc crosses the
/// whole sky and its ends are never in frame.
constexpr double kPeriod = 4096.0;

/// No more RK4 steps than this in one job. Past it the step grows to fit, up
/// to kMaxStep; past that the sheet falls behind the clock rather than stall
/// the host (and says so in the snapshot's skyTime).
constexpr int kMaxStepsPerJob = 6;
constexpr double kMaxStep     = 2.0;

/// The substorm: the sheet strength jumps by this and relaxes back with this
/// time constant; the poleward arc brightens and surges west.
constexpr double kSubstormBoost    = 2.5;
constexpr double kSubstormDecay    = 240.0;///< s
constexpr double kSurgeSpeed       = 1.5;  ///< km/s westward (surges travel at ~1-2 km/s)
constexpr double kSurgeDecay       = 180.0;///< s
constexpr double kPolewardBrighten = 4.0;
constexpr double kBrightenDecay    = 150.0;///< s

/// A new small perturbation is seeded this often (sky seconds) while the
/// forcing is on: it stands for the magnetosphere's continuous driving.
constexpr double kForcingInterval = 12.0;

double wallMs()
{
	using namespace std::chrono;
	return duration< double, std::milli >( steady_clock::now().time_since_epoch() ).count();
}

double spacingFor( double delta )
{
	return std::clamp( 0.5 * delta, 2.0, 8.0 );
}
} // namespace

Engine::Engine() = default;

Engine::~Engine()
{
	{
		std::lock_guard< std::mutex > lock( mutex );
		stopping = true;
	}
	wake.notify_all();
	if( worker.joinable() )
		worker.join();
}

double Engine::StepFor( const Params& p )
{
	return std::clamp( 0.25 * p.delta / std::max( p.gamma * kSubstormBoost, 1e-3 ), 0.02, 1.0 );
}

//---------------------------------------------------------------------------
void Engine::perturb( Arc& arc, double amplitude, double wavelength, double centre, double width )
{
	const double k = 2.0 * kPi / wavelength;
	const double phase = 2.0 * kPi * random.Uniform();
	for( size_t i = 0; i < arc.x.size(); ++i )
	{
		double d = arc.x[ i ] - centre;
		d -= kPeriod * std::floor( d / kPeriod + 0.5 );
		const double envelope = width > 0.0 ? std::exp( -0.5 * d * d / ( width * width ) ) : 1.0;
		arc.y[ i ] += amplitude * envelope * std::sin( k * arc.x[ i ] + phase );
	}
}

void Engine::reset( const Params& p )
{
	random = Pcg( p.seed, 0x5eedULL );
	sheet  = Sheet();
	sheet.settings.period = kPeriod;
	time                  = 0.0;
	boost = polewardGain = 1.0;
	surgeSpeed            = 0.0;
	nextForcing           = kForcingInterval;

	const int arcs      = std::clamp( p.arcs, 1, 5 );
	const double space  = spacingFor( p.delta );
	const int perArc    = std::clamp( static_cast< int >( kPeriod / space ), 64, std::max( 64, p.cap / arcs ) );
	for( int a = 0; a < arcs; ++a )
	{
		Arc arc;
		arc.baseY = a * p.spacing;
		//Offset each arc's nodes by a fraction of a spacing, so no two arcs'
		//nodes line up and the periodic sums never see a degenerate pair.
		const double offset = ( a * 0.37 ) * kPeriod / perArc;
		for( int i = 0; i < perArc; ++i )
		{
			const double x = -0.5 * kPeriod + offset + kPeriod * i / perArc;
			arc.x.push_back( x );
			arc.y.push_back( arc.baseY );
			arc.g.push_back( p.gamma * kPeriod / perArc );
			arc.a.push_back( x );
		}

		//The seeded perturbation: a few modes near the fastest-growing
		//wavelength, and one long fold, all of `disturbance` km.
		const double fastest = 2.0 * kPi / FastestWavenumber( p.delta );
		for( int m = 0; m < 4; ++m )
			perturb( arc, p.disturbance * 0.5, fastest * ( 0.7 + 0.6 * random.Uniform() ), 0.0, 0.0 );
		perturb( arc, p.disturbance, 600.0 + 900.0 * random.Uniform(), 0.0, 0.0 );
		sheet.arcs.push_back( std::move( arc ) );
	}

	builtArcs    = arcs;
	builtSpacing = p.spacing;
	builtSeed    = p.seed;
	current      = p;//the circulations above are at this strength
}

//---------------------------------------------------------------------------
void Engine::Run( const Job& job )
{
	const double start = wallMs();
	const Params& p    = job.params;

	if( job.calm || builtArcs != std::clamp( p.arcs, 1, 5 ) || builtSpacing != p.spacing || builtSeed != p.seed )
	{
		reset( p );
		time = job.skyTime;
		nextForcing = time + kForcingInterval;
	}

	//The sheet strength follows the control between jobs: every segment's
	//circulation is scaled together, so the sheet's shape is untouched and
	//only its speed changes.
	const double wanted = p.gamma * boost;
	const double have   = current.gamma > 0.0 && started ? current.gamma * boost : wanted;
	if( have > 0.0 && std::fabs( wanted / have - 1.0 ) > 1e-12 )
		for( Arc& arc : sheet.arcs )
			for( double& g : arc.g )
				g *= wanted / have;

	for( int s = 0; s < job.substorms; ++s )
	{
		//The jump. The large-scale kink on the poleward arc is the seed the
		//same instability rolls into a surge; the westward travel and the
		//brightening are the substorm's own.
		const double jump = kSubstormBoost / boost;
		boost             = kSubstormBoost;
		for( Arc& arc : sheet.arcs )
			for( double& g : arc.g )
				g *= jump;
		polewardGain = kPolewardBrighten;
		surgeSpeed   = kSurgeSpeed;
		if( !sheet.arcs.empty() )
			perturb( sheet.arcs.back(), std::max( 0.4 * p.spacing, 15.0 ), 400.0, 600.0 * ( random.Uniform() - 0.5 ),
			         250.0 );
	}

	current = p;
	started = true;

	sheet.settings.period     = kPeriod;
	sheet.settings.delta      = p.delta;
	sheet.settings.spacingMax = spacingFor( p.delta );
	sheet.settings.cap        = p.cap;
	sheet.settings.relaxTime  = p.relaxTime;
	sheet.settings.threads    = p.threads;

	//Advance to the target in whole steps.
	double h         = StepFor( p );
	const double gap = job.skyTime - time;
	int steps        = gap > 0.0 ? static_cast< int >( std::floor( gap / h + 1e-9 ) ) : 0;
	if( steps > kMaxStepsPerJob )
	{
		h     = std::min( gap / kMaxStepsPerJob, kMaxStep );
		steps = static_cast< int >( std::floor( gap / h + 1e-9 ) );
		steps = std::min( steps, kMaxStepsPerJob );
	}

	for( int s = 0; s < steps; ++s )
	{
		//The poleward arc's surge is a drift of its own; everything else
		//drifts with the convection.
		sheet.settings.driftU = p.drift;
		sheet.Step( h );
		if( surgeSpeed > 0.0 && !sheet.arcs.empty() )
			for( double& x : sheet.arcs.back().x )
				x -= surgeSpeed * h;
		time += h;

		//The event decays, exactly.
		const double relaxed = 1.0 + ( boost - 1.0 ) * std::exp( -h / kSubstormDecay );
		if( std::fabs( relaxed - boost ) > 0.0 )
		{
			for( Arc& arc : sheet.arcs )
				for( double& g : arc.g )
					g *= relaxed / boost;
			boost = relaxed;
		}
		surgeSpeed *= std::exp( -h / kSurgeDecay );
		polewardGain = 1.0 + ( polewardGain - 1.0 ) * std::exp( -h / kBrightenDecay );

		if( p.forcing && p.disturbance > 0.0 && time >= nextForcing )
		{
			nextForcing += kForcingInterval;
			if( !sheet.arcs.empty() )
			{
				Arc& arc = sheet.arcs[ random.Next() % sheet.arcs.size() ];
				const double fastest = 2.0 * kPi / FastestWavenumber( p.delta );
				perturb( arc, 0.3 * p.disturbance, fastest * ( 0.7 + 0.6 * random.Uniform() ),
				         kPeriod * ( random.Uniform() - 0.5 ), 300.0 );
			}
		}

		sheet.Refine();
		sheet.Rewrap();
	}

	//If the steps could not reach the target, the sheet is behind the clock:
	//that time is lost, not owed, so a stall never turns into a lurch later.
	//(Less than a step short is ordinary: the remainder carries to the next job.)
	if( gap > 0.0 && steps > 0 )
		time = std::max( time, job.skyTime - h );

	//Snapshot.
	std::vector< double > gains( sheet.arcs.size(), 1.0 );
	if( !gains.empty() )
		gains.back() *= polewardGain;
	Precipitation( sheet, p, p.gamma * boost, gains, snapshot );
	snapshot.skyTime  = time;
	snapshot.steps    = steps;
	snapshot.gammaNow = p.gamma * boost;
	snapshot.count    = sheet.Count();
	snapshot.refused  = sheet.refused;
	snapshot.cpuMs    = wallMs() - start;
}

//---------------------------------------------------------------------------
void Engine::Precipitation( const Sheet& sheet, const Params& p, double gammaNow, const std::vector< double >& arcGain,
                            Snapshot& out )
{
	out.nodes.clear();
	out.arcStart.clear();
	const double L = sheet.settings.period;

	for( size_t a = 0; a < sheet.arcs.size(); ++a )
	{
		const Arc& arc = sheet.arcs[ a ];
		out.arcStart.push_back( static_cast< int >( out.nodes.size() ) );
		const size_t n = arc.x.size();
		auto length    = [ & ]( size_t i, size_t j, double wrap ) {
			return std::hypot( arc.x[ j ] + wrap - arc.x[ i ], arc.y[ j ] - arc.y[ i ] );
		};
		for( size_t i = 0; i < n; ++i )
		{
			const size_t prev   = ( i + n - 1 ) % n;
			const size_t next   = ( i + 1 ) % n;
			const double before = length( prev, i, i == 0 ? L : 0.0 );
			const double after  = length( i, next, i + 1 == n ? L : 0.0 );
			//Circulation per km of sheet: the node's weight over the length it
			//stands for. High where the sheet is wound up, low where stretched.
			const double w      = 0.5 * ( arc.g[ prev ] + arc.g[ i ] );
			const double gamma  = std::fabs( w ) / std::max( 0.5 * ( before + after ), 1e-6 );
			const double ratio  = std::clamp( gamma / std::max( gammaNow, 1e-9 ), 0.05, 20.0 );

			//The Knight relation: j_par = K dPhi, and j_par is the sheet strength
			//(the space charge's divergence carried along the field), so the
			//accelerating potential -- and E0 -- goes as the sheet strength;
			//the energy flux j dPhi goes as its square. Knight is the exponent's
			//weight: 0 is no coupling.
			//Both RELATIVE to the base Energy and Flux, which the renderer
			//applies on the frame they are set -- so switching the precipitation
			//off is seen on the very next frame, not one engine job later.
			const double e0   = std::pow( ratio, p.knight );
			const double flux = std::pow( ratio, 2.0 * p.knight ) * arcGain[ a ];

			double x = arc.x[ i ];
			x -= L * std::floor( x / L + 0.5 );
			Node node;
			node.x     = static_cast< float >( x );
			node.y     = static_cast< float >( arc.y[ i ] );
			node.flux  = static_cast< float >( flux );
			node.lnE0  = static_cast< float >( std::log( e0 ) );
			node.label = static_cast< float >( arc.a[ i ] - L * std::floor( arc.a[ i ] / L + 0.5 ) );
			node.gamma = static_cast< float >( gamma );
			out.nodes.push_back( node );
		}
	}
	out.arcStart.push_back( static_cast< int >( out.nodes.size() ) );
}

//---------------------------------------------------------------------------
void Engine::loop()
{
	std::unique_lock< std::mutex > lock( mutex );
	for( ;; )
	{
		wake.wait( lock, [ this ] { return hasJob || stopping; } );
		if( stopping )
			return;
		const Job job = pending;
		hasJob        = false;
		lock.unlock();
		Run( job );
		lock.lock();
		busy = false;
		done.notify_all();
	}
}

void Engine::Submit( const Job& job )
{
	if( !worker.joinable() )
		worker = std::thread( &Engine::loop, this );
	std::unique_lock< std::mutex > lock( mutex );
	done.wait( lock, [ this ] { return !busy; } );
	pending = job;
	hasJob  = true;
	busy    = true;
	lock.unlock();
	wake.notify_all();
}

const Snapshot& Engine::Wait()
{
	std::unique_lock< std::mutex > lock( mutex );
	done.wait( lock, [ this ] { return !busy; } );
	return snapshot;
}

} // namespace boreal::engine
