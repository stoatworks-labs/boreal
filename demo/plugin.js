/**
 * Boreal — browser demo.
 *
 * The aurora borealis and australis. The one idea, from `AGENTS.md`: **an
 * auroral arc is a sheet of charge drifting at E×B, and a charge sheet drifting
 * at E×B is a vortex sheet** — so the curls, folds and surges are the
 * Kelvin–Helmholtz rolling of a Birkhoff–Rott sheet, not a noise function. The
 * electrons the sheet accelerates (the Knight relation) deposit their energy
 * in an NRLMSIS atmosphere (Fang 2008), the O(¹S) and O(¹D) populations carry
 * their own lifetimes, and a camera on a spherical Earth marches its rays
 * through the 80–800 km shell.
 *
 * The plugin is two halves, and they are not equally faithful here:
 *
 *   **The GPU half is the plugin's own GLSL.** `COMMON`, `QUAD_VERTEX`,
 *   `SPLAT_VERTEX`, `SPLAT_FRAGMENT`, `UPDATE_FRAGMENT`, `OCCUPANCY_FRAGMENT`,
 *   `MARCH_LIBRARY`, `MARCH_FRAGMENT`, `ALLSKY_FRAGMENT` and
 *   `COMPOSITE_FRAGMENT` below are the `R"(...)"` bodies of
 *   `source/Shaders.cpp`, copied across unedited and assembled the way
 *   `Assemble()` assembles them, and run as the same six passes in the same
 *   order as `BorealPlugin::ProcessOpenGL`, at the same map sizes (1024² float
 *   footprint maps, a 32² all-sky). `demo/tools/check_shaders.py` compares
 *   them character for character — plus the NRLMSIS table and the preset rows —
 *   and `tools/verify.sh` runs it.
 *
 *   **The CPU half is a port** (`port.js`): the vortex sheet and its RK4, the
 *   engine's arcs, forcing, Substorm and Calm, the Knight relation, the
 *   atmosphere's interpolation, Fang 2008 and the emission tables, the colour
 *   components. Nothing checks a port but a reader. On the day it was written
 *   it was held against the C++ — same node count, same refusals, same sky
 *   time, node positions agreeing to about six figures after 600 frames and a
 *   substorm, the emission tables to every printed digit — but that was a
 *   one-off comparison, not a check that runs.
 *
 * ------------------------------------------------------ what is reduced
 *
 * **The node cap: 1024, not 4096.** The pair sum is O(N²); the plugin runs it
 * in C++ on up to four worker threads and takes ~14 ms a step at its cap. Here
 * it is one JavaScript thread on the page's main thread, where 1024 nodes costs
 * about 10 ms a step. The plugin's own at-cap behaviour then applies at 1024 —
 * insertion refused, the curls drawn coarser, the refusals counted — and the
 * sheet starts at 512 nodes an arc (8 km apart) rather than 1024 (4 km apart),
 * so its fine curls are coarser than the plugin's. The stats line under the
 * canvas says so with numbers.
 *
 * **The composition is 960 × 540 by default**, so the march runs at 480 × 270
 * at Detail Half. The plugin marches at Detail × whatever the host's raster is.
 *
 * ------------------------------------------------------ what is missing
 *
 * **Nothing audio.** The plugin declares an `Audio` FFT buffer that Resolume
 * fills and two amounts over it (Audio Substorm, Audio Flux). There is no
 * Resolume FFT in a browser, so all three are absent from this panel rather
 * than present and dead. Substorm is still here as the button it also is.
 *
 * **Substorm and Calm are toggles the page releases**, not events: the kit has
 * no FF_TYPE_EVENT. A press is one rising edge, exactly what the plugin counts.
 *
 * **Arcs and Seed are dropdowns.** They are FF_TYPE_INTEGER in the plugin
 * (1–5 and 0–9999); the kit has no integer control. Arcs offers all five;
 * Seed offers 0–99, not all ten thousand.
 *
 * **The About block is absent**, as on every page in this suite.
 *
 * ------------------------------------------------------ decided, not asked
 *
 * **Preset is the plugin's OVERRIDE**, reproduced: while it is on anything but
 * Custom the row is laid over the sliders at read time (`Effective()`), and the
 * sliders are, for those columns, not the truth. That is the plugin's
 * behaviour — Resolume does not consume value events — so the page does not
 * paper over it by writing the sliders.
 *
 * **Restart is a clock jump**, which the plugin takes as "no sky time passes"
 * rather than as running backwards: the sky carries on from where it was.
 * Calm is the control that starts it again.
 *
 * **Both plugins, one page.** `SW Boreal` (source) and `SW Boreal Over`
 * (effect) are one class with a flag, as in the repository; the Plugin
 * dropdown picks which. The Over group and the clip controls are shown only
 * for the effect, which is the only one that declares or reads them.
 */

import { mountDemo } from './vendor/demo.js';
import { Program, PassBuffer, bindTexture, GLError } from './vendor/gl.js';
import * as P from './port.js';

//---------------------------------------------------------------------------
// Shaders -- verbatim from source/Shaders.cpp. Do not edit here.
//
// The backticks inside five comments are escaped, because a template literal
// has nowhere else to go; check_shaders.py decodes that one escape before
// comparing and rejects any other backslash, so it cannot hide a difference.
//---------------------------------------------------------------------------

const VERSION = '#version 410 core\n';

const COMMON = `
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

//How many km one texel spans at \`km\`: d(km)/d(tc) / size.
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
//about the observer. \`down\` is the unit vector down the field line.
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
// The camera. \`uv\` is 0..1 over the raster, (0,0) bottom left.
//   kind 0: rectilinear, \`tanHalf\` = tan( vertical FOV / 2 ).
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
`;

const QUAD_VERTEX = `
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;
out vec2 uv;
void main()
{
	gl_Position = vPosition;
	uv          = vUV;
}
`;

const SPLAT_VERTEX = `
layout( location = 0 ) in vec4 Ends;  //A.east, A.pole, B.east, B.pole: km
layout( location = 1 ) in vec4 Values;//flux A, flux B, ln E0 A, ln E0 B
layout( location = 2 ) in vec3 Extra; //label A, label B (unused here), corner 0..5

uniform float MapA;
uniform float MapS;
uniform float MapSize;
uniform float Thickness;//km, 1 sigma

out vec4 vEnds;
out vec4 vValues;

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
}
`;

const SPLAT_FRAGMENT = `
in vec4 vEnds;
in vec4 vValues;

uniform float MapA;
uniform float MapS;
uniform float MapSize;
uniform float Thickness;
uniform float FluxScale;//erg cm^-2 s^-1 at the base sheet strength
uniform float LnEnergy; //ln of the base E0, keV

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

	float lnE = LnEnergy + mix( vValues.z, vValues.w, t );
	fragColor = vec4( flux, flux * lnE, 0.0, 0.0 );
}
`;

const UPDATE_FRAGMENT = `
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
`;

const OCCUPANCY_FRAGMENT = `
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
`;

const MARCH_LIBRARY = `
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
uniform int ClipSouthForTest;//0; 1 drops every footprint equatorward of the observer (--quadrants' wrong model)

//Rays: the one stochastic texture in the plugin. A seeded spectrum of
//field-aligned filaments (Alfvenic filamentation, which the model does not
//resolve), a function of the footprint's magnetic-east coordinate -- so each
//filament IS a field line -- evaluated here at the march's own resolution
//rather than baked into the footprint map, whose texel is ~2 km at an arc's
//usual distance. The phases carry the convection (reduced in double on the
//CPU). The mean of the modulation is 1 whatever Rays is: it moves light and
//makes none. It applies to the prompt lines and the green, not to the red,
//whose minutes-long population smears any filament out.
uniform float Rays;
uniform float RayK[ 8 ];   //1/km
uniform float RayAmp[ 8 ];
uniform float RayPhase[ 8 ];

//Averaged over the footprint span \`width\` (km of east) one sample stands
//for: each term is attenuated by its box filter, sinc( k w / 2 ), and the
//variance it loses is put back as a constant -- so a step coarser than a
//filament neither aliases it into blobs nor changes the mean. Seen along the
//arc the rays smear, as they do; seen across it they are sharp.
float rayModulation( float east, float width )
{
	if( Rays <= 0.0 )
		return 1.0;
	float s = 0.0, lost = 0.0;
	for( int k = 0; k < 8; ++k )
	{
		float x    = 0.5 * RayK[ k ] * width;
		float keep = x > 1e-3 ? sin( x ) / x : 1.0;
		s += keep * RayAmp[ k ] * cos( RayK[ k ] * east + RayPhase[ k ] );
		lost += 0.5 * RayAmp[ k ] * RayAmp[ k ] * ( 1.0 - keep * keep );
	}
	return ( 1.0 - Rays ) + Rays * ( s * s + lost );
}

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

vec2 footAt( float t, vec3 d )
{
	return footprint( t * d, altitudeAlong( t, d.z ), FieldDown, MagEast, MagPole );
}

//Emission at one point, photons cm^-2 s^-1 per km of path, per channel.
vec4 emissionAt( float t, vec3 d, float eastSpan )
{
	float h   = altitudeAlong( t, d.z );
	vec2 foot = footprint( t * d, h, FieldDown, MagEast, MagPole );
	vec2 tc   = toMap2( foot, MapA, MapS );
	if( !insideMap( tc ) || ( ClipSouthForTest == 1 && foot.y < 0.0 ) )
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
	float m = rayModulation( foot.x, eastSpan );
	return vec4( e.x * m, e.y, e.z * m, e.w * m );
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
		vec2 k0    = footAt( t, d );
		vec2 k1    = footAt( t + span, d );
		vec2 f0    = toMap2( k0, MapA, MapS );
		vec2 f1    = toMap2( k1, MapA, MapS );
		vec2 fm    = toMap2( footAt( t + 0.5 * span, d ), MapA, MapS );
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
		float east  = abs( k1.x - k0.x ) / float( n );//km of footprint east per sample
		for( int i = 0; i < n; ++i )
			photons += emissionAt( t + ( float( i ) + 0.5 ) * dt, d, east ) * dt;
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
`;

const MARCH_FRAGMENT = `
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
`;

const ALLSKY_FRAGMENT = `
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
`;

const COMPOSITE_FRAGMENT = `
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
`;

/// Assemble(): kVersion + kCommon + the pieces, in order.
const assemble = (...pieces) => VERSION + COMMON + pieces.join('');

//---------------------------------------------------------------------------
// Presets.h — verbatim rows, checked against the C++ by check_shaders.py.
// Standard columns hold the host's 0..1; option, integer and boolean columns
// hold the real value. Row 1 is the constructor's defaults.
//---------------------------------------------------------------------------
const PRESET_ROWS = [
  ['Boreal', [0, 0.425, 0.68, 0.5, 0.5, 2, 0.4223, 0.5624, 0.4076, 0.274, 0.55, 0.588, 0.8997, 0.6, 0.5886, 0.35, 0.4, 0.6333, 0.3162, 0.3333, 0, 0.5, 0.35, 0.5556, 0.5, 0.375, 0, 0.4, 1, 2]],
  ['Quiet Arc', [0, 0.62, 0.68, 0.5, 0.375, 1, 0.4223, 0.33, 0.55, 0.16, 0.52, 0.63, 0.80, 0.3, 0.66, 0.12, 0.2, 0.55, 0.3162, 0.3333, 0, 0.5, 0.22, 0.4, 0.5, 0.45, 0, 0.4, 1, 2]],
  ['Corona', [0, 0.2525, 0.68, 0.5, 0.5, 3, 0.45, 0.78, 0.45, 0.55, 0.5, 0.68, 0.93, 0.85, 0.35, 0.7, 0.5, 0.6333, 0.3162, 0.3333, 0, 1.0, 0.87, 0.5556, 0.5, 0.3, 0, 0.4, 1, 0]],
  ['Red Storm', [0, 0.45, 0.68, 0.5, 0.5, 2, 0.68, 0.4, 0.5, 0.35, 0.5, 0.2, 0.85, 0.4, 0.7, 0.2, 1.0, 0.83, 0.3162, 0.3333, 0, 0.5, 0.4, 0.6, 0.5, 0.5, 0, 0.4, 1, 2]],
  ['Rayed Band', [0, 0.44, 0.68, 0.5, 0.5, 1, 0.4223, 0.48, 0.75, 0.22, 0.55, 0.65, 0.92, 0.5, 0.3, 0.92, 0.4, 0.6333, 0.3162, 0.3333, 0, 0.5, 0.35, 0.5556, 0.5, 0.375, 0, 0.4, 1, 2]],
  ['Australis', [1, 0.5, 0.68, 0.5, 0.5, 2, 0.45, 0.56, 0.42, 0.3, 0.45, 0.55, 0.90, 0.6, 0.5, 0.4, 0.5, 0.6333, 0.3162, 0.3333, 0, 0.0, 0.3, 0.6, 0.5, 0.4, 0, 0.4, 1, 2]],
  ['Eye', [0, 0.425, 0.68, 0.5, 0.5, 2, 0.4223, 0.5624, 0.4076, 0.274, 0.55, 0.588, 0.8997, 0.6, 0.5886, 0.35, 0.4, 0.6333, 0.3162, 0.3333, 0, 0.5, 0.35, 0.5556, 0.5, 0.375, 1, 0.4, 1, 2]],
];

/// The page id each preset column drives, in presets::Param order (kPresetTarget).
const PRESET_TARGET = [
  'hemisphere', 'ovalDistance', 'dip', 'declination', 'speed', 'arcs',
  'arcSpacing', 'sheetStrength', 'curlSize', 'disturbance', 'drift', 'energy',
  'flux', 'knight', 'thickness', 'rays', 'activity', 'wind',
  'airglow', 'extinction', 'camera', 'lookAzimuth', 'lookElevation', 'fov',
  'roll', 'exposure', 'observer', 'stars', 'starMotion', 'horizon',
];
const ROW_ONE = Object.fromEntries(PRESET_TARGET.map((id, c) => [id, PRESET_ROWS[0][1][c]]));

//---------------------------------------------------------------------------
// Boreal.cpp constants.
//---------------------------------------------------------------------------
const PI = 3.14159265358979323846;
const kMapSize = 1024;
const kMapA = 80.0;
const kMapU = 2000.0;
const kAllSkySize = 32;
const kSnowAlbedo = 0.8;
const kObserverLatDeg = 69.65;
const kSiderealDay = 86164.0905;
const kDisplayScale = 250.0;
const kOccThreshold = 1e-5;
const kMaxMarchSteps = 400;
const kRayShortest = 0.5;
const kRedDiffusion = 0.5;
const kMaxFrameDelta = 0.25;
const kRayTerms = 8;
const kDetailFractions = [0.25, 0.5, 0.75, 1.0];
const MAP_S = Math.asinh(kMapU / kMapA);

/// The page's node cap. The plugin's is P.kPluginCap (4096); see the header.
const BROWSER_CAP = 1024;

/// Seed and Arcs are FF_TYPE_INTEGER; the kit has none, so they are dropdowns
/// and the index is not the plugin's value. These convert.
const SEED_CHOICES = 100;
const hostValue = {
  arcs: (index) => index + 1,
  seed: (index) => index,
};

const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);
const optionIndex = (value, count) => clamp(P.lround(value), 0, count - 1);

/// What the telemetry line reports. Filled by the renderer, read by a timer.
const telemetry = { nodes: 0, refused: 0, steps: 0, cpuMs: 0, skyTime: 0, speed: 0, substorms: 0 };

//---------------------------------------------------------------------------
// The renderer: BorealPlugin::ProcessOpenGL, pass for pass.
//---------------------------------------------------------------------------
function createRenderer(gl, quad) {
  // RGBA32F and R32F are sampled LINEARLY here, as they are in the plugin
  // (the update pass's advection, the occupancy mip chain). That is an
  // extension in WebGL2, and without it every one of those reads is black.
  if (!gl.getExtension('OES_texture_float_linear')) {
    throw new GLError('OES_texture_float_linear is missing. Boreal samples its 32-bit float footprint maps with linear filtering, and without it the populations cannot be read.');
  }

  const quadVertex = VERSION + QUAD_VERTEX;
  const splatProgram = new Program(gl, assemble(SPLAT_VERTEX), assemble(SPLAT_FRAGMENT), 'splat', {
    attribs: { Ends: 0, Values: 1, Extra: 2 },
  });
  const updateProgram = new Program(gl, quadVertex, assemble(UPDATE_FRAGMENT), 'update');
  const occupancyProgram = new Program(gl, quadVertex, assemble(OCCUPANCY_FRAGMENT), 'occupancy');
  const marchProgram = new Program(gl, quadVertex, assemble(MARCH_LIBRARY, MARCH_FRAGMENT), 'march');
  const allSkyProgram = new Program(gl, quadVertex, assemble(MARCH_LIBRARY, ALLSKY_FRAGMENT), 'allsky');
  const compositeProgram = new Program(gl, quadVertex, assemble(MARCH_LIBRARY, COMPOSITE_FRAGMENT), 'composite');

  // All 32-bit float, as in ensureBuffers(): the populations are fed back every
  // frame through a decay within 1e-4 of 1, which a half float cannot represent.
  const production = new PassBuffer(gl, { filter: 'linear' }).ensure(kMapSize, kMapSize, gl.RG32F);
  const state = [
    new PassBuffer(gl, { filter: 'linear' }).ensure(kMapSize, kMapSize, gl.RGBA32F),
    new PassBuffer(gl, { filter: 'linear' }).ensure(kMapSize, kMapSize, gl.RGBA32F),
  ];
  const occupancy = new PassBuffer(gl, { filter: 'linear', mip: true }).ensure(kMapSize, kMapSize, gl.R32F);
  const march = new PassBuffer(gl, { filter: 'nearest' });
  const allSky = new PassBuffer(gl, { filter: 'linear', mip: true }).ensure(kAllSkySize, kAllSkySize, gl.RGBA32F);
  let stateIndex = 0;

  const splatVAO = gl.createVertexArray();
  const splatVBO = gl.createBuffer();
  gl.bindVertexArray(splatVAO);
  gl.bindBuffer(gl.ARRAY_BUFFER, splatVBO);
  const stride = 11 * 4;
  gl.enableVertexAttribArray(0);
  gl.vertexAttribPointer(0, 4, gl.FLOAT, false, stride, 0);
  gl.enableVertexAttribArray(1);
  gl.vertexAttribPointer(1, 4, gl.FLOAT, false, stride, 16);
  gl.enableVertexAttribArray(2);
  gl.vertexAttribPointer(2, 3, gl.FLOAT, false, stride, 32);
  gl.bindVertexArray(null);
  gl.bindBuffer(gl.ARRAY_BUFFER, null);

  // The emission tables, rebuilt on a quantised Activity as ensureTables() does.
  const shapeTexture = gl.createTexture();
  const columnTexture = gl.createTexture();
  let tablesActivity = -1;
  function ensureTables(activity) {
    const wanted = Math.round(clamp(activity, 0, 1) * 64.0) / 64.0;
    if (wanted === tablesActivity) return;
    const tables = P.BuildTables(wanted);
    tablesActivity = wanted;
    const rows = new Float32Array(P.kEnergies * 4 * 2);
    rows.set(tables.column, 0);
    rows.set(tables.prompt, P.kEnergies * 4);
    for (const [texture, width, height, data] of [
      [shapeTexture, P.kHeights, P.kEnergies, tables.shape],
      [columnTexture, P.kEnergies, 2, rows],
    ]) {
      gl.bindTexture(gl.TEXTURE_2D, texture);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, width, height, 0, gl.RGBA, gl.FLOAT, data);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    }
    gl.bindTexture(gl.TEXTURE_2D, null);
  }

  const components = P.componentUniforms();
  const engine = new P.Engine();
  let shown = null;
  let lastNow = -1;
  let skyTime = 0;
  let substormPresses = 0;
  let calmWanted = false;
  let substormFired = 0;

  const unbind = (n) => {
    for (let unit = 0; unit < n; unit += 1) bindTexture(gl, unit, null);
    gl.activeTexture(gl.TEXTURE0);
  };

  return {
    render({ input, params, width, height, time, variant }) {
      const isEffect = variant === 'over';

      //------------------------------------------------------------------
      // The values the plugin renders with. Effective() lays the active
      // preset's row over the host's values at read time; Seed and the Over
      // group are read raw, as the plugin reads them.
      //------------------------------------------------------------------
      const raw = (id) => (hostValue[id] ? hostValue[id](params.get(id)) : params.get(id));
      const preset = optionIndex(params.get('preset'), PRESET_ROWS.length + 1);
      const eff = (id) => {
        if (preset > 0) {
          const c = PRESET_TARGET.indexOf(id);
          if (c >= 0) return PRESET_ROWS[preset - 1][1][c];
        }
        return raw(id);
      };

      // Events act on the rising edge, once. The page's toggles are released
      // here, in the frame that takes the press, as a host releases a button:
      // one press is one rising edge, which is all the plugin counts.
      if (params.get('substorm') > 0.5) {
        substormPresses += 1;
        params.set('substorm', 0);
      }
      if (params.get('calm') > 0.5) {
        calmWanted = true;
        params.set('calm', 0);
      }

      //------------------------------------------------------------------
      // Time: a backwards or large step is a jump, across which no sky time
      // passes. The kit's clock is already in seconds, so the plugin's vote on
      // the host's unit has nothing to decide here.
      //------------------------------------------------------------------
      let hostDt = 0;
      if (lastNow >= 0) {
        const step = time - lastNow;
        if (!(step < 0 || step > kMaxFrameDelta)) hostDt = step;
      }
      lastNow = time;
      const frameDt = hostDt * P.SpeedFromParam(eff('speed'));
      skyTime += frameDt;

      ensureTables(P.ActivityFromParam(eff('activity')));
      const detail = optionIndex(eff('detail'), 4);
      const mw = Math.max(8, P.lround(width * kDetailFractions[detail]));
      const mh = Math.max(8, P.lround(height * kDetailFractions[detail]));
      march.ensure(mw, mh, gl.RGBA32F);

      //------------------------------------------------------------------
      // buildView().
      //------------------------------------------------------------------
      const az = (P.LookAzimuthFromParam(eff('lookAzimuth')) * PI) / 180;
      const el = (P.LookElevationFromParam(eff('lookElevation')) * PI) / 180;
      const roll = (P.RollFromParam(eff('roll')) * PI) / 180;
      const fov = (P.FovFromParam(eff('fov')) * PI) / 180;
      const f = [Math.sin(az) * Math.cos(el), Math.cos(az) * Math.cos(el), Math.sin(el)];
      const r0 = [Math.cos(az), -Math.sin(az), 0];
      const u0 = [-Math.sin(az) * Math.sin(el), -Math.cos(az) * Math.sin(el), Math.cos(el)];
      const view = {
        forward: f,
        right: [0, 1, 2].map((i) => r0[i] * Math.cos(roll) + u0[i] * Math.sin(roll)),
        up: [0, 1, 2].map((i) => u0[i] * Math.cos(roll) - r0[i] * Math.sin(roll)),
        cameraKind: optionIndex(eff('camera'), 2),
        tanHalf: Math.tan(0.5 * fov),
        lookAz: az,
      };
      const dip = (P.DipFromParam(eff('dip')) * PI) / 180;
      const decl = (P.DeclinationFromParam(eff('declination')) * PI) / 180;
      const australis = optionIndex(eff('hemisphere'), 2) === 1;
      const sign = australis ? -1 : 1;
      const pole = [sign * Math.sin(decl), sign * Math.cos(decl)];
      view.magPole = pole;
      view.magEast = [Math.cos(decl), -Math.sin(decl)];
      view.fieldDown = [Math.cos(dip) * pole[0], Math.cos(dip) * pole[1], -Math.sin(dip)];

      //------------------------------------------------------------------
      // The engine: take the previous job's sheet, run this frame's. The
      // plugin posts the job to a worker and always waits for it before the
      // next; running it here after taking the previous snapshot is the same
      // sequence of states, one frame behind, without the thread.
      //------------------------------------------------------------------
      const job = {
        skyTime,
        params: {
          arcs: clamp(P.lround(eff('arcs')), 1, 5),
          spacing: Math.fround(P.ArcSpacingFromParam(eff('arcSpacing'))),
          gamma: Math.fround(P.SheetStrengthFromParam(eff('sheetStrength'))),
          delta: Math.fround(P.CurlSizeFromParam(eff('curlSize'))),
          disturbance: Math.fround(P.DisturbanceFromParam(eff('disturbance'))),
          drift: Math.fround(P.DriftFromParam(eff('drift'))),
          knight: Math.fround(P.KnightFromParam(eff('knight'))),
          seed: P.SeedFromParam(raw('seed')) + 1,
          relaxTime: 300.0,
          cap: BROWSER_CAP,
          forcing: true,
        },
        substorms: substormPresses,
        calm: calmWanted,
      };
      substormFired += substormPresses;
      substormPresses = 0;
      calmWanted = false;
      if (shown === null) {
        shown = engine.Run(job);
      } else {
        const previous = engine.snapshot;
        engine.Run(job);
        shown = previous;
      }
      telemetry.nodes = engine.snapshot.count;
      telemetry.refused = engine.snapshot.refused;
      // The cost of the last job that stepped: most jobs at 4x take none,
      // because one RK4 step of ~0.8 s of sky covers a dozen frames.
      if (engine.snapshot.steps > 0) {
        telemetry.steps = engine.snapshot.steps;
        telemetry.cpuMs = engine.snapshot.cpuMs;
      }
      telemetry.skyTime = skyTime;
      telemetry.speed = P.SpeedFromParam(eff('speed'));
      telemetry.substorms = substormFired;

      //------------------------------------------------------------------
      // 1. Splat.
      //------------------------------------------------------------------
      {
        const kPeriod = 4096.0;
        const oval = Math.fround(P.OvalDistanceFromParam(eff('ovalDistance')));
        const limit = kMapU + 60.0;
        const { nodes, arcStart } = shown;
        const vertices = new Float32Array(nodes.length / 6 * 6 * 11);
        let v = 0;
        for (let a = 0; a + 1 < arcStart.length; a += 1) {
          const begin = arcStart[a], end = arcStart[a + 1];
          const n = end - begin;
          const labelOffset = Math.fround(1777.7 * a);
          for (let i = 0; i < n; i += 1) {
            const na = (begin + i) * 6;
            const nb = (begin + ((i + 1) % n)) * 6;
            const ax = nodes[na], ay = Math.fround(nodes[na + 1] + oval);
            let bx = nodes[nb];
            bx = Math.fround(bx - kPeriod * P.lround((bx - ax) / kPeriod));
            const by = Math.fround(nodes[nb + 1] + oval);
            if ((Math.abs(ax) > limit && Math.abs(bx) > limit) || (Math.abs(ay) > limit && Math.abs(by) > limit)) continue;
            let lb = nodes[nb + 4];
            lb = Math.fround(lb - kPeriod * P.lround((lb - nodes[na + 4]) / kPeriod));
            for (let corner = 0; corner < 6; corner += 1) {
              vertices.set([ax, ay, bx, by, nodes[na + 2], nodes[nb + 2], nodes[na + 3], nodes[nb + 3],
                nodes[na + 4] + labelOffset, lb + labelOffset, corner], v);
              v += 11;
            }
          }
        }

        production.clearTo(0, 0, 0, 0);
        if (v > 0) {
          splatProgram.use();
          splatProgram.set('MapA', kMapA);
          splatProgram.set('MapS', MAP_S);
          splatProgram.set('MapSize', kMapSize);
          splatProgram.set('Thickness', P.ThicknessFromParam(eff('thickness')));
          // Audio Flux is 1 + 2 * amount * level; with no audio, 1.
          splatProgram.set('FluxScale', P.FluxFromParam(eff('flux')));
          splatProgram.set('LnEnergy', Math.log(P.EnergyFromParam(eff('energy'))));
          gl.enable(gl.BLEND);
          gl.blendFunc(gl.ONE, gl.ONE);
          gl.bindVertexArray(splatVAO);
          gl.bindBuffer(gl.ARRAY_BUFFER, splatVBO);
          gl.bufferData(gl.ARRAY_BUFFER, vertices.subarray(0, v), gl.STREAM_DRAW);
          gl.drawArrays(gl.TRIANGLES, 0, v / 11);
          gl.bindBuffer(gl.ARRAY_BUFFER, null);
          gl.bindVertexArray(null);
          gl.disable(gl.BLEND);
        }
      }

      //------------------------------------------------------------------
      // 2. Update the populations.
      //------------------------------------------------------------------
      {
        const target = state[1 - stateIndex];
        target.bind();
        updateProgram.use();
        bindTexture(gl, 0, state[stateIndex].texture);
        bindTexture(gl, 1, production.texture);
        bindTexture(gl, 2, columnTexture);
        updateProgram.setSampler('State', 0).setSampler('Production', 1).setSampler('Columns', 2);
        updateProgram.set('LnLow', Math.log(P.kLowKeV));
        updateProgram.set('LnHigh', Math.log(P.kHighKeV));
        updateProgram.set('MapA', kMapA);
        updateProgram.set('MapS', MAP_S);
        updateProgram.set('MapSize', kMapSize);
        updateProgram.set('Dt', frameDt);
        const wind = P.WindFromParam(eff('wind'));
        updateProgram.set('Wind', wind * view.magEast[0], wind * view.magPole[0]);
        updateProgram.set('Diffusion', Math.sqrt(8.0 * kRedDiffusion * Math.max(frameDt, 0)));
        quad.draw();
        unbind(3);
      }
      stateIndex = 1 - stateIndex;

      //------------------------------------------------------------------
      // 3. Occupancy, and its mip chain.
      //------------------------------------------------------------------
      {
        occupancy.bind();
        occupancyProgram.use();
        bindTexture(gl, 0, state[stateIndex].texture);
        bindTexture(gl, 1, production.texture);
        bindTexture(gl, 2, columnTexture);
        occupancyProgram.setSampler('State', 0).setSampler('Production', 1).setSampler('Columns', 2);
        occupancyProgram.set('LnLow', Math.log(P.kLowKeV));
        occupancyProgram.set('LnHigh', Math.log(P.kHighKeV));
        occupancyProgram.set('MapSize', kMapSize);
        occupancyProgram.set('A5577', P.kA5577);
        occupancyProgram.set('ARed', P.kA6300 + P.kA6364);
        quad.draw();
        unbind(3);
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        occupancy.generateMipmap();
      }

      //------------------------------------------------------------------
      // setMarchUniforms(), for passes 4, 5 and 6.
      //------------------------------------------------------------------
      const bindMarchInputs = () => {
        bindTexture(gl, 0, state[stateIndex].texture);
        bindTexture(gl, 1, production.texture);
        bindTexture(gl, 2, occupancy.texture);
        bindTexture(gl, 3, shapeTexture);
        bindTexture(gl, 4, columnTexture);
      };
      const setMarchUniforms = (program) => {
        program.setSampler('State', 0).setSampler('Production', 1).setSampler('Occupancy', 2)
          .setSampler('Shapes', 3).setSampler('Columns', 4);
        program.set('MapA', kMapA);
        program.set('MapS', MAP_S);
        program.set('MapSize', kMapSize);
        program.set('OccThreshold', kOccThreshold);
        program.set('LnLow', Math.log(P.kLowKeV));
        program.set('LnHigh', Math.log(P.kHighKeV));
        program.set('FieldDown', ...view.fieldDown);
        program.set('MagEast', ...view.magEast);
        program.set('MagPole', ...view.magPole);
        program.set('A5577', P.kA5577);
        program.set('ARed', P.kA6300 + P.kA6364);
        program.set('AirglowKR', P.AirglowFromParam(eff('airglow')));
        program.set('AirglowKm', P.kAirglowKm);
        program.set('AirglowSigma', P.kAirglowSigma);
        program.setInt('MaxSteps', kMaxMarchSteps);
        program.set('ChannelMask', 1, 1, 1, 1);
        program.setInt('ClipSouthForTest', 0);

        // The ray spectrum: seeded wavelengths 0.5-5 km, amplitudes normalised
        // so the mean of s^2 is 1, phases drifting and riding the convection.
        const rayK = new Float32Array(kRayTerms);
        const rayAmp = new Float32Array(kRayTerms);
        const rayPhase = new Float32Array(kRayTerms);
        const seed = (Math.imul(P.SeedFromParam(raw('seed')), 7919) + 17) >>> 0;
        const drift = P.DriftFromParam(eff('drift'));
        let norm = 0;
        for (let k = 0; k < kRayTerms; k += 1) {
          const h = (salt) => P.PcgHash((seed + Math.imul(k, 131) + salt) >>> 0) / 4294967296.0;
          const lambda = kRayShortest * Math.pow(10.0, (k + h(1)) / kRayTerms);
          const wave = (2.0 * PI) / lambda;
          const omega = 2.0 * PI * (0.01 + 0.05 * h(2)) * (h(3) < 0.5 ? -1.0 : 1.0);
          let phase = 2.0 * PI * h(4) + omega * skyTime - wave * ((drift * skyTime) % lambda);
          phase -= 2.0 * PI * Math.floor(phase / (2.0 * PI));
          rayK[k] = wave;
          rayAmp[k] = Math.sqrt(lambda) * (0.5 + h(5));
          rayPhase[k] = phase;
          norm += rayAmp[k] * rayAmp[k];
        }
        for (let k = 0; k < kRayTerms; k += 1) rayAmp[k] *= Math.sqrt(2.0 / norm);
        program.setArray('RayK', rayK).setArray('RayAmp', rayAmp).setArray('RayPhase', rayPhase);
        program.set('Rays', P.RaysFromParam(eff('rays')));

        program.setArray('CompXYZ', components.xyz, 3);
        program.setArray('CompScotopic', components.scot);
        program.setArray('CompNm', components.nm);
        const channels = program.location('CompChannel[0]') ?? program.location('CompChannel');
        if (channels !== null) gl.uniform1iv(channels, components.channel);
        program.set('Extinction', P.ExtinctionFromParam(eff('extinction')));
        program.set('DisplayGain', Math.pow(2, P.ExposureFromParam(eff('exposure'))) * kDisplayScale);
        program.setInt('Observer', optionIndex(eff('observer'), 2));
      };
      const setCamera = (program) => {
        program.setInt('CameraKind', view.cameraKind);
        program.set('Forward', ...view.forward);
        program.set('Right', ...view.right);
        program.set('Up', ...view.up);
        program.set('TanHalf', view.tanHalf);
        program.set('LookAz', view.lookAz);
      };

      //------------------------------------------------------------------
      // 4. March.
      //------------------------------------------------------------------
      march.bind();
      marchProgram.use();
      bindMarchInputs();
      setMarchUniforms(marchProgram);
      setCamera(marchProgram);
      marchProgram.set('Raster', mw, mh);
      quad.draw();
      unbind(5);

      //------------------------------------------------------------------
      // 5. The whole sky, for the light it throws on the scene.
      //------------------------------------------------------------------
      const illumination = isEffect ? P.IlluminationFromParam(params.get('illumination')) : 0.0;
      const horizon = optionIndex(eff('horizon'), 3);
      const wantSky = isEffect ? illumination > 0.0 : horizon !== 0;
      if (wantSky) {
        allSky.bind();
        allSkyProgram.use();
        bindMarchInputs();
        setMarchUniforms(allSkyProgram);
        quad.draw();
        unbind(5);
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        allSky.generateMipmap();
      }

      //------------------------------------------------------------------
      // 6. Composite, into the page's framebuffer.
      //------------------------------------------------------------------
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      gl.viewport(0, 0, width, height);
      compositeProgram.use();
      bindMarchInputs();
      // The source has no clip; like the plugin, it binds a real texture there.
      bindTexture(gl, 5, isEffect ? input.texture : march.texture);
      bindTexture(gl, 6, march.texture);
      bindTexture(gl, 7, allSky.texture);
      setMarchUniforms(compositeProgram);
      setCamera(compositeProgram);
      compositeProgram.setSampler('InputTexture', 5).setSampler('March', 6).setSampler('AllSky', 7);
      compositeProgram.set('MaxUV', 1, 1);
      compositeProgram.setInt('IsEffect', isEffect ? 1 : 0);
      compositeProgram.set('Raster', width, height);
      compositeProgram.set('MarchSize', mw, mh);
      compositeProgram.setInt('OutputXYZ', 0);
      compositeProgram.set('SkyLight', wantSky ? 1.0 : 0.0);
      compositeProgram.set('Illumination', illumination);
      compositeProgram.setInt('MaskMode', isEffect ? optionIndex(params.get('skyMask'), 3) : 0);
      compositeProgram.set('MaskThreshold', P.MaskThresholdFromParam(params.get('maskThreshold')));
      compositeProgram.set('MixAmount', isEffect ? clamp(params.get('mix'), 0, 1) : 1.0);
      compositeProgram.setInt('HorizonKind', horizon);
      compositeProgram.set('SnowAlbedo', kSnowAlbedo);
      compositeProgram.set('StarGain', P.StarsFromParam(eff('stars')));
      const seed = P.SeedFromParam(raw('seed'));
      compositeProgram.setUint('HorizonSeed', P.PcgHash((Math.imul(seed, 3) + 1) >>> 0));
      compositeProgram.setUint('StarSeed', P.PcgHash((Math.imul(seed, 3) + 2) >>> 0));

      // Local (east, north, up) -> celestial: the pole at the observer's
      // latitude, turned by the sidereal angle.
      const lat = ((australis ? -1 : 1) * kObserverLatDeg * PI) / 180;
      const cp = [0, Math.cos(lat), Math.sin(lat)];
      const ex = [1, 0, 0];
      const ey = [cp[1] * ex[2] - cp[2] * ex[1], cp[2] * ex[0] - cp[0] * ex[2], cp[0] * ex[1] - cp[1] * ex[0]];
      let angle = eff('starMotion') > 0.5 ? (2.0 * PI * skyTime) / kSiderealDay : 0.0;
      angle -= 2.0 * PI * Math.floor(angle / (2.0 * PI));
      const c = Math.cos(angle), s = Math.sin(angle);
      const m = new Float32Array(9);
      for (let col = 0; col < 3; col += 1) {
        m[col * 3] = c * ex[col] - s * ey[col];
        m[col * 3 + 1] = s * ex[col] + c * ey[col];
        m[col * 3 + 2] = cp[col];
      }
      const toSky = compositeProgram.location('LocalToSky');
      if (toSky !== null) gl.uniformMatrix3fv(toSky, false, m);
      compositeProgram.set('PixelAngle', view.cameraKind === 0 ? (2.0 * view.tanHalf) / height : PI / Math.min(width, height));
      quad.draw();
      unbind(8);

    },
  };
}

//---------------------------------------------------------------------------
// The parameters: the constructor's, in its order and groups, with its names.
//---------------------------------------------------------------------------
const std = (id, name, group, display, hint, def = ROW_ONE[id]) => ({ id, name, type: 'standard', default: def, group, display, hint });
const opt = (id, name, elements, group, hint, def = ROW_ONE[id]) => ({ id, name, type: 'option', elements, default: def, group, hint });
const fixed = (n, digits = 0) => n.toFixed(digits);
const signed = (n, digits = 0) => `${n >= 0 ? '+' : '−'}${Math.abs(n).toFixed(digits)}`;

const PARAMS = [
  opt('hemisphere', 'Hemisphere', ['Borealis', 'Australis'], 'Sky',
    'Australis turns the poleward direction round while magnetic east stays east, so the same dynamics appear mirrored, the curls turn the other way and the stars turn about the south pole.'),
  std('ovalDistance', 'Oval Distance', 'Sky', (v) => `${fixed(P.OvalDistanceFromParam(v))} km`,
    'Km poleward of you to the equatorward arc: −600 puts the oval overhead and behind you, +1400 a band low on the horizon.'),
  std('dip', 'Dip', 'Sky', (v) => `${fixed(P.DipFromParam(v), 1)}°`,
    'Magnetic inclination. The curtains are field-aligned, so their rays converge on the magnetic zenith, 90° − dip equatorward of the zenith.'),
  std('declination', 'Declination', 'Sky', (v) => `${signed(P.DeclinationFromParam(v), 1)}°`,
    'Magnetic declination, east of geographic north: turns the whole oval.'),
  std('speed', 'Speed', 'Sky', (v) => (P.SpeedFromParam(v) === 0 ? 'frozen' : `${P.SpeedFromParam(v).toFixed(2)}× sky time`),
    'Sky seconds per second. 1× is real time — honest and slow; the default is 4×. The very bottom freezes the sky.'),
  { id: 'seed', name: 'Seed', type: 'option', elements: Array.from({ length: SEED_CHOICES }, (_, i) => String(i)), default: 1, group: 'Sky',
    hint: 'Which sky, not what kind: the seeded perturbation, the forcing, the rays, the stars and the hills. FF_TYPE_INTEGER 0–9999 in the plugin; 0–99 here.' },

  { id: 'arcs', name: 'Arcs', type: 'option', elements: ['1', '2', '3', '4', '5'], default: ROW_ONE.arcs - 1, group: 'Arcs',
    hint: 'Parallel arcs, Arc Spacing apart, interacting through the same Birkhoff–Rott sum. FF_TYPE_INTEGER 1–5 in the plugin. Changing it rebuilds the sheet.' },
  std('arcSpacing', 'Arc Spacing', 'Arcs', (v) => `${fixed(P.ArcSpacingFromParam(v))} km`,
    'Km between neighbouring arcs. Changing it rebuilds the sheet.'),
  std('sheetStrength', 'Sheet Strength', 'Arcs', (v) => `${P.SheetStrengthFromParam(v).toFixed(2)} km/s`,
    'The shear across the sheet — circulation per km. The Kelvin–Helmholtz growth rate goes as it, so a strong sheet curls fast.'),
  std('curlSize', 'Curl Size', 'Arcs', (v) => `δ ${P.CurlSizeFromParam(v).toFixed(1)} km`,
    'δ, the sheet’s regularisation length. The fastest-growing wavelength is ~2πδ/1.26, so this is a physical length and not a noise frequency.'),
  std('disturbance', 'Disturbance', 'Arcs', (v) => `${P.DisturbanceFromParam(v).toFixed(1)} km`,
    'Km of seeded perturbation near the fastest-growing wavelength, and of the forcing added every 12 s of sky time. Zero is a straight arc that stays straight.'),
  std('drift', 'Drift', 'Arcs', (v) => `${signed(P.DriftFromParam(v), 2)} km/s`,
    'Uniform eastward convection. It changes none of the sheet’s invariants; it carries the rays with it.'),
  { id: 'substorm', name: 'Substorm', type: 'boolean', default: 0, group: 'Arcs',
    hint: 'The sheet strength ×2.5 (decaying over 240 s), the poleward arc ×4 brighter and surging west at 1.5 km/s, and a 400 km kink the same instability rolls up. An FF_TYPE_EVENT in the plugin; here a toggle the page releases after one frame.' },
  { id: 'calm', name: 'Calm', type: 'boolean', default: 0, group: 'Arcs',
    hint: 'Rebuild straight arcs with the seeded perturbation. An FF_TYPE_EVENT in the plugin; here a toggle the page releases after one frame.' },

  std('energy', 'Energy', 'Precipitation', (v) => `${P.EnergyFromParam(v).toFixed(2)} keV`,
    'The electrons’ characteristic energy at the base sheet strength. Soft precipitation stops high, where O is and the red line lives; hard reaches below 100 km, where N₂ gives the pink lower border.'),
  std('flux', 'Flux', 'Precipitation', (v) => (P.FluxFromParam(v) === 0 ? 'off' : `${P.FluxFromParam(v).toFixed(2)} erg cm⁻² s⁻¹`),
    'Energy flux at the base sheet strength. Applied by the renderer, so Flux 0 is dark on the very next frame.'),
  std('knight', 'Knight', 'Precipitation', (v) => P.KnightFromParam(v).toFixed(2),
    'The Knight relation’s exponent weight: E0 goes as the local sheet strength to this power and the energy flux as twice it. 1 is the relation, 0 none — a uniform curtain.'),
  std('thickness', 'Thickness', 'Precipitation', (v) => `σ ${P.ThicknessFromParam(v).toFixed(2)} km`,
    'The curtain’s 1σ half-thickness across the arc. (The spec’s “Curtain Thickness” is 17 characters; FFGL shows 16.)'),
  std('rays', 'Rays', 'Precipitation', (v) => P.RaysFromParam(v).toFixed(2),
    'The one stochastic texture: eight seeded filaments of 0.5–5 km riding the sheet. It moves light and makes none — the mean is 1 at any setting — and the minutes-long red line smears it out.'),

  std('activity', 'Activity', 'Atmosphere', (v) => (P.ActivityFromParam(v) < 0.02 ? 'solar minimum' : P.ActivityFromParam(v) > 0.98 ? 'solar maximum' : P.ActivityFromParam(v).toFixed(2)),
    'Interpolates the NRLMSIS atmosphere between a quiet sun and solar maximum. The thermosphere heats and swells and the red line climbs. Rebuilds the emission tables (quantised to 1/64).'),
  std('wind', 'Neutral Wind', 'Atmosphere', (v) => `${signed(P.WindFromParam(v) * 1000)} m/s`,
    'Eastward wind at the red line’s height. It advects the O(¹D) population, which lives for a minute or more, so the red drifts off the curtain that made it.'),
  std('airglow', 'Airglow', 'Atmosphere', (v) => `${fixed(P.AirglowFromParam(v) * 1000)} R`,
    'The 557.7 nm night airglow layer at 97 km. Its brightening toward the horizon is the path integral’s own Jacobian, not a formula written in.'),
  std('extinction', 'Extinction', 'Atmosphere', (v) => `${P.ExtinctionFromParam(v).toFixed(2)}×`,
    'Rayleigh + aerosol optical depth along the Kasten & Young airmass; 1 is a clean sea-level site. Blue suffers most, low down.'),

  opt('camera', 'Camera', ['Rectilinear', 'Fisheye'], 'Camera',
    'Fisheye is an equidistant all-sky camera: zenith in the middle, the horizon on the circle, east on the left (looking up).'),
  std('lookAzimuth', 'Look Azimuth', 'Camera', (v) => `${signed(P.LookAzimuthFromParam(v))}°`, '0 is geographic north.'),
  std('lookElevation', 'Look Elevation', 'Camera', (v) => `${fixed(P.LookElevationFromParam(v))}°`),
  std('fov', 'Field of View', 'Camera', (v) => `${fixed(P.FovFromParam(v))}°`, 'The frame’s height, rectilinear.'),
  std('roll', 'Roll', 'Camera', (v) => `${signed(P.RollFromParam(v))}°`),
  std('exposure', 'Exposure', 'Camera', (v) => `${signed(P.ExposureFromParam(v), 1)} stops`),
  opt('observer', 'Observer', ['Camera', 'Eye'], 'Camera',
    'Eye applies CIE 191:2010 mesopic photometry: a faint display goes grey-green, and the red line — V′(630 nm) is 0.0033 — nearly vanishes.'),
  std('stars', 'Stars', 'Camera', (v) => `${P.StarsFromParam(v).toFixed(2)}×`),
  { id: 'starMotion', name: 'Star Motion', type: 'boolean', default: ROW_ONE.starMotion, group: 'Camera',
    hint: 'Turn the sky at the sidereal rate (sky time) about the pole at ±69.65°.' },
  opt('horizon', 'Horizon', ['None', 'Flat', 'Hills'], 'Camera',
    'None leaves below the horizon transparent, for compositing. The ground is snow lit by the whole sky, so it takes the display’s colour.'),
  opt('detail', 'Detail', ['Quarter', 'Half', 'Three Quarters', 'Full'], 'Camera',
    'The march’s raster as a fraction of the output, upsampled by a joint bilateral filter keyed on the footprint map.', 1),

  opt('preset', 'Preset', ['Custom', ...PRESET_ROWS.map(([name]) => name)], 'Preset',
    'An OVERRIDE, not a write. On anything but Custom the row is laid over the sky, arcs, precipitation, atmosphere and camera every frame at read time, and those sliders are not the truth. That is the plugin’s behaviour — Resolume does not consume value events. Put it back on Custom to get the sliders back.', 0),

  opt('skyMask', 'Sky Mask', ['Everything', 'Alpha', 'Dark Areas'], 'Over',
    'Everything adds the light over the whole clip. Alpha: the clip’s transparent sky becomes the night sky and its opaque pixels are exactly the clip. Dark Areas: only where luma is under Mask Threshold.', 0),
  std('maskThreshold', 'Mask Threshold', 'Over', (v) => P.MaskThresholdFromParam(v).toFixed(2), 'Luma below which Dark Areas counts as sky.', 0.25),
  std('illumination', 'Illumination', 'Over', (v) => `k ${P.IlluminationFromParam(v).toFixed(1)}`,
    'Lights the clip by 1 + k E, E the whole sky’s illuminance on the ground, in its own colour: snow goes green under a strong display.', 0.1),
  std('mix', 'Mix', 'Over', (v) => v.toFixed(2), undefined, 1.0),
];

const demo = mountDemo({
  name: 'Boreal',
  pluginId: 'BR01 · BR02',
  tagline:
    'The aurora borealis and australis. The arc is a sheet of charge drifting at E×B — a vortex sheet — that rolls itself into curls, folds and surges by the Kelvin–Helmholtz instability; the electrons it accelerates light the green, red, blue and pink lines at the heights an NRLMSIS atmosphere puts them; and a camera on a spherical Earth looks up through it all. The six render passes here are the plugin’s own GLSL; the vortex sheet and the emission tables are a JavaScript port of its C++, with the sheet capped at 1024 nodes rather than 4096. Nothing audio is on this page.',
  repo: 'https://github.com/stoatworks-labs/boreal',

  blurb:
    'It is Boreal’s own six GLSL passes, ported from the repository to WebGL2 and driven by a JavaScript port of its C++ vortex sheet and emission tables — a port only a reader checks, with the sheet capped at 1024 nodes where the plugin allows 4096, so the finest curls are coarser. SW Boreal is a source and reads no video; SW Boreal Over adds the same sky to a generated clip as light.',

  // The source leaves below the horizon transparent (Horizon None) and the Over
  // effect keeps the clip's alpha, so what sits behind is a real question.
  showBackdrop: true,

  // The footprint maps are RG32F/RGBA32F/R32F render targets and the splat
  // accumulates into RG32F by additive blending: both extensions, or nothing.
  needFloat: true,
  needFloatBlend: true,

  variants: {
    label: 'Plugin',
    default: 'source',
    options: [
      { id: 'source', name: 'SW Boreal (source)', hint: 'BR01, FF_SOURCE: the night sky, no input.' },
      { id: 'over', name: 'SW Boreal Over (effect)', hint: 'BR02, FF_EFFECT: the aurora added to the clip as light.' },
    ],
  },

  // For the Over effect only. Shape on transparency has a real alpha for Sky
  // Mask: Alpha; Lights on black has the dark areas Dark Areas looks for.
  sources: ['scene', 'alpha', 'spot', 'grid', 'bars'],

  params: PARAMS,

  differences: [
    'The CPU half is a PORT, not the plugin’s code. Boreal’s dynamics are a periodic Birkhoff–Rott vortex sheet integrated by RK4 in C++ (engine/Sheet.cpp, engine/Engine.cpp), and its emission tables come from Fang 2008 deposition over a baked NRLMSIS atmosphere (physics/Emission.cpp, Atmosphere.cpp, Optics.cpp). All of that is translated to JavaScript in port.js, function for function, because without it there is no sky to draw. Nothing checks the port but a reader: the repository’s brtest --kh, --invariants, --knight, --deposition and --lifetime check the C++ and have never heard of this page. When it was written it was compared once against the C++ and agreed — same node count and refusals, node positions to about six figures after 600 frames and a substorm, the emission tables to every printed digit — but that comparison does not run again.',
    'The sheet is capped at 1024 nodes, not the plugin’s 4096. The pair sum is O(N²): the plugin runs it on up to four C++ threads at ~14 ms a step at its cap, and this page runs it on one JavaScript thread, where 1024 nodes is about 10 ms. The plugin’s own at-cap behaviour applies at 1024 — insertion refused and counted, the curls drawn coarser — and each arc starts at 512 nodes 8 km apart rather than 1024 nodes 4 km apart. So the large folds and surges are the plugin’s, and the finest curls are coarser. The line under the canvas reports the node count and the refusals.',
    'The GPU half is not a port. The splat, update, occupancy, march, all-sky and composite passes are the plugin’s own GLSL, at the plugin’s own map sizes (1024² 32-bit float footprint maps, a 32² all-sky), and demo/tools/check_shaders.py fails the repository’s verify script if a character of them drifts — or a number of the NRLMSIS table or of the seven preset rows.',
    'Nothing audio. The plugin declares an Audio FFT buffer that Resolume fills, with Audio Substorm (each onset is a substorm) and Audio Flux (the level brightens the precipitation) over it. There is no Resolume FFT in a browser, so all three are absent from this panel rather than present and dead. Substorm is here as the button the plugin also declares.',
    'Substorm and Calm are FF_TYPE_EVENT in the plugin. The kit has no event control, so they are toggles the page releases after one frame — one press, one rising edge, which is what the plugin counts. Arcs and Seed are FF_TYPE_INTEGER; they are dropdowns here, and Seed offers 0–99 of the plugin’s 0–9999. The About block is absent.',
    'The engine runs on the page’s main thread, one frame behind, exactly as the plugin’s worker does — the frame draws the previous job’s sheet — so the sequence of skies is the plugin’s sequence without the thread. A slow machine drops frames rather than sky time; past six steps a job the step grows, and past 2 s a step the sheet falls behind the clock, as in the plugin. Restart is a clock jump: no sky time passes across it and the sky carries on. Calm starts it again.',
    'Nothing here is measured. The plugin’s numerical proof — the Kelvin–Helmholtz growth rate against the derived σ(k, δ), circulation, impulse and Hamiltonian over ten minutes, Fang 2008 against Fang 2010, the lifetimes against the exact integrator, the colour, the corona’s vanishing point and van Rhijn — is brtest in the repository, and that harness, not this page, is the reason to believe the sky. The plugin itself has never been loaded into Resolume; that is not something a browser can speak to either.',
  ],

  createRenderer,
});

//---------------------------------------------------------------------------
// Which controls belong to which plugin.
//
// The Over group exists only in SW Boreal Over's constructor, and only the
// effect reads a clip. For the source both are hidden rather than left present
// and inert.
//---------------------------------------------------------------------------
//
// By inline style, not the `hidden` attribute: kit.css gives these elements a
// `display` of their own, which beats the attribute's user-agent rule, so
// `hidden = true` leaves them on screen.
function showForVariant(variant) {
  const effect = variant === 'over';
  const show = (node, on) => { if (node) node.style.display = on ? '' : 'none'; };
  for (const section of document.querySelectorAll('.pgroup')) {
    if (section.querySelector('.pgroup__name')?.textContent === 'Over') show(section, effect);
  }
  for (const field of document.querySelectorAll('.transport__field')) {
    if (field.querySelector('.transport__label')?.textContent === 'Clip') show(field, effect);
  }
  show(document.querySelector('.transport__file'), effect);
}

if (demo && !new URLSearchParams(window.location.search).has('embed')) {
  showForVariant(demo.state.variant);
  document.addEventListener('demo:state', () => showForVariant(demo.state.variant));

  //-------------------------------------------------------------------------
  // The stats line. Reports; measures nothing. Without it a coarser curl reads
  // as the plugin being coarse, rather than the page being capped.
  //-------------------------------------------------------------------------
  const stage = document.querySelector('.stage');
  if (stage) {
    const line = document.createElement('p');
    line.className = 'stage__status';
    line.id = 'boreal-stats';
    stage.append(line);
    setInterval(() => {
      const t = telemetry;
      const minutes = Math.floor(t.skyTime / 60);
      const seconds = (t.skyTime - minutes * 60).toFixed(0).padStart(2, '0');
      line.textContent =
        `Sky time ${minutes}:${seconds} at ${t.speed === 0 ? 'frozen' : `${t.speed.toFixed(2)}×`}. `
        + `The sheet: ${t.nodes.toLocaleString('en-GB')} of the page’s ${BROWSER_CAP.toLocaleString('en-GB')}-node cap `
        + `(the plugin’s is ${P.kPluginCap.toLocaleString('en-GB')}), ${t.refused.toLocaleString('en-GB')} insertions refused at the cap so far; `
        + `the last job that stepped took ${t.steps} RK4 step${t.steps === 1 ? '' : 's'} in ${t.cpuMs.toFixed(1)} ms on this page’s one thread. `
        + `Substorms: ${t.substorms}.`;
    }, 250);
  }
}
