#pragma once

#include <cstdint>
#include <vector>

/**
    The auroral arc as a vortex sheet.

    In the ionosphere the plasma drifts at v = E x B / B^2 = z x grad(phi) / B,
    so phi / B is a stream function and the space charge is its vorticity. A
    thin arc is a thin sheet of charge, and a thin sheet of vorticity moving
    in its own induced velocity is the Birkhoff-Rott problem (Hallinan & Davis
    1970; Hallinan 1976). Everything here is that, in km and seconds of sky
    time, in double.

    ------------------------------------------------------------ the kernel

    Periodic in x with period L (Krasny 1986's periodic kernel), regularised by
    a physical length delta:

        u_i = -1/(2L) sum_j W_j sinh Y / D
        v_i = +1/(2L) sum_j W_j sin X / D
        D   = cosh Y - cos X + c,   c = (1/2)(2 pi delta / L)^2
        X   = 2 pi (x_i - x_j) / L,   Y = 2 pi (y_i - y_j) / L

    Near a vortex, D ~ (2 pi / L)^2 (r^2 + delta^2) / 2, so delta is the
    familiar planar regularisation length and is what Curl Size sets.

    It is the gradient of psi = -(W / 4 pi) ln D, so the Hamiltonian
    H = -(1/4 pi) sum_{i<j} W_i W_j ln D_ij is conserved by the exact flow.

    ------------------------------------------------------------ growth rate

    Linearising a flat sheet of strength gamma (circulation per km) about
    y = 0, with k = 2 pi m / L and cosh mu = 1 + c (derivation in AGENTS.md):

        sigma^2 = (gamma k / 2)^2 e^{-m mu} (1 - e^{-m mu}) / ( m sinh mu )

    which tends to (gamma k / 2)^2 as delta -> 0 and to the planar
    (gamma k / 2)^2 e^{-k delta}(1 - e^{-k delta}) / (k delta) as L -> inf.
    `GrowthRate` is that formula; `brtest --kh` measures it.

    ------------------------------------------------------------ circulation

    Circulation belongs to SEGMENTS: G_j is the circulation between node j and
    node j+1 of an arc. A node's weight is the trapezoid W_i = (G_{i-1}+G_i)/2.
    Inserting a node halves one segment's G, which is exact in binary, so the
    total is conserved to the last bit through any number of insertions, and
    removing a node sums two -- exact to one rounding.
*/
namespace boreal::engine
{
struct Arc
{
	std::vector< double > x;///< km east, UNWRAPPED: node N would be node 0 + L
	std::vector< double > y;///< km poleward
	std::vector< double > g;///< segment circulation, km^2/s; segment j is node j -> j+1
	std::vector< double > a;///< Lagrangian label (km of the original arc), for the rays
	double baseY = 0.0;     ///< where the arc was laid down, km poleward
};

struct SheetSettings
{
	double period     = 4096.0;///< L, km
	double delta      = 8.0;   ///< the regularisation, km (Curl Size)
	double driftU     = 0.0;   ///< uniform eastward convection, km/s
	double spacingMax = 4.0;   ///< insert a node where neighbours are further apart, km
	int cap           = 4096;  ///< total nodes over all arcs
	bool insert       = true;
	bool remove       = true;
	double relaxTime  = 0.0;   ///< s; 0 = none (pure Birkhoff-Rott)
	int threads       = 1;
};

class Sheet
{
public:
	std::vector< Arc > arcs;
	SheetSettings settings;

	/// Total nodes over all arcs.
	int Count() const;

	/// Velocities of every node, flattened in arc order.
	void Velocity( const std::vector< double >& x, const std::vector< double >& y, std::vector< double >& u,
	               std::vector< double >& v ) const;

	/// One classical RK4 step of h seconds. Circulations are constant within it.
	void Step( double h );

	/// Insert where neighbours are too far apart (up to the cap), remove where
	/// they are much too close. Returns how many changed.
	int Refine();

	/// Invariants.
	double Circulation() const;///< sum of G
	double Impulse() const;    ///< sum W y: with the circulation, the sheet's centroid
	double Hamiltonian() const;

	/// Node weights, flattened.
	void Weights( std::vector< double >& w ) const;

	/// Keep every arc's x within one period of the origin, by whole periods.
	void Rewrap();

	/// How many insertions were refused because the cap was reached, ever.
	long long refused = 0;

private:
	void flatten( std::vector< double >& x, std::vector< double >& y ) const;
	std::vector< double > w_, x0_, y0_, xs_, ys_, k1u_, k1v_, k2u_, k2v_, k3u_, k3v_, k4u_, k4v_;
};

/// The linear growth rate of a single mode on the regularised periodic sheet.
/// gamma km/s (circulation per km), k 1/km, delta km, L km.
double GrowthRate( double gamma, double k, double delta, double period );

/// The same in the planar (L -> inf) limit.
double GrowthRatePlanar( double gamma, double k, double delta );

/// The wavenumber of fastest growth on the planar sheet: k* = x* / delta, where
/// x* maximises x e^{-x}(1 - e^{-x}) / x ... solved numerically once.
double FastestWavenumber( double delta );

/// The engine's random numbers: PCG32 (O'Neill 2014). Mirrored bit for bit in
/// the shader's `pcg()` (Shaders.cpp) for the rays and the stars.
class Pcg
{
public:
	explicit Pcg( uint64_t seed = 0x853c49e6748fea9bULL, uint64_t stream = 0xda3e39cb94b95bdbULL );
	uint32_t Next();
	double Uniform();///< [0, 1)
	double Normal(); ///< Box-Muller

private:
	uint64_t state     = 0;
	uint64_t increment = 0;
};

/// The integer hash both sides share (the PCG output permutation of one LCG
/// step: Jarzynski & Olano 2020's "pcg" hash).
uint32_t PcgHash( uint32_t v );

} // namespace boreal::engine
