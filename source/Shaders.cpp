#include "Shaders.h"

namespace boreal::shaders
{
const char* const kVersion = "#version 410 core\n";

//===========================================================================
// The library. No #version, no main: assembled into every pass and into the
// harness's probes.
//===========================================================================
const char* const kCommon = R"(
const float R_EARTH  = 6371.0;  //km
const float H_REF    = 110.0;   //km: the footprint plane, where the sheet lives
const float H_BOTTOM = 80.0;
const float H_TOP    = 800.0;
const float PI       = 3.14159265358979;

//= mirrored in engine/Sheet.cpp, PcgHash(). Integer only: the same on every GPU.
uint pcg( uint v )
{
	uint state = v * 747796405u + 2891336453u;
	uint word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}

float hash01( uint v )
{
	return float( pcg( v ) ) * ( 1.0 / 4294967296.0 );
}

//---------------------------------------------------------------------------
// The footprint map's coordinates. Each axis is asinh-stretched about the
// observer, so a texel is ~constant in ANGLE as seen from the ground: fine
// overhead, coarse on the horizon, where a km is a fraction of a pixel anyway.
//   tc = 0.5 + 0.5 asinh( km / a ) / S,   S = asinh( U / a )
//---------------------------------------------------------------------------
float toMap( float km, float a, float s )
{
	return 0.5 + 0.5 * asinh( km / a ) / s;
}

vec2 toMap2( vec2 km, float a, float s )
{
	return vec2( toMap( km.x, a, s ), toMap( km.y, a, s ) );
}

float fromMap( float tc, float a, float s )
{
	return a * sinh( ( 2.0 * tc - 1.0 ) * s );
}

vec2 fromMap2( vec2 tc, float a, float s )
{
	return vec2( fromMap( tc.x, a, s ), fromMap( tc.y, a, s ) );
}

//How many km one texel spans at `km`: d(km)/d(tc) / size.
float texelKm( float km, float a, float s, float size )
{
	return 2.0 * s * sqrt( a * a + km * km ) / size;
}

//---------------------------------------------------------------------------
// Geometry on a spherical Earth, in km, relative to the observer (x east,
// y north, z up; the Earth's centre at (0, 0, -R)). Written without ever
// forming R^2 - R^2: in float that cancellation is kilometres near the horizon.
//---------------------------------------------------------------------------

//Altitude of the point t km along a ray whose zenith cosine is dz.
float altitudeAlong( float t, float dz )
{
	float a = 2.0 * R_EARTH * dz * t + t * t;
	return a / ( sqrt( R_EARTH * R_EARTH + a ) + R_EARTH );
}

//How far along a ray (dz >= 0) the altitude reaches hh.
float distanceTo( float hh, float dz )
{
	float c = hh * ( 2.0 * R_EARTH + hh );
	return c / ( R_EARTH * dz + sqrt( R_EARTH * R_EARTH * dz * dz + c ) );
}

//Follow the (straight) field line through q, at altitude h, to H_REF, and
//return the footprint in magnetic (east, poleward) km, azimuthal-equidistant
//about the observer. `down` is the unit vector down the field line.
vec2 footprint( vec3 q, float h, vec3 down, vec2 magEast, vec2 magPole )
{
	float c    = ( h - H_REF ) * ( 2.0 * R_EARTH + h + H_REF );
	float b    = R_EARTH * down.z + dot( down, q );
	float s    = -c / ( b - sqrt( max( b * b - c, 0.0 ) ) );
	vec3 f     = q + s * down;
	float horiz = length( f.xy );
	float theta = atan( horiz, R_EARTH + f.z );
	vec2 xy     = horiz > 1e-6 ? ( R_EARTH + H_REF ) * theta * ( f.xy / horiz ) : vec2( 0.0 );
	return vec2( dot( xy, magEast ), dot( xy, magPole ) );
}

//---------------------------------------------------------------------------
// The camera. `uv` is 0..1 over the raster, (0,0) bottom left.
//   kind 0: rectilinear, `tanHalf` = tan( vertical FOV / 2 ).
//   kind 1: equidistant all-sky fisheye, zenith in the middle, the horizon on
//           the inscribed circle, look azimuth at the top, east on the LEFT
//           (looking up, as every all-sky camera records).
//= mirrored in tools/brtest, cameraRay().
//---------------------------------------------------------------------------
vec3 cameraRay( vec2 uv, vec2 raster, int kind, vec3 fwd, vec3 right, vec3 up, float tanHalf, float lookAz,
                out bool valid )
{
	valid = true;
	if( kind == 0 )
	{
		float aspect = raster.x / raster.y;
		vec2 p       = ( 2.0 * uv - 1.0 ) * vec2( aspect, 1.0 ) * tanHalf;
		return normalize( fwd + p.x * right + p.y * up );
	}
	vec2 d     = ( uv - 0.5 ) * raster;
	float r    = length( d ) / ( 0.5 * min( raster.x, raster.y ) );
	if( r > 1.0 )
		valid = false;
	float z    = min( r, 1.0 ) * 0.5 * PI;
	float az   = lookAz + atan( -d.x, d.y );
	return vec3( sin( z ) * sin( az ), sin( z ) * cos( az ), cos( z ) );
}

//---------------------------------------------------------------------------
// Extinction. Rayleigh (Hansen & Travis 1974) plus an Angstrom aerosol, along
// the Kasten & Young 1989 airmass.
//= mirrored in physics/Optics.cpp.
//---------------------------------------------------------------------------
float kastenYoung( float zenithDegrees )
{
	float z = clamp( zenithDegrees, 0.0, 90.0 );
	return 1.0 / ( cos( radians( z ) ) + 0.50572 * pow( 96.07995 - z, -1.6364 ) );
}

float rayleighDepth( float nm )
{
	float l = nm * 1e-3;
	float l2 = l * l;
	float l4 = l2 * l2;
	return 0.008569 / l4 * ( 1.0 + 0.0113 / l2 + 0.00013 / l4 );
}

float aerosolDepth( float nm )
{
	return 0.05 * pow( nm / 550.0, -1.3 );
}

float transmission( float nm, float zenithDegrees, float scale )
{
	return exp( -scale * ( rayleighDepth( nm ) + aerosolDepth( nm ) ) * kastenYoung( zenithDegrees ) );
}

//---------------------------------------------------------------------------
// Colour.
//---------------------------------------------------------------------------
vec3 xyzToLinearSRGB( vec3 c )
{
	return vec3( 3.2406 * c.x - 1.5372 * c.y - 0.4986 * c.z,
	            -0.9689 * c.x + 1.8758 * c.y + 0.0415 * c.z,
	             0.0557 * c.x - 0.2040 * c.y + 1.0570 * c.z );
}

//= mirrored in physics/Optics.cpp, GamutMap(). Toward the grey of the same
//luminance until the lowest component is zero: luminance kept exactly.
vec3 gamutMap( vec3 rgb )
{
	float y  = dot( rgb, vec3( 0.2126, 0.7152, 0.0722 ) );
	if( y <= 0.0 )
		return vec3( 0.0 );
	float lo = min( rgb.r, min( rgb.g, rgb.b ) );
	if( lo >= 0.0 )
		return rgb;
	float t = y / ( y - lo );
	return vec3( y ) + t * ( rgb - vec3( y ) );
}

vec3 encodeSRGB( vec3 c )
{
	c = clamp( c, 0.0, 1.0 );
	return mix( 12.92 * c, 1.055 * pow( c, vec3( 1.0 / 2.4 ) ) - 0.055, step( vec3( 0.0031308 ), c ) );
}

vec3 decodeSRGB( vec3 c )
{
	return mix( c / 12.92, pow( ( c + 0.055 ) / 1.055, vec3( 2.4 ) ), step( vec3( 0.04045 ), c ) );
}
)";

//===========================================================================
// The full-screen quad.
//===========================================================================
const char* const kQuadVertex = R"(
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;
out vec2 uv;
void main()
{
	gl_Position = vPosition;
	uv          = vUV;
}
)";

//===========================================================================
// 1. Splat. One quad per segment of the sheet, drawn into the footprint map
// with additive blending. Each fragment belongs to the segment whose slab
// (the projection parameter in [0, 1)) it falls in, so a polyline's joints
// are neither doubled nor dropped along it.
//===========================================================================
const char* const kSplatVertex = R"(
layout( location = 0 ) in vec4 Ends;  //A.east, A.pole, B.east, B.pole: km
layout( location = 1 ) in vec4 Values;//flux A, flux B, ln E0 A, ln E0 B
layout( location = 2 ) in vec3 Extra; //label A, label B, corner 0..5

uniform float MapA;
uniform float MapS;
uniform float MapSize;
uniform float Thickness;//km, 1 sigma

out vec4 vEnds;
out vec4 vValues;
out vec2 vLabels;

void main()
{
	vec2 a     = Ends.xy;
	vec2 b     = Ends.zw;
	vec2 along = b - a;
	float len  = length( along );
	vec2 t     = len > 1e-6 ? along / len : vec2( 1.0, 0.0 );
	vec2 n     = vec2( -t.y, t.x );

	int corner = int( Extra.z + 0.5 );
	//(end, side) for the two triangles: (0,-)(1,-)(1,+) (0,-)(1,+)(0,+)
	int endIndex = ( corner == 1 || corner == 2 || corner == 4 ) ? 1 : 0;
	float side   = ( corner == 2 || corner == 4 || corner == 5 ) ? 1.0 : -1.0;

	vec2 p      = endIndex == 0 ? a : b;
	float texel = max( texelKm( p.x, MapA, MapS, MapSize ), texelKm( p.y, MapA, MapS, MapSize ) );
	float reach = 4.0 * sqrt( Thickness * Thickness + 0.25 * texel * texel ) + 2.0 * texel;
	p += n * side * reach + t * ( endIndex == 0 ? -2.0 : 2.0 ) * texel;

	gl_Position = vec4( toMap2( p, MapA, MapS ) * 2.0 - 1.0, 0.0, 1.0 );
	vEnds       = Ends;
	vValues     = Values;
	vLabels     = Extra.xy;
}
)";

const char* const kSplatFragment = R"(
in vec4 vEnds;
in vec4 vValues;
in vec2 vLabels;

uniform float MapA;
uniform float MapS;
uniform float MapSize;
uniform float Thickness;
uniform float FluxScale;//erg cm^-2 s^-1 at the base sheet strength
uniform float LnEnergy; //ln of the base E0, keV
uniform float Rays;     //0..1
uniform float RayK[ 8 ];    //1/km
uniform float RayAmp[ 8 ];
uniform float RayPhase[ 8 ];//reduced on the CPU in double: never a raw clock in float

out vec4 fragColor;

void main()
{
	vec2 tc = gl_FragCoord.xy / MapSize;
	vec2 p  = fromMap2( tc, MapA, MapS );

	vec2 a  = vEnds.xy;
	vec2 ab = vEnds.zw - a;
	float l2 = dot( ab, ab );
	float t  = l2 > 0.0 ? dot( p - a, ab ) / l2 : 0.0;
	if( t < 0.0 || t >= 1.0 )
		discard;
	vec2 across = p - ( a + t * ab );
	float d2    = dot( across, across );

	//A curtain thinner than the texel is widened to it, and dimmed by the
	//same factor: the flux integrated across the curtain is kept.
	float texel = max( texelKm( p.x, MapA, MapS, MapSize ), texelKm( p.y, MapA, MapS, MapSize ) );
	float w2    = Thickness * Thickness + 0.25 * texel * texel;
	float flux  = FluxScale * mix( vValues.x, vValues.y, t ) * ( Thickness / sqrt( w2 ) ) * exp( -0.5 * d2 / w2 );

	//Rays: the one stochastic texture in the plugin. A seeded spectrum of
	//field-aligned filaments along the sheet (Alfvenic filamentation), carried
	//on the Lagrangian label so the rays travel with the sheet. The mean of the
	//modulation is 1 whatever Rays is, so it moves light and makes none.
	float label = mix( vLabels.x, vLabels.y, t );
	float s     = 0.0;
	for( int k = 0; k < 8; ++k )
		s += RayAmp[ k ] * cos( RayK[ k ] * label + RayPhase[ k ] );
	flux *= ( 1.0 - Rays ) + Rays * s * s;

	float lnE = LnEnergy + mix( vValues.z, vValues.w, t );
	fragColor = vec4( flux, flux * lnE, 0.0, 0.0 );
}
)";

//===========================================================================
// 2. Update. The exact exponential integrator, per texel:
//      S' = S e^{-dt/tau} + P tau (1 - e^{-dt/tau})
// for the O(1S) and O(1D) population columns, with P and tau from the
// production's ln E0; and the same for S ln E0, so the population carries the
// energy it was excited at. The red population is first advected by the
// neutral wind and spread by a small diffusion.
//===========================================================================
const char* const kUpdateFragment = R"(
uniform sampler2D State;     //(S_1S, S_1S lnE, S_1D, S_1D lnE)
uniform sampler2D Production;//(Q, Q lnE)
uniform sampler2D Columns;   //row 0: (P_1S per erg, tau_1S, P_1D per erg, tau_1D) against ln E0
uniform float LnLow;
uniform float LnHigh;
uniform float MapA;
uniform float MapS;
uniform float MapSize;
uniform float Dt;        //s of sky time this frame
uniform vec2 Wind;       //km/s in (magnetic east, poleward)
uniform float Diffusion; //km: the tap offset, sqrt( 8 D dt )

out vec4 fragColor;

vec4 columnsAt( float lnE )
{
	float x = clamp( ( lnE - LnLow ) / ( LnHigh - LnLow ), 0.0, 1.0 );
	return texture( Columns, vec2( ( x * 63.0 + 0.5 ) / 64.0, 0.25 ) );
}

vec2 advance( vec2 s, float production, float lnP, float tauP, float tauS, bool haveS )
{
	float tau  = haveS ? tauS : tauP;
	if( tau <= 0.0 || Dt <= 0.0 )
		return s;
	float keep = exp( -Dt / tau );
	float fill = tau * ( 1.0 - keep );
	return vec2( s.x * keep + production * fill, s.y * keep + production * lnP * fill );
}

void main()
{
	vec2 tc    = gl_FragCoord.xy / MapSize;
	vec4 state = texture( State, tc );

	//The red population drifts with the wind: read it from upwind.
	vec2 red = state.ba;
	if( Dt > 0.0 && ( Wind.x != 0.0 || Wind.y != 0.0 || Diffusion > 0.0 ) )
	{
		vec2 here = fromMap2( tc, MapA, MapS );
		vec2 from = here - Wind * Dt;
		red       = 0.5 * texture( State, toMap2( from, MapA, MapS ) ).ba;
		red += 0.125 * texture( State, toMap2( from + vec2( Diffusion, 0.0 ), MapA, MapS ) ).ba;
		red += 0.125 * texture( State, toMap2( from - vec2( Diffusion, 0.0 ), MapA, MapS ) ).ba;
		red += 0.125 * texture( State, toMap2( from + vec2( 0.0, Diffusion ), MapA, MapS ) ).ba;
		red += 0.125 * texture( State, toMap2( from - vec2( 0.0, Diffusion ), MapA, MapS ) ).ba;
	}

	vec2 prod   = texture( Production, tc ).rg;
	float lnP   = prod.r > 0.0 ? prod.g / prod.r : LnLow;
	vec4 atP    = columnsAt( lnP );

	bool haveG  = state.r > 0.0;
	bool haveR  = red.x > 0.0;
	float tauG  = haveG ? columnsAt( state.g / state.r ).y : atP.y;
	float tauR  = haveR ? columnsAt( red.y / red.x ).w : atP.w;

	vec2 green  = advance( state.rg, prod.r * atP.x, lnP, atP.y, tauG, haveG );
	vec2 redNew = advance( red, prod.r * atP.z, lnP, atP.w, tauR, haveR );
	fragColor   = vec4( green, redNew );
}
)";

//===========================================================================
// 3. Occupancy: each texel's vertical emission column, kR, all lines.
//===========================================================================
const char* const kOccupancyFragment = R"(
uniform sampler2D State;
uniform sampler2D Production;
uniform sampler2D Columns;//row 1: (427.8, 391.4, 1P column per erg, -)
uniform float LnLow;
uniform float LnHigh;
uniform float MapSize;
uniform float A5577;
uniform float ARed;//A(630.0) + A(636.4)

out vec4 fragColor;

void main()
{
	vec2 tc    = gl_FragCoord.xy / MapSize;
	vec4 state = texture( State, tc );
	vec2 prod  = texture( Production, tc ).rg;
	float lnP  = prod.r > 0.0 ? prod.g / prod.r : LnLow;
	float x    = clamp( ( lnP - LnLow ) / ( LnHigh - LnLow ), 0.0, 1.0 );
	vec4 prompt = texture( Columns, vec2( ( x * 63.0 + 0.5 ) / 64.0, 0.75 ) );
	float column = A5577 * state.r + ARed * state.b + prod.r * ( prompt.x + prompt.z );
	fragColor = vec4( column * 1e-9, 0.0, 0.0, 0.0 );
}
)";

//===========================================================================
// The march, and the sky's colour: a library shared by the march pass (4),
// the all-sky pass (5) and the composite (6).
//===========================================================================
const char* const kMarchLibrary = R"(
uniform sampler2D State;     //unit 0
uniform sampler2D Production;//unit 1
uniform sampler2D Occupancy; //unit 2, mipmapped
uniform sampler2D Shapes;    //unit 3: x = height 80..800 km, y = ln E0
uniform sampler2D Columns;   //unit 4
uniform float MapA;
uniform float MapS;
uniform float MapSize;
uniform float OccThreshold;//kR below which a map region counts as empty
uniform float LnLow;
uniform float LnHigh;
uniform vec3 FieldDown;
uniform vec2 MagEast;
uniform vec2 MagPole;
uniform float A5577;
uniform float ARed;
uniform float AirglowKR;   //zenith column
uniform float AirglowKm;
uniform float AirglowSigma;
uniform int MaxSteps;
uniform vec4 ChannelMask;  //1s; the harness isolates one line with it

//The colour of each spectral component: XYZ in cd m^-2 per kR (683 lm/W
//times the CMFs times the radiance of a kR), the scotopic luminance per kR,
//its wavelength and which march channel it belongs to.
uniform vec3 CompXYZ[ 9 ];
uniform float CompScotopic[ 9 ];
uniform float CompNm[ 9 ];
uniform int CompChannel[ 9 ];
uniform float Extinction;  //zenith optical depth scale
uniform float DisplayGain; //2^Exposure times the display scale
uniform int Observer;      //0 camera, 1 eye

const float WINDOW = 40.0;//km of ray per empty-space test
const float VSTEP  = 2.0; //km of altitude per sample, at most

float lnCoord( float lnE )
{
	float x = clamp( ( lnE - LnLow ) / ( LnHigh - LnLow ), 0.0, 1.0 );
	return ( x * 63.0 + 0.5 ) / 64.0;
}

vec4 shapeAt( float h, float lnE )
{
	return texture( Shapes, vec2( ( h - H_BOTTOM + 0.5 ) / ( H_TOP - H_BOTTOM + 1.0 ), lnCoord( lnE ) ) );
}

//The 557.7 nm airglow layer: a Gaussian in altitude, integrated along the
//ray in the altitude variable, where the integrand is smooth at every
//elevation: column = int eps(h) dt/dh dh, dt/dh = (R+h) / sqrt(R^2 dz^2 + h(2R+h)).
//Its brightening towards the horizon (van Rhijn) is that Jacobian; nobody
//writes it in.
float airglow( float dz )
{
	if( AirglowKR <= 0.0 )
		return 0.0;
	const int N = 64;
	float lo    = AirglowKm - 5.0 * AirglowSigma;
	float hi    = AirglowKm + 5.0 * AirglowSigma;
	float dh    = ( hi - lo ) / float( N );
	float sum   = 0.0;
	for( int i = 0; i <= N; ++i )
	{
		float h  = lo + dh * float( i );
		float wt = ( i == 0 || i == N ) ? 1.0 : ( ( i % 2 == 1 ) ? 4.0 : 2.0 );
		float z  = ( h - AirglowKm ) / AirglowSigma;
		float e  = exp( -0.5 * z * z ) / ( AirglowSigma * 2.5066282746 );
		float j  = ( R_EARTH + h ) / sqrt( R_EARTH * R_EARTH * dz * dz + h * ( 2.0 * R_EARTH + h ) );
		sum += wt * e * j;
	}
	return AirglowKR * sum * dh / 3.0;
}

bool insideMap( vec2 tc )
{
	return all( greaterThanEqual( tc, vec2( 0.0 ) ) ) && all( lessThanEqual( tc, vec2( 1.0 ) ) );
}

vec2 mapAt( float t, vec3 d )
{
	float h = altitudeAlong( t, d.z );
	return toMap2( footprint( t * d, h, FieldDown, MagEast, MagPole ), MapA, MapS );
}

//Emission at one point, photons cm^-2 s^-1 per km of path, per channel.
vec4 emissionAt( float t, vec3 d )
{
	float h = altitudeAlong( t, d.z );
	vec2 tc = toMap2( footprint( t * d, h, FieldDown, MagEast, MagPole ), MapA, MapS );
	if( !insideMap( tc ) )
		return vec4( 0.0 );
	vec4 st   = texture( State, tc );
	vec2 pr   = texture( Production, tc ).rg;
	float lnP = pr.r > 0.0 ? pr.g / pr.r : LnLow;
	vec4 pc   = texture( Columns, vec2( lnCoord( lnP ), 0.75 ) );
	vec4 e    = vec4( 0.0 );
	if( st.r > 0.0 )
		e.x = A5577 * st.r * shapeAt( h, st.g / st.r ).x;
	if( st.b > 0.0 )
		e.y = ARed * st.b * shapeAt( h, st.a / st.b ).y;
	if( pr.r > 0.0 )
	{
		vec4 s = shapeAt( h, lnP );
		e.z    = pr.r * pc.x * s.z;
		e.w    = pr.r * pc.z * s.w;
	}
	return e;
}

//The four channels' columns along a ray, kR, before extinction.
vec4 marchRay( vec3 d )
{
	vec4 sum = vec4( 0.0 );
	if( d.z <= 0.0 )
		return sum;
	sum.x += airglow( d.z );

	float t     = distanceTo( H_BOTTOM, d.z );
	float tEnd  = distanceTo( H_TOP, d.z );
	int budget  = MaxSteps;
	vec4 photons = vec4( 0.0 );
	while( t < tEnd && budget > 0 )
	{
		float span = min( WINDOW, tEnd - t );
		vec2 f0    = mapAt( t, d );
		vec2 f1    = mapAt( t + span, d );
		vec2 fm    = mapAt( t + 0.5 * span, d );
		budget -= 1;

		//Empty-space skipping: the mean of a mip level is zero exactly where
		//every texel under it is (the data are never negative).
		vec2 lo     = min( f0, min( f1, fm ) );
		vec2 hi     = max( f0, max( f1, fm ) );
		float reach = max( hi.x - lo.x, hi.y - lo.y ) * MapSize;
		float level = ceil( log2( max( reach, 1.0 ) ) ) + 1.0;
		bool offMap = ( hi.x < 0.0 || hi.y < 0.0 || lo.x > 1.0 || lo.y > 1.0 );
		if( offMap || textureLod( Occupancy, clamp( 0.5 * ( lo + hi ), 0.0, 1.0 ), level ).r < OccThreshold )
		{
			t += span;
			continue;
		}

		//Fine steps: no more than about a texel of footprint, and no more
		//than VSTEP of altitude, per sample.
		float h0    = altitudeAlong( t, d.z );
		float h1    = altitudeAlong( t + span, d.z );
		float rate  = max( reach / span, 1e-4 );
		float climb = max( abs( h1 - h0 ) / span, 1e-4 );
		float step  = min( span, min( 0.8 / rate, VSTEP / climb ) );
		int n       = int( ceil( span / max( step, span / 96.0 ) ) );
		n           = min( n, max( budget, 1 ) );
		float dt    = span / float( n );
		for( int i = 0; i < n; ++i )
			photons += emissionAt( t + ( float( i ) + 0.5 ) * dt, d ) * dt;
		budget -= n;
		t += span;
	}
	return ( sum + photons * 1e-9 ) * ChannelMask;
}

//The sky's colour at a direction with zenith angle z (degrees), in display-
//linear RGB. Extinction per component; then the camera (sensor colour, gamut
//mapped) or the eye (CIE 191:2010 mesopic: luminance from the mesopic
//curve, colour faded with the adaptation coefficient m).
vec3 skyXYZ( vec4 kR, float zenith, out float scotopic )
{
	vec3 xyz = vec3( 0.0 );
	scotopic = 0.0;
	for( int c = 0; c < 9; ++c )
	{
		float amount = kR[ CompChannel[ c ] ] * transmission( CompNm[ c ], zenith, Extinction );
		xyz += amount * CompXYZ[ c ];
		scotopic += amount * CompScotopic[ c ];
	}
	return xyz;
}

vec3 skyColour( vec4 kR, float zenith )
{
	float scot;
	vec3 xyz  = skyXYZ( kR, zenith, scot );
	vec3 rgb  = gamutMap( xyzToLinearSRGB( xyz ) );
	if( Observer == 0 )
		return rgb * DisplayGain;

	float lp = xyz.y;
	float v0 = 683.0 / 1699.0;
	float m  = 0.5;
	float lm = 0.0;
	for( int i = 0; i < 8; ++i )
	{
		lm = ( m * lp + ( 1.0 - m ) * scot * v0 ) / ( m + ( 1.0 - m ) * v0 );
		m  = lm <= 0.005 ? 0.0 : ( lm >= 5.0 ? 1.0 : clamp( 0.7670 + 0.3334 * log( lm ) / log( 10.0 ), 0.0, 1.0 ) );
	}
	lm = ( m * lp + ( 1.0 - m ) * scot * v0 ) / ( m + ( 1.0 - m ) * v0 );
	vec3 chroma = lp > 0.0 ? rgb / lp : vec3( 1.0 );
	return mix( vec3( 1.0 ), chroma, m ) * lm * DisplayGain;
}
)";

//===========================================================================
// 4. March, at the Detail raster.
//===========================================================================
const char* const kMarchFragment = R"(
in vec2 uv;
uniform vec2 Raster;
uniform int CameraKind;
uniform vec3 Forward;
uniform vec3 Right;
uniform vec3 Up;
uniform float TanHalf;
uniform float LookAz;
out vec4 fragColor;

void main()
{
	bool valid;
	vec3 d    = cameraRay( uv, Raster, CameraKind, Forward, Right, Up, TanHalf, LookAz, valid );
	fragColor = valid ? marchRay( d ) : vec4( 0.0 );
}
)";

//===========================================================================
// 5. The whole sky, 32 x 32 equidistant, cosine-weighted: its mean is the
// illuminance on a horizontal surface over pi, i.e. what a white Lambertian
// ground would reflect.
//===========================================================================
const char* const kAllSkyFragment = R"(
in vec2 uv;
out vec4 fragColor;

void main()
{
	vec2 d  = ( uv - 0.5 ) * 2.0;
	float r = length( d );
	if( r > 1.0 )
	{
		fragColor = vec4( 0.0 );
		return;
	}
	float z   = r * 0.5 * PI;
	float az  = atan( d.x, d.y );
	vec3 dir  = vec3( sin( z ) * sin( az ), sin( z ) * cos( az ), cos( z ) );
	vec3 rgb  = skyColour( marchRay( dir ), degrees( z ) );
	//Texel solid angle for an equidistant map: (sin z / z) (dtheta)^2, with
	//dtheta = (pi/2) / 16 per texel. Times cos z / pi: a uniform sky of L gives L.
	float dth = 0.5 * PI / 16.0;
	float sol = ( z > 1e-4 ? sin( z ) / z : 1.0 ) * dth * dth;
	fragColor = vec4( rgb * cos( z ) * sol / PI, 1.0 );
}
)";

//===========================================================================
// 6. Composite, at the output raster.
//===========================================================================
const char* const kCompositeFragment = R"(
in vec2 uv;

uniform sampler2D InputTexture;//unit 5: the clip (Over only)
uniform sampler2D March;       //unit 6
uniform sampler2D AllSky;      //unit 7, mipmapped
uniform vec2 MaxUV;
uniform int IsEffect;
uniform vec2 Raster;           //output
uniform vec2 MarchSize;
uniform int CameraKind;
uniform vec3 Forward;
uniform vec3 Right;
uniform vec3 Up;
uniform float TanHalf;
uniform float LookAz;

uniform int OutputXYZ;         //the harness's colour check: XYZ, no gamut map, no stars
uniform float SkyLight;        //1 if AllSky holds this frame's sky
uniform float Illumination;    //k in 1 + k E
uniform int MaskMode;          //0 everything, 1 alpha, 2 dark areas
uniform float MaskThreshold;
uniform float MixAmount;

uniform int HorizonKind;       //0 none (transparent below), 1 flat, 2 hills
uniform uint HorizonSeed;
uniform float SnowAlbedo;

uniform float StarGain;
uniform mat3 LocalToSky;       //local (east, north, up) -> celestial, sidereal rotation included
uniform uint StarSeed;
uniform float PixelAngle;      //radians per output pixel, at the centre

//--------------------------------------------------------------------------
// The upsample: joint bilateral, keyed on the footprint where the ray crosses
// the reference height. A curtain's edge in the map is an edge in the guide,
// so the low-resolution march is not blurred across it.
//--------------------------------------------------------------------------
float guideAt( vec3 d )
{
	if( d.z <= 0.0 )
		return 0.0;
	float t = distanceTo( H_REF, d.z );
	vec2 tc = toMap2( footprint( t * d, H_REF, FieldDown, MagEast, MagPole ), MapA, MapS );
	if( !insideMap( tc ) )
		return 0.0;
	return log( 1.0 + 100.0 * texture( Occupancy, tc ).r );
}

vec4 upsampled( vec3 d )
{
	if( MarchSize == Raster )
		return texelFetch( March, ivec2( gl_FragCoord.xy ), 0 );

	vec2 p    = uv * MarchSize - 0.5;
	vec2 base = floor( p );
	vec2 f    = p - base;
	float g   = guideAt( d );
	vec4 sum  = vec4( 0.0 );
	float wsum = 0.0;
	vec4 plain = vec4( 0.0 );
	for( int j = 0; j < 2; ++j )
		for( int i = 0; i < 2; ++i )
		{
			ivec2 cell = ivec2( clamp( base + vec2( i, j ), vec2( 0.0 ), MarchSize - 1.0 ) );
			float wb   = ( i == 0 ? 1.0 - f.x : f.x ) * ( j == 0 ? 1.0 - f.y : f.y );
			vec4 v     = texelFetch( March, cell, 0 );
			bool ok;
			vec3 dn    = cameraRay( ( vec2( cell ) + 0.5 ) / MarchSize, Raster, CameraKind, Forward, Right, Up, TanHalf,
			                        LookAz, ok );
			float dg   = guideAt( dn ) - g;
			float w    = wb * exp( -2.0 * dg * dg );
			sum += w * v;
			wsum += w;
			plain += wb * v;
		}
	return wsum > 1e-3 ? sum / wsum : plain;
}

//--------------------------------------------------------------------------
// Stars: a seeded field on the celestial sphere, cells on a cube, at most one
// star per cell. Magnitudes from the power-law count N(<m) ~ 10^(0.5 m) up
// to m = 6.5; a mag 0 star gives 2.54e-6 lux (Allen's Astrophysical
// Quantities). Point spread 0.6 px (1 sigma).
//--------------------------------------------------------------------------
const int STAR_CELLS = 64;

vec3 starLight( vec3 dLocal, float zenith )
{
	if( StarGain <= 0.0 || dLocal.z <= -0.05 )
		return vec3( 0.0 );
	vec3 c  = LocalToSky * dLocal;
	vec3 a  = abs( c );
	int face;
	vec2 fuv;
	if( a.x >= a.y && a.x >= a.z )
	{
		face = c.x > 0.0 ? 0 : 1;
		fuv  = c.yz / a.x;
	}
	else if( a.y >= a.z )
	{
		face = c.y > 0.0 ? 2 : 3;
		fuv  = c.xz / a.y;
	}
	else
	{
		face = c.z > 0.0 ? 4 : 5;
		fuv  = c.xy / a.z;
	}
	vec2 cellF  = ( fuv * 0.5 + 0.5 ) * float( STAR_CELLS );
	ivec2 cell0 = ivec2( floor( cellF ) );
	float sigma = 0.6 * PixelAngle;
	vec3 light  = vec3( 0.0 );
	for( int j = -1; j <= 1; ++j )
		for( int i = -1; i <= 1; ++i )
		{
			ivec2 cell = clamp( cell0 + ivec2( i, j ), ivec2( 0 ), ivec2( STAR_CELLS - 1 ) );
			uint key   = pcg( StarSeed ^ pcg( uint( face ) * 7919u + uint( cell.x ) * 104729u + uint( cell.y ) ) );
			if( hash01( key ) > 0.30 )
				continue;
			vec2 at   = ( vec2( cell ) + vec2( hash01( key + 1u ), hash01( key + 2u ) ) ) / float( STAR_CELLS ) * 2.0 - 1.0;
			vec3 s;
			if( face == 0 )      s = vec3( 1.0, at.x, at.y );
			else if( face == 1 ) s = vec3( -1.0, at.x, at.y );
			else if( face == 2 ) s = vec3( at.x, 1.0, at.y );
			else if( face == 3 ) s = vec3( at.x, -1.0, at.y );
			else if( face == 4 ) s = vec3( at.x, at.y, 1.0 );
			else                 s = vec3( at.x, at.y, -1.0 );
			s = normalize( s );
			float chord = length( s - c );
			float theta = 2.0 * asin( min( 0.5 * chord, 1.0 ) );
			if( theta > 5.0 * sigma )
				continue;
			float mag   = max( 6.5 + 2.0 * log( max( hash01( key + 3u ), 1e-6 ) ) / log( 10.0 ), -1.5 );
			float lux   = 2.54e-6 * pow( 10.0, -0.4 * mag );
			float psf   = exp( -0.5 * theta * theta / ( sigma * sigma ) ) / ( 2.0 * PI * sigma * sigma );
			float warm  = hash01( key + 4u );
			vec3 tint   = mix( vec3( 0.80, 0.90, 1.12 ), vec3( 1.12, 0.93, 0.70 ), warm );
			tint /= dot( tint, vec3( 0.2126, 0.7152, 0.0722 ) );
			//Each channel extinct at its own effective wavelength.
			vec3 tr = vec3( transmission( 610.0, zenith, Extinction ), transmission( 550.0, zenith, Extinction ),
			                transmission( 465.0, zenith, Extinction ) );
			light += lux * psf * tint * tr;
		}
	return light * StarGain * DisplayGain;
}

//--------------------------------------------------------------------------
// The horizon's silhouette: hills from seeded 1-D value noise in azimuth.
//--------------------------------------------------------------------------
float hills( float azimuthDegrees )
{
	float h = 0.6;
	float amp = 2.2;
	float freq = 1.0 / 20.0;
	for( int o = 0; o < 5; ++o )
	{
		float x  = azimuthDegrees * freq;
		float i0 = floor( x );
		float f  = x - i0;
		f        = f * f * ( 3.0 - 2.0 * f );
		float period = 360.0 * freq;
		uint a   = uint( mod( i0, period ) );
		uint b   = uint( mod( i0 + 1.0, period ) );
		float va = hash01( pcg( HorizonSeed + uint( o ) * 1013u + a ) );
		float vb = hash01( pcg( HorizonSeed + uint( o ) * 1013u + b ) );
		h += amp * mix( va, vb, f );
		amp *= 0.5;
		freq *= 2.0;
	}
	return h;
}

out vec4 fragColor;

void main()
{
	bool valid;
	vec3 d       = cameraRay( uv, Raster, CameraKind, Forward, Right, Up, TanHalf, LookAz, valid );
	float zenith = degrees( acos( clamp( d.z, -1.0, 1.0 ) ) );
	vec4 kR      = valid ? upsampled( d ) : vec4( 0.0 );

	if( OutputXYZ == 1 )
	{
		float scot;
		fragColor = vec4( skyXYZ( kR, zenith, scot ) * DisplayGain, 1.0 );
		return;
	}

	vec3 sky = skyColour( kR, zenith ) + ( valid ? starLight( d, zenith ) : vec3( 0.0 ) );
	vec3 skyLight = SkyLight > 0.0 ? textureLod( AllSky, vec2( 0.5 ), 5.0 ).rgb * 1024.0 : vec3( 0.0 );

	if( IsEffect == 0 )
	{
		//Outside an all-sky camera's circle there is no sky and no ground.
		if( !valid )
		{
			fragColor = vec4( 0.0 );
			return;
		}
		float alpha = 1.0;
		float el    = 90.0 - zenith;
		float az    = degrees( atan( d.x, d.y ) );
		bool ground = ( HorizonKind == 1 && el < 0.0 ) || ( HorizonKind == 2 && el < hills( az < 0.0 ? az + 360.0 : az ) );
		if( ground )
			sky = SnowAlbedo * skyLight;//the snow, lit by the whole sky
		else if( HorizonKind == 0 && el < 0.0 )
		{
			sky   = vec3( 0.0 );
			alpha = 0.0;
		}
		fragColor = vec4( encodeSRGB( sky ), alpha );
		return;
	}

	//The effect: the aurora is LIGHT, added to the clip, never an alpha
	//composite of one picture over another.
	vec4 clip = texture( InputTexture, uv * MaxUV );
	float mask = 1.0;
	if( MaskMode == 2 )
	{
		float luma = dot( clip.rgb, vec3( 0.2126, 0.7152, 0.0722 ) );
		mask       = 1.0 - smoothstep( MaskThreshold - 0.05, MaskThreshold + 0.05, luma );
	}
	vec3 lift     = Illumination * skyLight;
	bool untouched = all( equal( lift, vec3( 0.0 ) ) );
	vec3 lit      = untouched ? clip.rgb : encodeSRGB( decodeSRGB( clip.rgb ) * ( 1.0 + lift ) );

	vec3 result;
	float alpha = clip.a;
	if( MaskMode == 1 )
	{
		//Alpha: the clip's transparent sky becomes the night sky; where the clip
		//is opaque it is exactly itself.
		result = mix( encodeSRGB( sky ), lit, clip.a );
		alpha  = mix( clip.a, 1.0, MixAmount );
	}
	else
	{
		vec3 light = sky * mask;
		result     = all( equal( light, vec3( 0.0 ) ) ) ? lit : encodeSRGB( decodeSRGB( lit ) + light );
	}
	fragColor = vec4( mix( clip.rgb, result, MixAmount ), alpha );
}
)";

std::string Assemble( const char* a, const char* b, const char* c )
{
	std::string s = kVersion;
	s += kCommon;
	for( const char* piece : { a, b, c } )
		if( piece )
			s += piece;
	return s;
}

} // namespace boreal::shaders
