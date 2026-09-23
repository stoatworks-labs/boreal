#pragma once

#include "engine/Sheet.h"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace boreal::engine
{
/// The engine's inputs, in physical units, for one frame.
struct Params
{
	int arcs            = 2;
	double spacing      = 60.0; ///< km between arcs
	double gamma        = 1.0;  ///< sheet strength, km/s (circulation per km)
	double delta        = 8.0;  ///< Curl Size, km
	double disturbance  = 2.0;  ///< km of seeded perturbation
	double drift        = 0.0;  ///< km/s eastward
	double knight       = 0.6;  ///< exponent of the Knight coupling, 0..1
	double relaxTime    = 300.0;///< s: the arcs re-form (0 = never)
	uint32_t seed       = 1;
	int cap             = 4096;
	int threads         = 4;
	bool forcing        = true; ///< keep seeding small perturbations
};

/// One node as the renderer needs it.
struct Node
{
	float x, y;   ///< km east, km poleward (wrapped into one period)
	float flux;   ///< energy flux relative to the base Flux (Knight, substorm, audio)
	float lnE0;   ///< ln of E0 relative to the base Energy
	float label;  ///< Lagrangian coordinate, km, for the rays
	float gamma;  ///< local sheet strength, km/s
};

struct Snapshot
{
	std::vector< Node > nodes;
	std::vector< int > arcStart;///< arcStart[a]..arcStart[a+1] are arc a's nodes, in order
	double skyTime   = 0.0;
	int steps        = 0;   ///< RK4 steps taken for this snapshot
	double cpuMs     = 0.0; ///< the worker's time for this job
	double gammaNow  = 0.0; ///< base sheet strength including any substorm boost
	int count        = 0;
	long long refused = 0;
};

/**
    The sky's dynamics, on a worker thread.

    A frame hands the engine a Job ("advance to sky time T with these
    parameters and these events") and takes the snapshot of the PREVIOUS job:
    a pipeline one frame deep. The render thread waits for the previous job
    before posting the next, so the worker overlaps the GPU's frame and the
    sequence of states is exactly the sequence of jobs -- the picture is a
    function of the frame index and the controls, never of how fast the
    machine ran. `brtest --determinism` holds it to that.

    The RK4 step h is fixed per job from the controls, in seconds of sky time.
*/
class Engine
{
public:
	struct Job
	{
		double skyTime = 0.0;
		Params params;
		int substorms  = 0;
		bool calm      = false;
	};

	Engine();
	~Engine();
	Engine( const Engine& )            = delete;
	Engine& operator=( const Engine& ) = delete;

	/// Hand the worker a job. Waits for the previous one first.
	void Submit( const Job& job );

	/// Wait for the last job, and return its snapshot.
	const Snapshot& Wait();

	/// Run a job on the calling thread (the harness, and a host with no
	/// threads to spare).
	void Run( const Job& job );

	/// The sheet itself, for the checks. Only safe after Wait().
	Sheet& SheetForTest()
	{
		return sheet;
	}

	/// The step the engine would take for these parameters, s.
	static double StepFor( const Params& p );

	/// Per node, from the sheet as it stands: the Knight relation. Public so
	/// `--knight` can hold it to the formula on a sheet it has wound up.
	static void Precipitation( const Sheet& sheet, const Params& p, double gammaNow, const std::vector< double >& arcGain,
	                           Snapshot& out );

private:
	void reset( const Params& p );
	void perturb( Arc& arc, double amplitude, double wavelength, double centre, double width );
	void loop();

	Sheet sheet;
	Snapshot snapshot;
	Pcg random;
	Params current;
	bool started       = false;
	double time        = 0.0;///< sky seconds the sheet has reached
	double boost       = 1.0;///< substorm multiplier on the sheet strength, decays to 1
	double surgeSpeed  = 0.0;///< westward speed of the poleward arc, km/s, decays
	double polewardGain = 1.0;///< substorm brightening of the poleward arc, decays
	double nextForcing = 0.0;
	int builtArcs      = -1;
	double builtSpacing = -1.0;
	uint32_t builtSeed = 0;

	std::thread worker;
	std::mutex mutex;
	std::condition_variable wake, done;
	Job pending;
	bool hasJob   = false;
	bool busy     = false;
	bool stopping = false;
};

} // namespace boreal::engine
