
//===========================================================================
// The checks.
//
// Each takes a Perturb. With every field at its default the check scores the
// plugin against the physics; `--negative` sets one field at a time to a
// deliberately wrong model and requires the check to FAIL.
//===========================================================================
struct Perturb
{
	double khDelta          = 1.0;  ///< --kh predicts with this times delta
	bool invariantsRelax    = false;///< --invariants runs with the arc relaxation on (non-Hamiltonian)
	double knightExponent   = 1.0;  ///< --knight expects E0 ~ gamma^this at Knight 1
	bool depositionMono     = false;///< --deposition uses a monoenergetic beam at E0
	bool quenchOff          = false;///< --quench expects no collisional quenching
	bool lifetimeRadiative  = false;///< --lifetime expects tau = 1/A, no quenching
	double colourNm         = 0.0;  ///< --colour expects 557.7 to land at this wavelength's xy
	double coronaDip        = 0.0;  ///< --corona expects the vanishing point at this dip offset, degrees
	bool vanRhijnFlat       = false;///< --vanrhijn expects a flat Earth: sec z
	bool extinctionSecant   = false;///< --extinction expects the plane-parallel airmass sec z
	bool overAirglow        = false;///< --over-check expects identity with the airglow ON
	bool determinismSeeds   = false;///< --determinism expects two seeds to agree
	bool onsetUnprimed      = false;///< --onset runs the analyser without its priming
	bool defaultsShifted    = false;///< --defaults expects preset row 2 to be the defaults
};

using CheckFn = int ( * )( const Perturb& );
struct CheckEntry
{
	const char* flag;
	CheckFn run;
};

//---------------------------------------------------------------------------
// The camera, on the CPU. //= mirrored from Shaders.cpp, cameraRay().
//---------------------------------------------------------------------------
struct Vec3
{
	double x, y, z;
};
Vec3 add( Vec3 a, Vec3 b )
{
	return { a.x + b.x, a.y + b.y, a.z + b.z };
}
Vec3 scale( Vec3 a, double s )
{
	return { a.x * s, a.y * s, a.z * s };
}
double dot( Vec3 a, Vec3 b )
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vec3 normalise( Vec3 a )
{
	return scale( a, 1.0 / std::sqrt( dot( a, a ) ) );
}
Vec3 vec( const float* v )
{
	return { v[ 0 ], v[ 1 ], v[ 2 ] };
}

/// The direction of pixel (px, py) (pixel indices, row 0 at the bottom) in a
/// W x H raster.
Vec3 cameraRay( const View& v, double px, double py, int w, int h, bool& valid )
{
	const double u = ( px + 0.5 ) / w, t = ( py + 0.5 ) / h;
	valid          = true;
	if( v.cameraKind == 0 )
	{
		const double aspect = static_cast< double >( w ) / h;
		const double x      = ( 2.0 * u - 1.0 ) * aspect * v.tanHalf;
		const double y      = ( 2.0 * t - 1.0 ) * v.tanHalf;
		return normalise( add( vec( v.forward ), add( scale( vec( v.right ), x ), scale( vec( v.up ), y ) ) ) );
	}
	const double dx = ( u - 0.5 ) * w, dy = ( t - 0.5 ) * h;
	const double r  = std::sqrt( dx * dx + dy * dy ) / ( 0.5 * std::min( w, h ) );
	valid           = r <= 1.0;
	const double z  = std::min( r, 1.0 ) * 0.5 * kPi;
	const double az = v.lookAz + std::atan2( -dx, dy );
	return { std::sin( z ) * std::sin( az ), std::sin( z ) * std::cos( az ), std::cos( z ) };
}

/// Where a direction lands in a rectilinear raster, in pixel coordinates
/// (pixel centres at integers).
bool project( const View& v, Vec3 d, int w, int h, double& px, double& py )
{
	const double f = dot( d, vec( v.forward ) );
	if( f <= 0.0 )
		return false;
	const double aspect = static_cast< double >( w ) / h;
	const double x      = dot( d, vec( v.right ) ) / f / v.tanHalf / aspect;
	const double y      = dot( d, vec( v.up ) ) / f / v.tanHalf;
	px                  = ( x + 1.0 ) * 0.5 * w - 0.5;
	py                  = ( y + 1.0 ) * 0.5 * h - 0.5;
	return true;
}

float inverseGeometric( double value, double low, double high )
{
	return static_cast< float >( std::log( value / low ) / std::log( high / low ) );
}

//===========================================================================
// --kh: the Kelvin-Helmholtz growth of a regularised periodic sheet.
//===========================================================================
struct GrowthRun
{
	double measured, theory, tolerance;
};

GrowthRun growth( double gamma, int m, double delta, double period, int nodes, double predictDelta )
{
	engine::Sheet sheet;
	sheet.settings.period  = period;
	sheet.settings.delta   = delta;
	sheet.settings.insert  = false;
	sheet.settings.remove  = false;
	sheet.settings.threads = 4;
	engine::Arc arc;
	const double k   = 2.0 * kPi * m / period;
	const double eps = 1e-4;//km: k eps ~ 1e-5, deep in the linear regime
	std::vector< double > base;
	for( int i = 0; i < nodes; ++i )
	{
		const double x = -0.5 * period + period * i / nodes;
		base.push_back( x );
		arc.x.push_back( x );
		arc.y.push_back( eps * std::sin( k * x ) );
		arc.g.push_back( gamma * period / nodes );
		arc.a.push_back( x );
	}
	sheet.arcs.push_back( arc );

	const double sigma = engine::GrowthRate( gamma, k, delta, period );
	const double z     = 0.05;//sigma h
	const double h     = z / sigma;
	const int steps    = 110;//sigma T = 5.5: growth by ~120
	for( int s = 0; s < steps; ++s )
		sheet.Step( h );

	double amplitude = 0.0;
	for( int i = 0; i < nodes; ++i )
		amplitude += sheet.arcs[ 0 ].y[ i ] * std::sin( k * base[ i ] );
	amplitude *= 2.0 / nodes;

	//From a pure displacement the linear solution is eps cosh( sigma t ):
	//the decaying mode is in it, and cosh accounts for it exactly.
	const double T = steps * h;
	GrowthRun run;
	run.measured = std::acosh( amplitude / eps ) / T;
	run.theory   = engine::GrowthRate( gamma, k, delta * predictDelta, period );

	//The tolerance, derived rather than fitted:
	// - RK4: per step the growth factor is R(z) = 1+z+z^2/2+z^3/6+z^4/24
	//   instead of e^z, so the measured rate is ln R(z)/h; three times that gap.
	// - the discrete sum over nodes against the integral: the kernel's Fourier
	//   series aliases mode m with m +- N. The derivative kernel's coefficients
	//   go as n e^{-n mu}, so the relative error is ~ (N/m) e^{-(N-2m) mu}.
	// - nonlinearity: (k A_final)^2.
	// - rounding: the measured rate carries ~1e-13 / (sigma T) from acosh of a
	//   ratio good to ~1e-13.
	const double rk4      = std::fabs( std::log( 1 + z + z * z / 2 + z * z * z / 6 + z * z * z * z / 24 ) / z - 1.0 );
	const double kk       = 2.0 * kPi / period;
	const double mu       = std::acosh( 1.0 + 0.5 * ( kk * delta ) * ( kk * delta ) );
	const double aliasing = 4.0 * ( static_cast< double >( nodes ) / m ) * std::exp( -( nodes - 2 * m ) * mu );
	const double nonlin   = std::pow( k * amplitude, 2.0 );
	run.tolerance         = sigma * ( 3.0 * rk4 + aliasing + nonlin ) + 1e-12;
	return run;
}

int runKH( const Perturb& perturb )
{
	std::printf( "\n=== kh: a single mode on a periodic sheet grows at the derived sigma(k, delta)\n" );
	std::printf( "  sigma^2 = (gamma k/2)^2 e^{-m mu}(1 - e^{-m mu}) / (m sinh mu),  cosh mu = 1 + (2 pi delta/L)^2 / 2\n" );
	const double gamma = 1.0, period = 4096.0;
	const int nodes    = 1024;

	std::printf( "  -- three wavenumbers, delta = 20 km, N = %d, L = %.0f km, gamma = %.1f km/s\n", nodes, period, gamma );
	for( int m : { 8, 16, 32 } )
	{
		const GrowthRun r = growth( gamma, m, 20.0, period, nodes, perturb.khDelta );
		Check( std::fabs( r.measured - r.theory ) <= r.tolerance,
		       fmt( "lambda %5.0f km: measured %.9f /s, theory %.9f /s, |diff| %.2e <= %.2e", period / m, r.measured,
		            r.theory, std::fabs( r.measured - r.theory ), r.tolerance ) );
	}

	std::printf( "  -- delta -> 0 at lambda = 256 km: sigma / (gamma k / 2) rises towards 1\n" );
	double last = 0.0, deficit = 1.0;
	bool rising = true, closing = true;
	for( double delta : { 40.0, 20.0, 10.0, 5.0 } )
	{
		const int n        = delta < 8.0 ? 2048 : nodes;
		const GrowthRun r  = growth( gamma, 16, delta, period, n, perturb.khDelta );
		const double ratio = r.measured / ( 0.5 * gamma * 2.0 * kPi * 16 / period );
		Check( std::fabs( r.measured - r.theory ) <= r.tolerance,
		       fmt( "delta %4.1f km: sigma/(gamma k/2) = %.6f (theory %.6f), |diff| %.2e <= %.2e", delta, ratio,
		            r.theory / ( 0.5 * gamma * 2.0 * kPi * 16 / period ), std::fabs( r.measured - r.theory ), r.tolerance ) );
		rising  = rising && ratio > last;
		closing = closing && ( 1.0 - ratio ) < deficit / 1.5;
		deficit = 1.0 - ratio;
		last    = ratio;
	}
	Check( rising && closing, fmt( "the ratio rises monotonically, its gap to 1 shrinking at least 1.5x per halving of "
	                               "delta, to %.4f at delta = 5 km: sigma -> gamma k / 2", last ) );
	return Verdict();
}

//===========================================================================
// --invariants
//===========================================================================
int runInvariants( const Perturb& perturb )
{
	std::printf( "\n=== invariants: circulation, impulse and the Hamiltonian over 10 min of sky time\n" );
	auto build = [ & ]( double spacing ) {
		engine::Sheet sheet;
		sheet.settings.period     = 4096.0;
		sheet.settings.delta      = 16.0;
		sheet.settings.spacingMax = spacing;
		sheet.settings.cap        = 100000;
		sheet.settings.driftU     = 0.3;//a uniform drift changes none of the three
		sheet.settings.threads    = 4;
		sheet.settings.relaxTime  = perturb.invariantsRelax ? 120.0 : 0.0;
		engine::Pcg random( 7, 3 );
		for( int a = 0; a < 2; ++a )
		{
			engine::Arc arc;
			arc.baseY = 40.0 * a;
			const int n = static_cast< int >( std::lround( 4096.0 / spacing ) );//already at the bound: the first step inserts nothing
			for( int i = 0; i < n; ++i )
			{
				const double x = -2048.0 + 4096.0 * ( i + 0.37 * a ) / n;
				arc.x.push_back( x );
				arc.y.push_back( arc.baseY );
				arc.g.push_back( 0.3 * 4096.0 / n );
				arc.a.push_back( x );
			}
			for( int m = 0; m < 4; ++m )
			{
				const double k = 2.0 * kPi / ( 60.0 + 80.0 * random.Uniform() ), phase = 2.0 * kPi * random.Uniform();
				for( size_t i = 0; i < arc.x.size(); ++i )
					arc.y[ i ] += 3.0 * std::sin( k * arc.x[ i ] + phase );
			}
			sheet.arcs.push_back( arc );
		}
		return sheet;
	};

	//Three runs. The Hamiltonian is O(N^2) logs to evaluate, so it is taken at
	//the ends of each run only:
	//  1. h = 1 s, no refinement: the integrator's own drift;
	//  2. h = 0.5 s, the same: the drift must shrink ~16x if it is RK4's;
	//  3. h = 1 s, refinement on: everything, through every insertion.
	struct Result
	{
		double circulation, impulse, impulseJumps, hamiltonian, discretisation;
		int nodes, refinements;
	};
	auto run = [ & ]( double h, bool refine, Result& r, double spacing = 4.0 ) {
		engine::Sheet sheet = build( spacing );
		//Insertion only: removal is a coarsening, and is not what "through
		//point insertion" is about (AGENTS.md).
		sheet.settings.insert = refine;
		sheet.settings.remove = false;
		const double c0 = sheet.Circulation(), i0 = sheet.Impulse(), h0 = sheet.Hamiltonian();
		double impulseJumps = 0.0;
		int refinements     = 0;
		const int steps     = static_cast< int >( std::lround( 600.0 / h ) );
		for( int s = 0; s < steps; ++s )
		{
			sheet.Step( h );
			const double before = sheet.Impulse();
			const int changed   = sheet.Refine();
			if( changed > 0 )
			{
				//An insertion puts a node on a cubic through its neighbours,
				//which moves sum W y by (G/2)(y_new - linear midpoint): the
				//quadrature's, booked separately and bounded below.
				impulseJumps += sheet.Impulse() - before;
				refinements += changed;
			}
		}
		r.circulation  = sheet.Circulation() - c0;
		r.impulse      = sheet.Impulse() - i0 - impulseJumps;
		r.impulseJumps = impulseJumps;
		r.hamiltonian  = ( sheet.Hamiltonian() - h0 ) / std::fabs( h0 );
		r.nodes        = sheet.Count();
		r.refinements  = refinements;
		//The discretisation error of the end state's Hamiltonian: the same sheet
		//with every segment halved once. An insertion moves the discrete sum
		//towards the continuum's by at most about this much per halving.
		r.discretisation = 0.0;
		if( refine )
		{
			engine::Sheet finer = sheet;
			finer.settings.spacingMax *= 0.5;
			finer.settings.remove = false;
			finer.settings.cap    = 1 << 30;
			finer.Refine();
			r.discretisation = std::fabs( finer.Hamiltonian() - sheet.Hamiltonian() ) / std::fabs( h0 );
		}
		return c0;
	};

	Result coarse {}, fine {}, refined {}, rough {};
	const double circulation = run( 1.0, false, coarse );
	run( 0.5, false, fine );
	run( 1.0, true, refined );
	run( 1.0, true, rough, 8.0 );
	const double scaleImpulse = circulation * 100.0;//circulation times the y span of the arcs, km

	std::printf( "  two arcs, 2048 nodes, gamma 0.3 km/s, delta 16 km, spacing <= 4 km, drift 0.3 km/s; with refinement: %d nodes at the end after %d "
	             "insertions\n",
	             refined.nodes, refined.refinements );
	Check( std::fabs( refined.circulation ) <= 1e-12 * circulation,
	       fmt( "circulation: changed by %.3e of %.6e (bound 1e-12 relative: segment halving is exact, a merge one rounding)",
	            refined.circulation, circulation ) );
	Check( std::fabs( refined.impulse ) <= 1e-11 * scaleImpulse && std::fabs( coarse.impulse ) <= 1e-11 * scaleImpulse,
	       fmt( "impulse (sum W y, the centroid): %.3e without refinement, %.3e between refinements, of scale %.3e "
	            "(bound 1e-11: the kernel is antisymmetric and RK4 keeps linear invariants to rounding)",
	            coarse.impulse, refined.impulse, scaleImpulse ) );
	Check( std::fabs( refined.impulseJumps ) <= 1e-5 * scaleImpulse,
	       fmt( "impulse through the insertions: %.3e (bound 1e-5 of scale: the cubic's departure from the chord, "
	            "O(spacing^2 curvature), on a sheet resolved to spacing < delta/2)",
	            refined.impulseJumps ) );
	const double ratio = std::fabs( coarse.hamiltonian ) / std::max( std::fabs( fine.hamiltonian ), 1e-300 );
	Check( std::fabs( coarse.hamiltonian ) <= 1e-6 && ( std::fabs( coarse.hamiltonian ) < 1e-13 || ( ratio > 8.0 && ratio < 32.0 ) ),
	       fmt( "Hamiltonian, fixed nodes: %.3e at h = 1 s, %.3e at h = 0.5 s (ratio %.1f; RK4's h^4 predicts 16, "
	            "accepted 8..32) -- bound 1e-6 relative",
	            coarse.hamiltonian, fine.hamiltonian, ratio ) );
	//Through insertion the discrete Hamiltonian is not conserved to rounding:
	//an insertion re-discretises the double sum, and the dynamics of an
	//N-node sheet conserve H_N, not the continuum's H. What must hold is that
	//the change is DISCRETISATION -- it vanishes with the spacing, as O(h^2)
	//-- and is small at the resolution the plugin runs at.
	const double shrink = std::fabs( rough.hamiltonian ) / std::max( std::fabs( refined.hamiltonian ), 1e-300 );
	Check( std::fabs( refined.hamiltonian ) <= 1e-4 && shrink >= 2.5,
	       fmt( "Hamiltonian through %d insertions: %.3e relative at spacing <= 4 km, %.3e at <= 8 km: halving the "
	            "spacing shrinks it %.1fx (O(h^2) predicts 4; accepted >= 2.5) and it stays under 1e-4. For scale, "
	            "halving every segment of the end state once moves H by %.3e",
	            refined.refinements, refined.hamiltonian, rough.hamiltonian, shrink, refined.discretisation ) );
	return Verdict();
}

//===========================================================================
// --knight
//===========================================================================
int runKnight( const Perturb& perturb )
{
	std::printf( "\n=== knight: E0 at each node goes as the local sheet strength at Knight 1, flat at 0\n" );
	engine::Engine eng;
	engine::Engine::Job job;
	job.params.arcs        = 1;
	job.params.gamma       = 1.5;
	job.params.delta       = 8.0;
	job.params.disturbance = 6.0;
	job.params.relaxTime   = 0.0;
	job.params.threads     = 4;
	for( int f = 1; f <= 120; ++f )
	{
		job.skyTime = f * 1.5;
		eng.Run( job );
	}
	const engine::Sheet& sheet = eng.SheetForTest();

	double lo = 1e30, hi = 0.0, worst1 = 0.0, worst0 = 0.0;
	int used = 0;
	for( double knight : { 1.0, 0.0 } )
	{
		engine::Params p = job.params;
		p.knight         = knight;
		engine::Snapshot snap;
		engine::Engine::Precipitation( sheet, p, p.gamma, std::vector< double >( sheet.arcs.size(), 1.0 ), snap );
		for( const engine::Node& n : snap.nodes )
		{
			const double ratio = n.gamma / p.gamma;
			if( ratio <= 0.05 || ratio >= 20.0 )
				continue;//clamped: outside the relation's stated range
			const double e0 = std::exp( static_cast< double >( n.lnE0 ) );
			if( knight == 1.0 )
			{
				lo = std::min( lo, ratio );
				hi = std::max( hi, ratio );
				worst1 = std::max( worst1, std::fabs( e0 / std::pow( ratio, perturb.knightExponent ) - 1.0 ) );
				++used;
			}
			else
				worst0 = std::max( worst0, std::fabs( e0 - 1.0 ) );
		}
	}
	Check( hi / lo > 3.0, fmt( "the sheet is wound up: local strength spans %.3f..%.3f of the base (%d nodes)", lo, hi, used ) );
	Check( worst1 <= 1e-6, fmt( "Knight 1: E0 / E_base = gamma / gamma_0 to %.2e (bound 1e-6: float storage)", worst1 ) );
	Check( worst0 <= 1e-7, fmt( "Knight 0: E0 = E_base at every node to %.2e", worst0 ) );
	return Verdict();
}

//===========================================================================
// --deposition
//===========================================================================
std::vector< double > ionisationProfile( double activity, double e0, bool mono, int which )
{
	std::vector< double > q( emission::kHeights );
	for( int i = 0; i < emission::kHeights; ++i )
	{
		const atmosphere::Air air = atmosphere::At( emission::HeightAt( i ), activity );
		q[ i ] = mono ? emission::IonisationFang2010Mono( air, 2.0 * e0, 1.0 )
		              : ( which == 2008 ? emission::IonisationFang2008( air, e0, 1.0 )
		                                : emission::IonisationFang2010Maxwellian( air, e0, 1.0 ) );
	}
	return q;
}

int runDeposition( const Perturb& perturb )
{
	std::printf( "\n=== deposition: Fang et al. over the NRLMSIS table (quiet sun)\n" );
	//Read off Fang et al. 2008, Figure 3(a) (MSIS F10.7 = 50, Ap = 5, Q0 = 1
	//erg): the ionisation peak of the 1 keV Maxwellian at 120 km and of the
	//10 keV one at 97.5 km, each to +-1.5 km (the reading). 5 keV is not
	//plotted; it must lie between. Tolerance: the reading, plus the table's
	//1 km spacing, plus 0.5 km for this table's atmosphere (F10.7 = 70, a
	//winter night at 69 N) not being theirs -- 3 km.
	struct Target
	{
		double e0, km;
	};
	const Target targets[] = { { 1.0, 120.0 }, { 10.0, 97.5 } };
	for( const Target& t : targets )
	{
		const double peak = emission::PeakHeight( ionisationProfile( 0.0, t.e0, perturb.depositionMono, 2008 ) );
		Check( std::fabs( peak - t.km ) <= 3.0,
		       fmt( "E0 = %4.1f keV: peak at %.2f km; Fang 2008 Fig. 3a reads %.1f km (tolerance 3 km)", t.e0, peak, t.km ) );
	}
	const double p1 = emission::PeakHeight( ionisationProfile( 0.0, 1.0, perturb.depositionMono, 2008 ) );
	const double p5 = emission::PeakHeight( ionisationProfile( 0.0, 5.0, perturb.depositionMono, 2008 ) );
	const double p10 = emission::PeakHeight( ionisationProfile( 0.0, 10.0, perturb.depositionMono, 2008 ) );
	Check( p5 < p1 && p5 > p10, fmt( "E0 = 5 keV peaks at %.2f km, between 1 keV (%.2f) and 10 keV (%.2f)", p5, p1, p10 ) );

	//Monotone descent over the whole table.
	double previous = 1e9;
	bool monotone   = true;
	for( int e = 0; e < emission::kEnergies; e += 3 )
	{
		const double peak = emission::PeakHeight( ionisationProfile( 0.0, emission::EnergyAt( e ), perturb.depositionMono, 2008 ) );
		monotone          = monotone && peak <= previous + 1e-9;
		previous          = peak;
	}
	Check( monotone, "the peak descends monotonically with E0 from 0.1 to 30 keV" );

	//The independent parameterisation.
	for( double e0 : { 1.0, 5.0, 10.0 } )
	{
		const double a = emission::PeakHeight( ionisationProfile( 0.0, e0, perturb.depositionMono, 2008 ) );
		const double b = emission::PeakHeight( ionisationProfile( 0.0, e0, false, 2010 ) );
		Check( std::fabs( a - b ) <= 3.0,
		       fmt( "E0 = %4.1f keV: Fang 2008 (Maxwellian) %.2f km vs Fang 2010 (monoenergetic, integrated) %.2f km "
		            "(both claim < 5 km of their transport models; tolerance 3 km)",
		            e0, a, b ) );
	}

	//Energy conservation of the table the plugin uses.
	for( double e0 : { 1.0, 5.0, 10.0 } )
	{
		double raw = 0.0;
		const std::vector< emission::Volume > rows = emission::Profile( 0.0, e0, 1.0, &raw );
		double deposited = 0.0;
		for( int i = 0; i < emission::kHeights; ++i )
			deposited += rows[ i ].ionisation * emission::TrapezoidWeight( i ) * 1e5 * emission::kIonPairKeV / emission::kKeVPerErg;
		Check( std::fabs( deposited - 1.0 ) <= 1e-12 && std::fabs( raw - 1.0 ) <= 0.05,
		       fmt( "E0 = %4.1f keV: the table deposits %.14f of Q (bound 1e-12); Fang's own profile put %.4f of it in "
		            "80-500 km (bound 5%%: the paper's stated accuracy)",
		            e0, deposited, raw ) );
	}
	return Verdict();
}

//===========================================================================
// --quench
//===========================================================================
int runQuench( const Perturb& perturb )
{
	std::printf( "\n=== quench: the red/green emission ratio is A/(A + sum k n), at every table node\n" );
	//The formula, written out here from the literature values -- not called
	//from Emission.cpp -- and compared with the SHAPE TABLE THE GPU SAMPLES,
	//read back from the texture.
	Rig rig;
	if( !rig.Init( 64, 64 ) || !rig.Render( 1 ) )
		return 1;
	const int nh = emission::kHeights, ne = emission::kEnergies;
	const Floats shape  = readTexture( rig.plugin.ShapeTextureID(), nh, ne );
	const Floats column = readTexture( rig.plugin.ColumnTextureID(), ne, 2 );
	const double activity = rig.plugin.CurrentTables().activity;

	const int e = 41;//E0 = 5.0 keV sits near here; the check holds at any column
	const double e0 = emission::EnergyAt( e );
	double worst    = 0.0;
	const std::vector< emission::Volume > rows = emission::Profile( activity, e0, 1.0 );
	for( int i = 0; i < nh; ++i )
	{
		const atmosphere::Air a = atmosphere::At( emission::HeightAt( i ), activity );
		const double t          = a.t;
		const double qS = perturb.quenchOff ? 0.0 : 4.0e-12 * std::exp( -865.0 / t ) * a.o2 + 2.0e-14 * a.o;
		const double qD = perturb.quenchOff ? 0.0
		                                    : 2.0e-11 * std::exp( 107.8 / t ) * a.n2 + 2.9e-11 * std::exp( 67.5 / t ) * a.o2
		                                          + 3.0e-12 * a.o;
		const double a1S = 1.26 + 7.54e-2 + 2.42e-4, a1D = 5.63e-3 + 2.11e-5 + 1.82e-3 + 3.39e-6 + 8.6e-7;
		const double a5577 = 1.26, a630 = 5.63e-3 + 2.11e-5;
		//Emission per unit production.
		const double expected = ( rows[ i ].prod1D * a630 / ( a1D + qD ) ) / ( rows[ i ].prod1S * a5577 / ( a1S + qS ) );
		//From the texture: emission = A * S_col * shape; S_col = P_col * tau_eff.
		const float* s  = &shape[ ( static_cast< size_t >( e ) * nh + i ) * 4 ];
		const float* c  = &column[ static_cast< size_t >( e ) * 4 ];
		const double gG = 1.26 * c[ 0 ] * c[ 1 ] * s[ 0 ];
		const double gR = a630 * c[ 2 ] * c[ 3 ] * s[ 1 ];
		if( rows[ i ].prod1S <= 0.0 || gG <= 1e-30 || rows[ i ].ionisation < 1e-6 * rows[ 0 ].ionisation )
			continue;
		worst = std::max( worst, std::fabs( ( gR / gG ) / expected - 1.0 ) );
	}
	Check( worst <= 1e-5, fmt( "E0 = %.2f keV: texture ratio against the formula, worst %.2e over %d nodes (bound 1e-5: "
	                           "the texture is float32)",
	                           e0, worst, nh ) );

	//Where each line peaks, at 5 keV exactly, from the rates the table is built from.
	const std::vector< emission::Volume > five = emission::Profile( activity, 5.0, 1.0 );
	std::vector< double > green( nh ), red( nh );
	for( int i = 0; i < nh; ++i )
	{
		const atmosphere::Air a = atmosphere::At( emission::HeightAt( i ), activity );
		const double lossD = perturb.quenchOff ? emission::kA1D : five[ i ].loss1D;
		const double lossS = perturb.quenchOff ? emission::kA1S : five[ i ].loss1S;
		green[ i ] = five[ i ].prod1S * emission::kA5577 / lossS;
		red[ i ]   = five[ i ].prod1D * emission::kA6300 / lossD;
		( void )a;
	}
	const double pg = emission::PeakHeight( green ), pr = emission::PeakHeight( red );
	Check( pr > 200.0, fmt( "E0 = 5 keV: 630.0 nm peaks at %.1f km (above 200 km)", pr ) );
	Check( pg < 150.0, fmt( "E0 = 5 keV: 557.7 nm peaks at %.1f km (below 150 km)", pg ) );
	return Verdict();
}

//===========================================================================
// --lifetime
//===========================================================================
int runLifetime( const Perturb& perturb )
{
	std::printf( "\n=== lifetime: with the precipitation off, each population decays as exp(-t / tau_eff)\n" );
	Rig rig;
	if( !rig.Init( 320, 180 ) )
		return 1;
	//One bright straight arc overhead, Knight 0 so every node has the base E0;
	//no wind, so the red population stays where it was made; 1x.
	rig.Set( PT_KNIGHT, 0.0f );
	rig.Set( PT_WIND, 0.5f );
	rig.Set( PT_DISTURBANCE, 0.0f );
	rig.Set( PT_SPEED, ParamFromSpeed( 1.0f ) );
	rig.Set( PT_OVAL_DISTANCE, 0.3f );
	rig.Set( PT_RAYS, 0.0f );
	const double e0 = emission::EnergyAt( 45 );
	rig.Set( PT_ENERGY, inverseGeometric( e0, 0.2, 20.0 ) );
	rig.plugin.SetDiffusionForTest( false );//transport is not what this measures
	if( !rig.Render( 900 ) )//15 s: green long steady, red partly built
		return 1;

	const int n      = rig.plugin.MapSize();
	const Floats s0  = rig.State();
	size_t best      = 0;
	for( size_t i = 0; i < static_cast< size_t >( n ) * n; ++i )
		if( s0[ i * 4 ] > s0[ best * 4 ] )
			best = i;

	const emission::Tables& t = rig.plugin.CurrentTables();
	int column = -1;
	for( int e = 0; e < emission::kEnergies; ++e )
		if( std::fabs( std::log( emission::EnergyAt( e ) / std::exp( s0[ best * 4 + 1 ] / s0[ best * 4 ] ) ) ) < 1e-3 )
			column = e;
	const double lnE   = s0[ best * 4 + 1 ] / s0[ best * 4 ];
	const double tauG  = perturb.lifetimeRadiative ? 1.0 / emission::kA1S : t.column[ static_cast< size_t >( column < 0 ? 45 : column ) * 4 + 1 ];
	const double tauR  = perturb.lifetimeRadiative ? 1.0 / emission::kA1D : t.column[ static_cast< size_t >( column < 0 ? 45 : column ) * 4 + 3 ];
	std::printf( "  the brightest texel: E0 = %.4f keV (table column %d at %.4f); tau_eff 557.7 = %.4f s, 630.0 = %.3f s\n",
	             std::exp( lnE ), column, e0, tauG, tauR );

	rig.Set( PT_FLUX, 0.0f );
	if( !rig.Render( 1 ) )
		return 1;
	const Floats marchNext = rig.March();
	const Floats s1        = rig.State();
	double prompt          = 0.0, greenLeft = 0.0;
	for( size_t i = 0; i < marchNext.size(); i += 4 )
	{
		prompt += std::fabs( marchNext[ i + 2 ] ) + std::fabs( marchNext[ i + 3 ] );
		greenLeft += marchNext[ i ];
	}
	Check( prompt == 0.0 && greenLeft > 0.0,
	       fmt( "the frame after Flux 0: 427.8 and 1P sum to %.3g exactly, while 557.7 still shows %.3g kR in total", prompt,
	            greenLeft ) );

	//Frame by frame: the host clock steps 1/60 s, the sky 1x.
	double worstG = 0.0, worstR = 0.0;
	const double g1 = s1[ best * 4 ], r1 = s1[ best * 4 + 2 ];
	int frames = 0;
	for( int f = 1; f <= 1200; ++f )
	{
		if( !rig.Render( 1 ) )
			return 1;
		if( f % 30 != 0 )
			continue;
		const Floats s = rig.State();
		const double t = f / 60.0;
		if( t <= 3.0 )
			worstG = std::max( worstG, std::fabs( s[ best * 4 ] / ( g1 * std::exp( -t / tauG ) ) - 1.0 ) );
		worstR = std::max( worstR, std::fabs( s[ best * 4 + 2 ] / ( r1 * std::exp( -t / tauR ) ) - 1.0 ) );
		frames = f;
	}
	//Tolerance: the per-frame decay factor is a float exp() of a float ratio,
	//a few ulp (~2e-7) relative each frame, compounding over n frames; and tau
	//comes from the float table. 3e-7 per frame.
	Check( worstG <= 3e-7 * 180, fmt( "557.7: over 3 s (180 frames) worst %.2e from exp(-t/%.4f) (bound %.1e)", worstG, tauG, 3e-7 * 180 ) );
	Check( worstR <= 3e-7 * frames, fmt( "630.0: over %.0f s (%d frames) worst %.2e from exp(-t/%.3f) (bound %.1e)",
	                                     frames / 60.0, frames, worstR, tauR, 3e-7 * frames ) );
	return Verdict();
}

//===========================================================================
// --colour
//===========================================================================
int runColour( const Perturb& perturb )
{
	std::printf( "\n=== colour: each line alone lands at its CIE 1931 chromaticity (XYZ, before gamut mapping)\n" );
	//The expected chromaticities, from the CVRL CIE 1931 2-degree table at
	//1 nm, interpolated -- written here as numbers, not read from Optics.cpp,
	//so a wrong table there fails. 557.7 alone; the red channel is 630.0 and
	//636.4 by their A values; the blue 427.8 with 391.4 at 0.65/0.20; the pink
	//the four 1P heads equally.
	struct Line
	{
		const char* name;
		int channel;
		double x, y;
	};
	auto xyOf = []( std::initializer_list< std::pair< double, std::array< double, 4 > > > parts ) {
		double X = 0, Y = 0, Z = 0;
		for( const auto& p : parts )
		{
			const double w = p.first / p.second[ 3 ];//photons -> energy: 1/lambda
			X += w * p.second[ 0 ];
			Y += w * p.second[ 1 ];
			Z += w * p.second[ 2 ];
		}
		return std::array< double, 2 > { X / ( X + Y + Z ), Y / ( X + Y + Z ) };
	};
	const std::array< double, 4 > l5577 = { 0.556814, 0.998586, 0.004631, 557.7339 };
	const std::array< double, 4 > l5550 = { 0.512050, 1.000000, 0.005750, 555.0 };
	const std::array< double, 4 > l6300 = { 0.641765, 0.264689, 0.000050, 630.0304 };
	const std::array< double, 4 > l6364 = { 0.515405, 0.204909, 0.000027, 636.3776 };
	const std::array< double, 4 > l4278 = { 0.256137, 0.009604, 1.244894, 427.81 };
	const std::array< double, 4 > l3914 = { 0.005012, 0.000142, 0.023696, 391.44 };
	const std::array< double, 4 > h1    = { 0.224714, 0.083932, 0.0, 654.5 };
	const std::array< double, 4 > h2    = { 0.142734, 0.052643, 0.0, 662.4 };
	const std::array< double, 4 > h3    = { 0.084650, 0.030981, 0.0, 670.5 };
	const std::array< double, 4 > h4    = { 0.050134, 0.018237, 0.0, 678.9 };
	const double r630 = 5.6511e-3 / ( 5.6511e-3 + 1.82339e-3 );
	const auto green  = perturb.colourNm > 0.0 ? xyOf( { { 1.0, l5550 } } ) : xyOf( { { 1.0, l5577 } } );
	const auto red    = xyOf( { { r630, l6300 }, { 1.0 - r630, l6364 } } );
	const auto blue   = xyOf( { { 1.0, l4278 }, { 3.25, l3914 } } );
	const auto pink   = xyOf( { { 0.25, h1 }, { 0.25, h2 }, { 0.25, h3 }, { 0.25, h4 } } );
	const Line lines[] = { { "557.7 nm", 0, green[ 0 ], green[ 1 ] },
		                   { "630.0 + 636.4 nm", 1, red[ 0 ], red[ 1 ] },
		                   { "427.8 + 391.4 nm", 2, blue[ 0 ], blue[ 1 ] },
		                   { "N2 1P heads", 3, pink[ 0 ], pink[ 1 ] } };

	for( const Line& line : lines )
	{
		Rig rig;
		if( !rig.Init( 256, 144 ) )
			return 1;
		rig.Set( PT_EXTINCTION, 0.0f );
		rig.Set( PT_ENERGY, inverseGeometric( line.channel == 1 ? 0.5 : 5.0, 0.2, 20.0 ) );
		rig.Set( PT_ACTIVITY, 1.0f );
		float mask[ 4 ] = { 0, 0, 0, 0 };
		mask[ line.channel ] = 1.0f;
		rig.plugin.SetChannelMaskForTest( mask[ 0 ], mask[ 1 ], mask[ 2 ], mask[ 3 ] );
		rig.plugin.SetOutputXYZForTest( true );
		if( !rig.Render( 600 ) )
			return 1;
		const Floats xyz = rig.Output();
		size_t best      = 0;
		for( size_t i = 0; i < xyz.size(); i += 4 )
			if( xyz[ i + 1 ] + xyz[ i ] + xyz[ i + 2 ] > xyz[ best + 1 ] + xyz[ best ] + xyz[ best + 2 ] )
				best = i;
		const double X = xyz[ best ], Y = xyz[ best + 1 ], Z = xyz[ best + 2 ];
		const double x = X / ( X + Y + Z ), y = Y / ( X + Y + Z );
		//Where sRGB would put it before the gamut map.
		const double in[ 3 ] = { X, Y, Z };
		double rgb[ 3 ];
		optics::XYZToLinearSRGB( in, rgb );
		const bool outside = std::min( { rgb[ 0 ], rgb[ 1 ], rgb[ 2 ] } ) < 0.0;
		Check( std::fabs( x - line.x ) <= 2e-4 && std::fabs( y - line.y ) <= 2e-4 && X + Y + Z > 0.0,
		       fmt( "%-17s xy = (%.5f, %.5f), CIE (%.5f, %.5f)%s (bound 2e-4: float XYZ)", line.name, x, y, line.x,
		            line.y, outside ? "; outside sRGB, gamut-mapped toward equal-Y grey" : "" ) );
	}
	return Verdict();
}

//===========================================================================
// --corona
//===========================================================================
struct Line2
{
	double cx, cy, dx, dy, weight;
};

/// Streaks in an image: threshold, flood fill, and each blob's principal axis.
std::vector< Line2 > streaks( const Floats& img, int w, int h, int channel )
{
	float peak = 0.0f;
	for( int i = 0; i < w * h; ++i )
		peak = std::max( peak, img[ static_cast< size_t >( i ) * 4 + channel ] );
	const float threshold = 0.05f * peak;
	std::vector< int > label( static_cast< size_t >( w ) * h, -1 );
	std::vector< Line2 > out;
	int next = 0;
	for( int start = 0; start < w * h; ++start )
	{
		if( label[ start ] >= 0 || img[ static_cast< size_t >( start ) * 4 + channel ] < threshold )
			continue;
		std::vector< int > stack { start }, members;
		label[ start ] = next;
		while( !stack.empty() )
		{
			const int p = stack.back();
			stack.pop_back();
			members.push_back( p );
			const int px = p % w, py = p / w;
			for( int dy = -1; dy <= 1; ++dy )
				for( int dx = -1; dx <= 1; ++dx )
				{
					const int qx = px + dx, qy = py + dy;
					if( qx < 0 || qy < 0 || qx >= w || qy >= h )
						continue;
					const int q = qy * w + qx;
					if( label[ q ] < 0 && img[ static_cast< size_t >( q ) * 4 + channel ] >= threshold )
					{
						label[ q ] = next;
						stack.push_back( q );
					}
				}
		}
		++next;
		if( members.size() < 20 )
			continue;
		bool touchesEdge = false;
		double sw = 0, sx = 0, sy = 0;
		for( int p : members )
		{
			const double v = img[ static_cast< size_t >( p ) * 4 + channel ];
			const int px = p % w, py = p / w;
			touchesEdge = touchesEdge || px == 0 || py == 0 || px == w - 1 || py == h - 1;
			sw += v;
			sx += v * px;
			sy += v * py;
		}
		if( touchesEdge )
			continue;
		const double cx = sx / sw, cy = sy / sw;
		double xx = 0, yy = 0, xy = 0;
		for( int p : members )
		{
			const double v = img[ static_cast< size_t >( p ) * 4 + channel ];
			const double ddx = p % w - cx, ddy = p / w - cy;
			xx += v * ddx * ddx;
			yy += v * ddy * ddy;
			xy += v * ddx * ddy;
		}
		const double angle = 0.5 * std::atan2( 2.0 * xy, xx - yy );
		const double major = 0.5 * ( xx + yy ) + std::sqrt( 0.25 * ( xx - yy ) * ( xx - yy ) + xy * xy );
		const double minor = 0.5 * ( xx + yy ) - std::sqrt( 0.25 * ( xx - yy ) * ( xx - yy ) + xy * xy );
		if( major < 16.0 * std::max( minor, 1e-9 ) )
			continue;//not a streak
		out.push_back( { cx, cy, std::cos( angle ), std::sin( angle ), std::sqrt( major / sw ) } );
	}
	return out;
}

/// The least-squares meeting point of lines, each weighted by its length.
bool meet( const std::vector< Line2 >& lines, double& x, double& y )
{
	double a = 0, b = 0, c = 0, d = 0, e = 0;
	for( const Line2& l : lines )
	{
		const double nx = -l.dy, ny = l.dx, w = l.weight * l.weight;
		const double r = nx * l.cx + ny * l.cy;
		a += w * nx * nx;
		b += w * nx * ny;
		c += w * ny * ny;
		d += w * nx * r;
		e += w * ny * r;
	}
	const double det = a * c - b * b;
	if( std::fabs( det ) < 1e-12 )
		return false;
	x = ( c * d - b * e ) / det;
	y = ( a * e - b * d ) / det;
	return true;
}

int runCorona( const Perturb& perturb )
{
	std::printf( "\n=== corona: field-aligned rays converge on the magnetic zenith, +-1 px, at two rasters\n" );
	for( int hemisphere = 0; hemisphere < 2; ++hemisphere )
		for( int raster : { 0, 1 } )
		{
			const int w = raster == 0 ? 640 : 1280, h = raster == 0 ? 360 : 720;
			Rig rig;
			if( !rig.Init( w, h ) )
				return 1;
			//Dots of precipitation around the magnetic zenith's footprint: each
			//lights one field line, which the camera sees as a straight streak.
			std::vector< engine::Node > nodes;
			std::vector< int > starts;
			const float oval = 0.0f;
			for( int i = 0; i < 10; ++i )
			{
				const double angle = 2.0 * kPi * ( i + 0.25 ) / 10.0;
				const double r     = 35.0 + 12.0 * ( i % 3 );
				starts.push_back( static_cast< int >( nodes.size() ) );
				nodes.push_back( { static_cast< float >( r * std::cos( angle ) ),
				                   static_cast< float >( -25.0 + r * std::sin( angle ) ), 1.0f, 0.0f, 0.0f, 1.0f } );
			}
			starts.push_back( static_cast< int >( nodes.size() ) );
			rig.plugin.FreezeNodesForTest( nodes, starts );
			rig.Set( PT_OVAL_DISTANCE, ( oval + 600.0f ) / 2000.0f );
			rig.Set( PT_HEMISPHERE, static_cast< float >( hemisphere ) );
			rig.Set( PT_THICKNESS, 0.0f );
			rig.Set( PT_RAYS, 0.0f );
			rig.Set( PT_AIRGLOW, 0.0f );
			rig.Set( PT_STARS, 0.0f );
			rig.Set( PT_DETAIL, 3.0f );//the full raster: the march IS the picture
			rig.Set( PT_ENERGY, inverseGeometric( 1.0, 0.2, 20.0 ) );
			//Look 12 degrees off the magnetic zenith, so the point is not
			//simply the middle of the frame.
			const double dip = 77.0;
			rig.Set( PT_DIP, static_cast< float >( ( dip - 60.0 ) / 25.0 ) );
			rig.Set( PT_LOOK_AZIMUTH, hemisphere == 0 ? 1.0f : 0.5f );//south / north
			rig.Set( PT_LOOK_ELEVATION, static_cast< float >( ( dip - 12.0 + 10.0 ) / 100.0 ) );
			rig.Set( PT_FOV, static_cast< float >( ( 70.0 - 15.0 ) / 135.0 ) );
			rig.Set( PT_ROLL, 0.62f );
			if( !rig.Render( 90 ) )
				return 1;
			const Floats img = rig.March();
			const View& v    = rig.plugin.CurrentView();

			const std::vector< Line2 > lines = streaks( img, w, h, 0 );
			double mx = 0, my = 0;
			const bool found = lines.size() >= 4 && meet( lines, mx, my );

			const double dipUsed = ( dip + perturb.coronaDip ) * kPi / 180.0;
			const double sign    = hemisphere == 1 ? -1.0 : 1.0;
			const Vec3 zenith    = { 0.0, -sign * std::cos( dipUsed ), std::sin( dipUsed ) };
			double px = 0, py = 0;
			project( v, zenith, w, h, px, py );
			const double miss = std::hypot( mx - px, my - py );
			Check( found && miss <= 1.0,
			       fmt( "%s %dx%d: %zu streaks meet at (%.2f, %.2f); the magnetic zenith projects to (%.2f, %.2f): %.2f px",
			            hemisphere ? "Australis" : "Borealis ", w, h, lines.size(), mx, my, px, py, miss ) );
		}
	return Verdict();
}

//===========================================================================
// --vanrhijn
//===========================================================================
int runVanRhijn( const Perturb& perturb )
{
	std::printf( "\n=== vanrhijn: the 97 km airglow layer brightens with zenith angle by van Rhijn's function\n" );
	//The layer is a Gaussian of sigma 3.5 km, not a sheet: its exact
	//brightening is van Rhijn averaged over the layer, which differs from the
	//thin-layer function by a computable amount. The tolerance is that
	//difference (evaluated here in double), plus float in the march.
	auto exact = []( double zDeg ) {
		const double s = optics::kAirglowSigma, h0 = optics::kAirglowKm;
		double sum = 0.0, norm = 0.0;
		for( int i = -4000; i <= 4000; ++i )
		{
			const double h = h0 + 5.0 * s * i / 4000.0;
			const double g = std::exp( -0.5 * ( h - h0 ) * ( h - h0 ) / ( s * s ) );
			sum += g * optics::VanRhijn( zDeg, h );
			norm += g;
		}
		return sum / norm;
	};
	for( int size : { 257, 513 } )
	{
		Rig rig;
		if( !rig.Init( size, size ) )
			return 1;
		rig.Set( PT_FLUX, 0.0f );
		rig.Set( PT_STARS, 0.0f );
		rig.Set( PT_EXTINCTION, 0.0f );
		rig.Set( PT_AIRGLOW, 1.0f );//1 kR at the zenith
		rig.Set( PT_CAMERA, 1.0f );
		rig.Set( PT_DETAIL, 3.0f );
		if( !rig.Render( 2 ) )
			return 1;
		const Floats img = rig.March();
		const View& v    = rig.plugin.CurrentView();
		double worstExcess = -1e30, worstErr = 0.0, worstZ = 0.0, worstAllow = 0.0;
		int samples = 0;
		for( int py = 0; py < size; py += 3 )
			for( int px = 0; px < size; px += 3 )
			{
				bool valid;
				const Vec3 d   = cameraRay( v, px, py, size, size, valid );
				const double z = std::acos( std::clamp( d.z, -1.0, 1.0 ) ) * 180.0 / kPi;
				if( !valid || z > 80.0 )
					continue;
				const double thin  = perturb.vanRhijnFlat ? 1.0 / std::cos( z * kPi / 180.0 ) : optics::VanRhijn( z, optics::kAirglowKm );
				const double got   = img[ ( static_cast< size_t >( py ) * size + px ) * 4 ];
				const double layer = std::fabs( exact( z ) - optics::VanRhijn( z, optics::kAirglowKm ) );
				const double allow = layer + 2e-5 * thin;
				const double err   = std::fabs( got - thin );
				if( err - allow > worstExcess )
				{
					worstExcess = err - allow;
					worstErr    = err;
					worstZ      = z;
					worstAllow  = allow;
				}
				++samples;
			}
		Check( worstExcess <= 0.0,
		       fmt( "%dx%d fisheye, %d pixels to z = 80: tightest |march - V(z)| %.2e kR at z = %.1f against %.2e allowed "
		            "(the layer's thickness plus 2e-5 relative for float)",
		            size, size, samples, worstErr, worstZ, worstAllow ) );
	}
	std::printf( "  (V(80) = %.4f for a thin layer at 97 km; sec(80) = %.4f)\n", optics::VanRhijn( 80.0, 97.0 ),
	             1.0 / std::cos( 80.0 * kPi / 180.0 ) );
	return Verdict();
}

//===========================================================================
// --extinction: the shipped GLSL, probed.
//===========================================================================
GLuint compileProbe( const std::string& fragment )
{
	auto compile = []( GLenum kind, const std::string& text ) {
		const GLuint shader = glCreateShader( kind );
		const char* src     = text.c_str();
		glShaderSource( shader, 1, &src, nullptr );
		glCompileShader( shader );
		GLint ok = 0;
		glGetShaderiv( shader, GL_COMPILE_STATUS, &ok );
		if( !ok )
		{
			char log[ 4096 ];
			glGetShaderInfoLog( shader, sizeof( log ), nullptr, log );
			std::fprintf( stderr, "probe shader: %s\n", log );
		}
		return shader;
	};
	const GLuint program = glCreateProgram();
	glAttachShader( program, compile( GL_VERTEX_SHADER, std::string( shaders::kVersion ) +
	                                                        "void main(){ vec2 p = vec2( gl_VertexID & 1, gl_VertexID >> 1 ) * 4.0 - 1.0;"
	                                                        " gl_Position = vec4( p, 0.0, 1.0 ); }\n" ) );
	glAttachShader( program, compile( GL_FRAGMENT_SHADER, fragment ) );
	glLinkProgram( program );
	return program;
}

int runExtinction( const Perturb& perturb )
{
	std::printf( "\n=== extinction: the shader's attenuation follows Kasten & Young per wavelength\n" );
	//The probe is kVersion + kCommon (the text the plugin compiles) + a main
	//that evaluates transmission() on a grid: the plugin's function, not a copy.
	const int nz = 91;
	const double nms[] = { 391.44, 427.81, 465.0, 550.0, 557.7339, 610.0, 630.0304, 670.5 };
	const int nl       = 8;
	std::string fragment = shaders::Assemble( R"(
uniform float Nm[ 8 ];
out vec4 fragColor;
void main()
{
	ivec2 p   = ivec2( gl_FragCoord.xy );
	fragColor = vec4( transmission( Nm[ p.y ], float( p.x ), 1.0 ), kastenYoung( float( p.x ) ), 0.0, 1.0 );
}
)" );
	const GLuint program = compileProbe( fragment );
	GLuint texture = 0, fbo = 0, vao = 0;
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D, texture );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, nz, nl, 0, GL_RGBA, GL_FLOAT, nullptr );
	glGenFramebuffers( 1, &fbo );
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0 );
	glGenVertexArrays( 1, &vao );
	glBindVertexArray( vao );
	glViewport( 0, 0, nz, nl );
	glUseProgram( program );
	float nm[ 8 ];
	for( int i = 0; i < nl; ++i )
		nm[ i ] = static_cast< float >( nms[ i ] );
	glUniform1fv( glGetUniformLocation( program, "Nm" ), nl, nm );
	glDrawArrays( GL_TRIANGLES, 0, 3 );
	const Floats out = readTexture( texture, nz, nl );
	glUseProgram( 0 );
	glBindVertexArray( 0 );
	glDeleteVertexArrays( 1, &vao );
	glDeleteFramebuffers( 1, &fbo );
	glDeleteTextures( 1, &texture );
	glDeleteProgram( program );

	double worstT = 0.0, worstX = 0.0;
	for( int l = 0; l < nl; ++l )
		for( int z = 0; z < nz; ++z )
		{
			const double airmass = perturb.extinctionSecant ? 1.0 / std::max( std::cos( z * kPi / 180.0 ), 1e-3 )
			                                                : optics::KastenYoung( z );
			const double tau     = optics::RayleighDepth( nms[ l ] ) + optics::AerosolDepth( nms[ l ] );
			const double want    = std::exp( -tau * airmass );
			const float* o       = &out[ ( static_cast< size_t >( l ) * nz + z ) * 4 ];
			worstT = std::max( worstT, std::fabs( o[ 0 ] - want ) / want );
			if( l == 0 )
				worstX = std::max( worstX, std::fabs( o[ 1 ] - airmass ) / airmass );
		}
	Check( worstX <= 1e-5, fmt( "the airmass, 0..90 degrees: worst relative error %.2e against Kasten & Young (bound "
	                            "1e-5: float pow)", worstX ) );
	Check( worstT <= 1e-4, fmt( "transmission at 8 wavelengths x 91 zenith angles: worst relative error %.2e (bound 1e-4: "
	                            "float exp of an optical path up to ~15)", worstT ) );
	std::printf( "  at z = 85: 427.8 nm %.4f, 557.7 nm %.4f, 630.0 nm %.4f (airmass %.3f)\n",
	             optics::Transmission( 427.81, 85.0, 1.0 ), optics::Transmission( 557.7339, 85.0, 1.0 ),
	             optics::Transmission( 630.0304, 85.0, 1.0 ), optics::KastenYoung( 85.0 ) );
	return Verdict();
}

//===========================================================================
// --over-check
//===========================================================================
int runOver( const Perturb& perturb )
{
	std::printf( "\n=== over: the effect leaves the clip bit-exact where it should\n" );
	const int w = 320, h = 180;
	auto same = []( const Floats& a, const Floats& b, int& differ ) {
		differ = 0;
		for( size_t i = 0; i < a.size(); ++i )
			if( a[ i ] != b[ i ] )
				++differ;
		return differ == 0;
	};
	{
		const Floats card = buildCard( w, h, false );
		Rig rig( true );
		if( !rig.Init( w, h, &card ) )
			return 1;
		rig.Set( PT_MIX, 0.0f );
		if( !rig.Render( 30 ) )
			return 1;
		int differ = 0;
		Check( same( rig.Output(), card, differ ), fmt( "Mix 0: output == clip, %d of %zu floats differ", differ, card.size() ) );
	}
	{
		const Floats card = buildCard( w, h, false );
		Rig rig( true );
		if( !rig.Init( w, h, &card ) )
			return 1;
		rig.Set( PT_FLUX, 0.0f );
		rig.Set( PT_AIRGLOW, perturb.overAirglow ? 0.5f : 0.0f );
		rig.Set( PT_STARS, 0.0f );
		rig.Set( PT_ILLUMINATION, 1.0f );
		if( !rig.Render( 30 ) )
			return 1;
		int differ = 0;
		Check( same( rig.Output(), card, differ ),
		       fmt( "Flux 0, Airglow 0, Stars 0 (Illumination 1): output == clip, %d floats differ", differ ) );
	}
	{
		const Floats card = buildCard( w, h, true );
		Rig rig( true );
		if( !rig.Init( w, h, &card ) )
			return 1;
		rig.Set( PT_SKY_MASK, 1.0f );
		rig.Set( PT_ILLUMINATION, 0.0f );
		if( !rig.Render( 30 ) )
			return 1;
		const Floats out = rig.Output();
		int kept = 0, changed = 0, lit = 0;
		for( size_t i = 0; i < card.size(); i += 4 )
		{
			if( card[ i + 3 ] == 1.0f )
			{
				if( out[ i ] != card[ i ] || out[ i + 1 ] != card[ i + 1 ] || out[ i + 2 ] != card[ i + 2 ] || out[ i + 3 ] != 1.0f )
					++changed;
				else
					++kept;
			}
			else if( out[ i + 1 ] > 0.0f )
				++lit;
		}
		Check( changed == 0 && kept > 0 && lit > 0,
		       fmt( "Sky Mask Alpha: %d opaque pixels kept exactly, %d changed; %d sky pixels filled with the aurora", kept,
		            changed, lit ) );
	}
	return Verdict();
}

//===========================================================================
// --determinism
//===========================================================================
int runDeterminism( const Perturb& perturb )
{
	std::printf( "\n=== determinism: the same seed gives the same frames, bit for bit\n" );
	auto film = []( float seed, int frames ) {
		Rig rig;
		rig.Init( 320, 180 );
		rig.Set( PT_SEED, seed );
		rig.Set( PT_SPEED, ParamFromSpeed( 16.0f ) );
		std::vector< Floats > shots;
		for( int f = 0; f < frames; ++f )
		{
			if( f == 100 )
				rig.Press( PT_SUBSTORM );
			rig.Render( 1 );
			if( f % 60 == 59 )
				shots.push_back( rig.Output() );
		}
		return shots;
	};
	const auto a = film( 3.0f, 300 ), b = film( 3.0f, 300 ), c = film( perturb.determinismSeeds ? 4.0f : 3.0f, 300 );
	int differ = 0, differSeed = 0;
	for( size_t s = 0; s < a.size(); ++s )
		for( size_t i = 0; i < a[ s ].size(); ++i )
		{
			differ += a[ s ][ i ] != b[ s ][ i ];
			differSeed += a[ s ][ i ] != c[ s ][ i ];
		}
	const auto d = film( 4.0f, 300 );
	int other = 0;
	for( size_t s = 0; s < a.size(); ++s )
		for( size_t i = 0; i < a[ s ].size(); ++i )
			other += a[ s ][ i ] != d[ s ][ i ];
	Check( differ == 0 && differSeed == 0,
	       fmt( "seed 3 twice (with a substorm, 16x, the worker thread): %d + %d floats differ over 5 frames", differ, differSeed ) );
	Check( other > 1000, fmt( "seed 3 against seed 4: %d floats differ", other ) );
	return Verdict();
}

//===========================================================================
// --onset
//===========================================================================
int runOnset( const Perturb& perturb )
{
	std::printf( "\n=== onset: the first hit after a clip trigger fires a substorm (the primed analyser)\n" );
	Rig rig;
	if( !rig.Init( 160, 90 ) )
		return 1;
	rig.plugin.SetUnprimedForTest( perturb.onsetUnprimed );
	rig.Set( PT_AUDIO_SUBSTORM, 0.8f );
	rig.feed = AudioFeed::Pulses;
	rig.Render( 150 );//2.5 s of beats
	const unsigned long long before = rig.plugin.SubstormsFired();

	//The trigger: the host's clock goes back to 0.35 s into a beat's decay,
	//with the music still playing. The next hit is at 0.5 s: 9 frames later.
	rig.clockOffset = 0.35 - static_cast< double >( rig.frame ) / rig.fps;
	rig.Render( 1 );
	const unsigned long long atTrigger = rig.plugin.SubstormsFired();
	rig.Render( 20 );
	const unsigned long long after = rig.plugin.SubstormsFired();
	Check( before >= 3, fmt( "before the trigger: %llu substorms from 5 beats", before ) );
	Check( atTrigger == before && after == atTrigger + 1,
	       fmt( "after the trigger: %llu on the trigger frame, %llu in the next 20 frames (one hit at 0.5 s)",
	            atTrigger - before, after - atTrigger ) );
	return Verdict();
}

//===========================================================================
// --defaults, --names
//===========================================================================
int runDefaults( const Perturb& perturb )
{
	std::printf( "\n=== defaults: preset row 1 is the constructor's defaults, and presets override\n" );
	BorealPlugin plugin( false );
	const unsigned int targets[ presets::kParamCount ] = {
		PT_HEMISPHERE, PT_OVAL_DISTANCE, PT_DIP,       PT_DECLINATION,   PT_SPEED,       PT_ARCS,
		PT_ARC_SPACING, PT_SHEET_STRENGTH, PT_CURL_SIZE, PT_DISTURBANCE, PT_DRIFT,       PT_ENERGY,
		PT_FLUX,       PT_KNIGHT,        PT_THICKNESS, PT_RAYS,          PT_ACTIVITY,    PT_WIND,
		PT_AIRGLOW,    PT_EXTINCTION,    PT_CAMERA,    PT_LOOK_AZIMUTH,  PT_LOOK_ELEVATION, PT_FOV,
		PT_ROLL,       PT_EXPOSURE,      PT_OBSERVER,  PT_STARS,         PT_STAR_MOTION, PT_HORIZON,
	};
	const presets::Preset& row = presets::kPresets[ perturb.defaultsShifted ? 1 : 0 ];
	int wrong = 0;
	for( int c = 0; c < presets::kParamCount; ++c )
		if( std::fabs( plugin.GetFloatParameter( targets[ c ] ) - row.v[ c ] ) > 1e-6f )
		{
			std::printf( "    %s: default %g, preset %g\n", plugin.GetParamName( targets[ c ] ),
			             plugin.GetFloatParameter( targets[ c ] ), row.v[ c ] );
			++wrong;
		}
	Check( wrong == 0, fmt( "%d columns: %d differ from row 1 (\"%s\")", presets::kParamCount, wrong, row.name ) );
	plugin.SetFloatParameter( PT_HEMISPHERE, 0.0f );
	plugin.SetFloatParameter( PT_PRESET, 6.0f );//Australis
	Check( plugin.Effective( PT_HEMISPHERE ) == 1.0f, "choosing Australis overrides the hemisphere" );
	plugin.SetFloatParameter( PT_PRESET, 0.0f );
	Check( plugin.Effective( PT_HEMISPHERE ) == 0.0f, "Custom hands the controls back" );
	return Verdict();
}

int runNames( const Perturb& )
{
	std::printf( "\n=== names: every parameter name unique and within FFGL's 16 characters\n" );
	for( bool effect : { false, true } )
	{
		BorealPlugin plugin( effect );
		std::map< std::string, int > seen;
		int longNames = 0, dupes = 0;
		for( unsigned int i = 0; i < plugin.ParamCount(); ++i )
		{
			const std::string name = plugin.GetParamName( i ) ? plugin.GetParamName( i ) : "";
			if( name.size() > 16 )
			{
				std::printf( "    too long: %s\n", name.c_str() );
				++longNames;
			}
			if( seen[ name ]++ > 0 )
				++dupes;
		}
		for( int p = 0; p < presets::kCount; ++p )
			longNames += std::strlen( presets::kPresets[ p ].name ) > 16;
		Check( longNames == 0 && dupes == 0, fmt( "%s: %u parameters, %d too long, %d duplicated", effect ? "SW Boreal Over" : "SW Boreal",
		                                          plugin.ParamCount(), longNames, dupes ) );
	}
	return Verdict();
}

//===========================================================================
// --state: millpond's, for both plugins.
//===========================================================================
int runState( const Perturb& )
{
	std::printf( "\n=== state: the GL state the host hands over is the state it gets back\n" );
	for( bool effect : { false, true } )
	{
		Rig rig( effect );
		if( !rig.Init( 320, 180 ) )
			return 1;
		rig.Set( PT_ILLUMINATION, 0.5f );
		GLuint hostArray = 0, hostBuffer = 0;
		glGenVertexArrays( 1, &hostArray );
		glGenBuffers( 1, &hostBuffer );
		int problems = 0;
		std::string what;
		for( int frame = 0; frame < 3; ++frame )
		{
			glBindFramebuffer( GL_FRAMEBUFFER, rig.outputFBO );
			glViewport( 7, 5, 300, 170 );
			glBindVertexArray( hostArray );
			glBindBuffer( GL_ARRAY_BUFFER, hostBuffer );
			glEnable( GL_BLEND );
			glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO );
			glClearColor( 0.2f, 0.3f, 0.4f, 0.5f );
			glEnable( GL_SCISSOR_TEST );
			glScissor( 0, 0, 320, 180 );
			glActiveTexture( GL_TEXTURE0 );
			glUseProgram( 0 );
			rig.plugin.SetTime( frame / 60.0 );
			if( rig.plugin.ProcessOpenGL( &rig.process ) != FF_SUCCESS )
				return 1;
			GLint viewport[ 4 ] = {}, array = 0, buffer = 0, program = 0, unit = 0, fbo = 0, src = 0, dst = 0;
			GLfloat clear[ 4 ] = {};
			glGetIntegerv( GL_VIEWPORT, viewport );
			glGetIntegerv( GL_VERTEX_ARRAY_BINDING, &array );
			glGetIntegerv( GL_ARRAY_BUFFER_BINDING, &buffer );
			glGetIntegerv( GL_CURRENT_PROGRAM, &program );
			glGetIntegerv( GL_ACTIVE_TEXTURE, &unit );
			glGetIntegerv( GL_FRAMEBUFFER_BINDING, &fbo );
			glGetIntegerv( GL_BLEND_SRC_RGB, &src );
			glGetIntegerv( GL_BLEND_DST_RGB, &dst );
			glGetFloatv( GL_COLOR_CLEAR_VALUE, clear );
			auto expect = [ & ]( bool ok, const char* name ) {
				if( !ok )
				{
					++problems;
					what += std::string( " " ) + name;
				}
			};
			expect( viewport[ 0 ] == 7 && viewport[ 1 ] == 5 && viewport[ 2 ] == 300 && viewport[ 3 ] == 170, "viewport" );
			expect( array == static_cast< GLint >( hostArray ), "vertex-array" );
			expect( buffer == static_cast< GLint >( hostBuffer ), "array-buffer" );
			expect( program == 0, "program" );
			expect( unit == GL_TEXTURE0, "active-unit" );
			expect( fbo == static_cast< GLint >( rig.outputFBO ), "framebuffer" );
			expect( glIsEnabled( GL_BLEND ) && src == GL_SRC_ALPHA && dst == GL_ONE_MINUS_SRC_ALPHA, "blend" );
			expect( glIsEnabled( GL_SCISSOR_TEST ), "scissor" );
			expect( clear[ 0 ] == 0.2f && clear[ 1 ] == 0.3f && clear[ 2 ] == 0.4f && clear[ 3 ] == 0.5f, "clear-colour" );
			for( int u = 0; u < 10; ++u )
			{
				GLint bound = 0;
				glActiveTexture( static_cast< GLenum >( GL_TEXTURE0 + u ) );
				glGetIntegerv( GL_TEXTURE_BINDING_2D, &bound );
				expect( bound == 0, "texture-unit" );
			}
			glActiveTexture( GL_TEXTURE0 );
		}
		glDisable( GL_SCISSOR_TEST );
		glDisable( GL_BLEND );
		glBindVertexArray( 0 );
		glBindBuffer( GL_ARRAY_BUFFER, 0 );
		glDeleteVertexArrays( 1, &hostArray );
		glDeleteBuffers( 1, &hostBuffer );
		Check( problems == 0, fmt( "%s, three frames: viewport, vertex array, array buffer, program, active unit, "
		                           "framebuffer, blend, scissor, clear colour, ten texture units (%d wrong:%s)",
		                           effect ? "Over" : "source", problems, what.empty() ? " none" : what.c_str() ) );
	}
	return Verdict();
}

//===========================================================================
// --engine: the CPU cost of the sheet.
//===========================================================================
int runEngine( const Perturb& )
{
	std::printf( "\n=== engine: one RK4 step of the Birkhoff-Rott sheet (4 evaluations of the N^2 sum)\n" );
	const int threads = std::clamp( static_cast< int >( std::thread::hardware_concurrency() ) / 2, 1, 4 );
	for( int n : { 512, 2048, 4096 } )
	{
		engine::Sheet sheet;
		sheet.settings.threads = threads;
		engine::Arc arc;
		for( int i = 0; i < n; ++i )
		{
			arc.x.push_back( -2048.0 + 4096.0 * i / n );
			arc.y.push_back( std::sin( i * 0.1 ) );
			arc.g.push_back( 4096.0 / n );
			arc.a.push_back( arc.x.back() );
		}
		sheet.arcs.push_back( arc );
		sheet.Step( 0.1 );
		const auto start = std::chrono::steady_clock::now();
		const int reps   = n > 2048 ? 5 : 20;
		for( int r = 0; r < reps; ++r )
			sheet.Step( 0.1 );
		const double ms = std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - start ).count() / reps;
		std::printf( "  N = %4d%s: %7.2f ms per step on %d threads\n", n, n == 4096 ? " (the cap)" : "", ms, threads );
	}
	std::printf( "  (a step is %.2f s of sky time at the defaults: at 4x, one step every %.0f frames)\n",
	             engine::Engine::StepFor( engine::Params {} ), engine::Engine::StepFor( engine::Params {} ) * 60.0 / 4.0 );
	return 0;
}

const std::vector< CheckEntry >& checks()
{
	static const std::vector< CheckEntry > list = {
		{ "kh", runKH },           { "invariants", runInvariants }, { "knight", runKnight },
		{ "deposition", runDeposition }, { "quench", runQuench },   { "lifetime", runLifetime },
		{ "colour", runColour },   { "corona", runCorona },         { "vanrhijn", runVanRhijn },
		{ "extinction", runExtinction }, { "over-check", runOver }, { "determinism", runDeterminism },
		{ "onset", runOnset },     { "defaults", runDefaults },     { "names", runNames },
		{ "state", runState },     { "engine", runEngine },
	};
	return list;
}

//===========================================================================
// --negative
//===========================================================================
int runNegative()
{
	struct Case
	{
		const char* name;
		CheckFn check;
		Perturb perturb;
		const char* what;
	};
	std::vector< Case > cases;
	auto add = [ & ]( const char* name, CheckFn fn, const char* what, std::function< void( Perturb& ) > set ) {
		Perturb p;
		set( p );
		cases.push_back( { name, fn, p, what } );
	};
	add( "kh", runKH, "predict with delta doubled", []( Perturb& p ) { p.khDelta = 2.0; } );
	add( "invariants", runInvariants, "run with the arcs' relaxation on (not Hamiltonian)", []( Perturb& p ) { p.invariantsRelax = true; } );
	add( "knight", runKnight, "expect E0 ~ gamma^0.5", []( Perturb& p ) { p.knightExponent = 0.5; } );
	add( "deposition", runDeposition, "a monoenergetic beam at 2 E0 instead of the Maxwellian", []( Perturb& p ) { p.depositionMono = true; } );
	add( "quench", runQuench, "expect no collisional quenching", []( Perturb& p ) { p.quenchOff = true; } );
	add( "lifetime", runLifetime, "expect the radiative lifetime 1/A", []( Perturb& p ) { p.lifetimeRadiative = true; } );
	add( "colour", runColour, "expect 557.7 at 555.0 nm's chromaticity", []( Perturb& p ) { p.colourNm = 555.0; } );
	add( "corona", runCorona, "expect the vanishing point for a dip 1 degree steeper", []( Perturb& p ) { p.coronaDip = 1.0; } );
	add( "vanrhijn", runVanRhijn, "expect a flat Earth (sec z)", []( Perturb& p ) { p.vanRhijnFlat = true; } );
	add( "extinction", runExtinction, "expect the plane-parallel airmass sec z", []( Perturb& p ) { p.extinctionSecant = true; } );
	add( "over", runOver, "expect identity with the airglow on", []( Perturb& p ) { p.overAirglow = true; } );
	add( "determinism", runDeterminism, "expect seeds 3 and 4 to agree", []( Perturb& p ) { p.determinismSeeds = true; } );
	add( "onset", runOnset, "run the analyser unprimed", []( Perturb& p ) { p.onsetUnprimed = true; } );
	add( "defaults", runDefaults, "expect preset row 2 to be the defaults", []( Perturb& p ) { p.defaultsShifted = true; } );

	int unfalsifiable = 0;
	for( const Case& c : cases )
	{
		std::printf( "\n=== negative control: %s -- %s\n", c.name, c.what );
		const int before = g_failures;
		g_failures       = 0;
		c.check( c.perturb );
		const int observed = g_failures;
		g_failures         = before;
		if( observed > 0 )
			std::printf( "  ok    %s failed %d check%s, as it must\n", c.name, observed, observed == 1 ? "" : "s" );
		else
		{
			std::printf( "  FAIL  %s PASSED against a wrong model -- it cannot fail, so it is not a check\n", c.name );
			++unfalsifiable;
		}
	}
	std::printf( "\nnegative controls: %zu wrong models, %d of them undetected\n", cases.size(), unfalsifiable );
	std::printf( "\n  %s\n", unfalsifiable == 0 ? "PASS" : "FAIL" );
	return unfalsifiable == 0 ? 0 : 1;
}
