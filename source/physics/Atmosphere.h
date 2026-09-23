#pragma once

/**
    The neutral atmosphere, 80-500 km.

    A baked NRLMSIS 2.1 table (AtmosphereTable.cpp; provenance in its header
    and in AGENTS.md), at two levels of solar activity, 2 km apart. Activity
    interpolates between them: log-linearly in the number densities and the
    mass density (they are exponential in height), linearly in temperature.
    Between nodes, the same, in height.

    Why MSIS and not the US Standard Atmosphere 1976: the 1976 standard has
    one thermosphere (T_inf = 1000 K), and Activity is the control that moves
    the exosphere temperature and with it the scale height, the O/N2 ratio and
    so where the red line lives. MSIS carries that dependence from data.
*/
namespace boreal::atmosphere
{
struct Row
{
	double n2, o2, o;///< cm^-3
	double t;        ///< K
	double rho;      ///< g cm^-3
};

extern const int kTableRows;
extern const double kTableBottomKm;
extern const double kTableStepKm;
extern const Row kTableQuiet[];
extern const Row kTableActive[];

struct Air
{
	double n2, o2, o;///< cm^-3
	double t;        ///< K
	double rho;      ///< g cm^-3
	double scaleHeight;///< cm: kT / (m g), m the mean molecular mass here
};

constexpr double kEarthRadiusKm = 6371.0;
constexpr double kBottomKm      = 80.0;
constexpr double kTopKm         = 500.0;

/// The air at `km`, for activity 0 (solar minimum) .. 1 (solar maximum).
Air At( double km, double activity );

} // namespace boreal::atmosphere
