#include "Boreal.h"

#include "Diag.h"
#include "GLState.h"
#include "Shaders.h"
#include "physics/Optics.h"

//FFGLSDK.h includes every other scoped binding and omits this one (SDK
//b1afaf9), so it has to be asked for by name.
#include <ffglex/FFGLScopedFBOBinding.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>

using namespace ffglex;

namespace boreal
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

double wallSeconds()
{
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}

int optionIndex( float value, int count )
{
	return std::clamp( static_cast< int >( std::lround( value ) ), 0, count - 1 );
}

constexpr int kClockVotes        = 4;
constexpr double kMaxFrameDelta  = 0.25;//host seconds; a bigger step is a jump
constexpr int kAllSkySize        = 32;
constexpr float kSnowAlbedo      = 0.8f;
constexpr double kObserverLatDeg = 69.65;///< Tromso, where the atmosphere table was taken
constexpr double kSiderealDay    = 86164.0905;
constexpr float kDisplayScale    = 250.0f;///< display units per cd m^-2 at 0 stops
constexpr float kOccThreshold    = 1e-5f; ///< kR: 0.01 R, far under anything visible
constexpr int kMaxMarchSteps     = 400;
constexpr double kRayShortest    = 0.5;///< km
/// O diffusion near the red line's height, km^2/s: the order of the molecular
/// diffusion coefficient of O at ~250 km (ASSUMED, not from a table).
constexpr float kRedDiffusion = 0.5f;

const char* const kHemisphereNames[] = { "Borealis", "Australis" };
const char* const kCameraNames[]     = { "Rectilinear", "Fisheye" };
const char* const kObserverNames[]   = { "Camera", "Eye" };
const char* const kHorizonNames[]    = { "None", "Flat", "Hills" };
const char* const kDetailNames[]     = { "Quarter", "Half", "Three Quarters", "Full" };
const char* const kMaskNames[]       = { "Everything", "Alpha", "Dark Areas" };

/// The ParamId each presets::Param drives, in presets::Param order.
constexpr unsigned int kPresetTarget[ presets::kParamCount ] = {
	PT_HEMISPHERE, PT_OVAL_DISTANCE, PT_DIP,       PT_DECLINATION,   PT_SPEED,       PT_ARCS,
	PT_ARC_SPACING, PT_SHEET_STRENGTH, PT_CURL_SIZE, PT_DISTURBANCE, PT_DRIFT,       PT_ENERGY,
	PT_FLUX,       PT_KNIGHT,        PT_THICKNESS, PT_RAYS,          PT_ACTIVITY,    PT_WIND,
	PT_AIRGLOW,    PT_EXTINCTION,    PT_CAMERA,    PT_LOOK_AZIMUTH,  PT_LOOK_ELEVATION, PT_FOV,
	PT_ROLL,       PT_EXPOSURE,      PT_OBSERVER,  PT_STARS,         PT_STAR_MOTION, PT_HORIZON,
};
} // namespace

static_assert( PT_SOURCE_COUNT - PT_ABOUT_TEXT == stoatworks::about::kParamCount,
               "the About run no longer matches StoatworksAbout.h -- add or remove a PT_ABOUT_BUTTON_n to match" );

//---------------------------------------------------------------------------
BorealPlugin::BorealPlugin( bool effect ) : isEffect( effect )
{
	SetMinInputs( isEffect ? 1 : 0 );
	SetMaxInputs( isEffect ? 1 : 0 );
	SetTimeSupported( true );

	//-------------------------------------------------------------------
	// Defaults: preset row 1, which `brtest --defaults` holds to it.
	//-------------------------------------------------------------------
	const presets::Preset& row = presets::kPresets[ 0 ];
	for( int c = 0; c < presets::kParamCount; ++c )
		params[ kPresetTarget[ c ] ] = row.v[ c ];
	params[ PT_SEED ]           = 1.0f;
	params[ PT_SUBSTORM ]       = 0.0f;
	params[ PT_CALM ]           = 0.0f;
	params[ PT_DETAIL ]         = 1.0f;//half
	params[ PT_AUDIO_SUBSTORM ] = 0.0f;
	params[ PT_AUDIO_FLUX ]     = 0.0f;
	params[ PT_PRESET ]         = 0.0f;
	params[ PT_SKY_MASK ]       = static_cast< float >( SkyMask::Everything );
	params[ PT_MASK_THRESHOLD ] = 0.25f;
	params[ PT_ILLUMINATION ]   = 0.1f;
	params[ PT_MIX ]            = 1.0f;

	auto standard = [ this ]( unsigned int id, const char* name ) { SetParamInfo( id, name, FF_TYPE_STANDARD, params[ id ] ); };
	auto option   = [ this ]( unsigned int id, const char* name, const char* const* names, int count ) {
		SetOptionParamInfo( id, name, count, params[ id ] );
		for( int i = 0; i < count; ++i )
			SetParamElementInfo( id, i, names[ i ], static_cast< float >( i ) );
	};
	auto integer = [ this ]( unsigned int id, const char* name, float lo, float hi ) {
		//Only FF_TYPE_STANDARD has its default clamped into 0..1, so an integer
		//is declared with its real default and range.
		SetParamInfo( id, name, FF_TYPE_INTEGER, params[ id ] );
		SetParamRange( id, lo, hi );
	};

	option( PT_HEMISPHERE, "Hemisphere", kHemisphereNames, 2 );
	standard( PT_OVAL_DISTANCE, "Oval Distance" );
	standard( PT_DIP, "Dip" );
	standard( PT_DECLINATION, "Declination" );
	standard( PT_SPEED, "Speed" );
	integer( PT_SEED, "Seed", 0.0f, 9999.0f );

	integer( PT_ARCS, "Arcs", 1.0f, 5.0f );
	standard( PT_ARC_SPACING, "Arc Spacing" );
	standard( PT_SHEET_STRENGTH, "Sheet Strength" );
	standard( PT_CURL_SIZE, "Curl Size" );
	standard( PT_DISTURBANCE, "Disturbance" );
	standard( PT_DRIFT, "Drift" );
	SetParamInfo( PT_SUBSTORM, "Substorm", FF_TYPE_EVENT, false );
	SetParamInfo( PT_CALM, "Calm", FF_TYPE_EVENT, false );

	standard( PT_ENERGY, "Energy" );
	standard( PT_FLUX, "Flux" );
	standard( PT_KNIGHT, "Knight" );
	standard( PT_THICKNESS, "Thickness" );//"Curtain Thickness" is 17: past FFGL's 16
	standard( PT_RAYS, "Rays" );

	standard( PT_ACTIVITY, "Activity" );
	standard( PT_WIND, "Neutral Wind" );
	standard( PT_AIRGLOW, "Airglow" );
	standard( PT_EXTINCTION, "Extinction" );

	option( PT_CAMERA, "Camera", kCameraNames, 2 );
	standard( PT_LOOK_AZIMUTH, "Look Azimuth" );
	standard( PT_LOOK_ELEVATION, "Look Elevation" );
	standard( PT_FOV, "Field of View" );
	standard( PT_ROLL, "Roll" );
	standard( PT_EXPOSURE, "Exposure" );
	option( PT_OBSERVER, "Observer", kObserverNames, 2 );
	standard( PT_STARS, "Stars" );
	SetParamInfo( PT_STAR_MOTION, "Star Motion", FF_TYPE_BOOLEAN, params[ PT_STAR_MOTION ] > 0.5f );
	option( PT_HORIZON, "Horizon", kHorizonNames, 3 );
	option( PT_DETAIL, "Detail", kDetailNames, kDetailCount );

	//An FFT buffer: Resolume shows it as an audio-source picker.
	SetBufferParamInfo( PT_AUDIO, "Audio", audio::kBins, FF_USAGE_FFT );
	for( int i = 0; i < audio::kBins; ++i )
		SetParamElementInfo( PT_AUDIO, i, "", 0.0f );
	standard( PT_AUDIO_SUBSTORM, "Audio Substorm" );
	standard( PT_AUDIO_FLUX, "Audio Flux" );

	SetOptionParamInfo( PT_PRESET, "Preset", 1 + presets::kCount, 0.0f );
	SetParamElementInfo( PT_PRESET, 0, "Custom", 0.0f );
	for( int i = 0; i < presets::kCount; ++i )
		SetParamElementInfo( PT_PRESET, 1 + i, presets::kPresets[ i ].name, static_cast< float >( 1 + i ) );

	for( unsigned int id = PT_HEMISPHERE; id <= PT_SEED; ++id )
		SetParamGroup( id, "Sky" );
	for( unsigned int id = PT_ARCS; id <= PT_CALM; ++id )
		SetParamGroup( id, "Arcs" );
	for( unsigned int id = PT_ENERGY; id <= PT_RAYS; ++id )
		SetParamGroup( id, "Precipitation" );
	for( unsigned int id = PT_ACTIVITY; id <= PT_EXTINCTION; ++id )
		SetParamGroup( id, "Atmosphere" );
	for( unsigned int id = PT_CAMERA; id <= PT_DETAIL; ++id )
		SetParamGroup( id, "Camera" );
	for( unsigned int id = PT_AUDIO; id <= PT_AUDIO_FLUX; ++id )
		SetParamGroup( id, "Audio" );
	SetParamGroup( PT_PRESET, "Preset" );

	SetParamInfo( PT_ABOUT_TEXT, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = PT_ABOUT_TEXT + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( unsigned int id = PT_ABOUT_TEXT; id < PT_SOURCE_COUNT; ++id )
		SetParamGroup( id, "About" );

	if( isEffect )
	{
		option( PT_SKY_MASK, "Sky Mask", kMaskNames, 3 );
		standard( PT_MASK_THRESHOLD, "Mask Threshold" );
		standard( PT_ILLUMINATION, "Illumination" );
		standard( PT_MIX, "Mix" );
		for( unsigned int id = PT_SKY_MASK; id < PT_COUNT; ++id )
			SetParamGroup( id, "Over" );
	}
}

BorealPlugin::~BorealPlugin() = default;

//---------------------------------------------------------------------------
FFResult BorealPlugin::InitGL( const FFGLViewportStruct* vp )
{
	diag::init();
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer="
	            + glStringOrUnknown( GL_RENDERER ) + " version=" + glStringOrUnknown( GL_VERSION ) );

	using namespace shaders;
	const std::string quadVertex = std::string( kVersion ) + kQuadVertex;
	struct Stage
	{
		FFGLShader* shader;
		std::string vertex, fragment;
		const char* name;
	};
	const Stage stages[] = {
		{ &splatShader, Assemble( kSplatVertex ), Assemble( kSplatFragment ), "splat" },
		{ &updateShader, quadVertex, Assemble( kUpdateFragment ), "update" },
		{ &occupancyShader, quadVertex, Assemble( kOccupancyFragment ), "occupancy" },
		{ &marchShader, quadVertex, Assemble( kMarchLibrary, kMarchFragment ), "march" },
		{ &allSkyShader, quadVertex, Assemble( kMarchLibrary, kAllSkyFragment ), "allsky" },
		{ &compositeShader, quadVertex, Assemble( kMarchLibrary, kCompositeFragment ), "composite" },
	};
	for( const Stage& stage : stages )
	{
		if( stage.shader->Compile( stage.vertex.c_str(), stage.fragment.c_str() ) )
			continue;
		diag::error( std::string( "the " ) + stage.name + " shader failed to compile - the plugin will do nothing" );
		FFGLLog::LogToHost( "Boreal: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}
	if( !quad.Initialise() )
	{
		diag::error( "quad geometry failed to initialise" );
		DeInitGL();
		return FF_FAIL;
	}

	glGenVertexArrays( 1, &splatVAO );
	glGenBuffers( 1, &splatVBO );

	diag::info( isEffect ? "initialised (Over)" : "initialised (source)" );
	return CFFGLPlugin::InitGL( vp );
}

//---------------------------------------------------------------------------
bool BorealPlugin::ensureBuffers( int width, int height )
{
	const int detail = optionIndex( Effective( PT_DETAIL ), kDetailCount );
	const int mw     = std::max( 8, static_cast< int >( std::lround( width * kDetailFractions[ detail ] ) ) );
	const int mh     = std::max( 8, static_cast< int >( std::lround( height * kDetailFractions[ detail ] ) ) );

	//All 32-bit float. The populations are fed back every frame through a
	//decay factor within 1e-4 of 1, which a half float (and this GPU truncates
	//to half) cannot even represent the difference from (millpond's trap).
	bool ok = production.Ensure( kMapSize, kMapSize, GL_RG32F, PassBuffer::Sampling::Linear );
	for( PassBuffer& buffer : state )
		ok = ok && buffer.Ensure( kMapSize, kMapSize, GL_RGBA32F, PassBuffer::Sampling::Linear );
	ok = ok && occupancy.Ensure( kMapSize, kMapSize, GL_R32F, PassBuffer::Sampling::Mipmapped );
	ok = ok && march.Ensure( mw, mh, GL_RGBA32F, PassBuffer::Sampling::Nearest );
	ok = ok && allSky.Ensure( kAllSkySize, kAllSkySize, GL_RGBA32F, PassBuffer::Sampling::Mipmapped );
	marchWidth  = mw;
	marchHeight = mh;
	return ok;
}

void BorealPlugin::ensureTables( double activity )
{
	//Quantised, so dragging Activity rebuilds a few dozen times, not every frame.
	const double wanted = std::round( std::clamp( activity, 0.0, 1.0 ) * 64.0 ) / 64.0;
	if( wanted == tablesActivity && shapeTexture != 0 )
		return;
	tables         = emission::BuildTables( wanted );
	tablesActivity = wanted;

	if( shapeTexture == 0 )
		glGenTextures( 1, &shapeTexture );
	if( columnTexture == 0 )
		glGenTextures( 1, &columnTexture );

	//Bound and unbound by hand, before any pass binds anything.
	glBindTexture( GL_TEXTURE_2D, shapeTexture );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, emission::kHeights, emission::kEnergies, 0, GL_RGBA, GL_FLOAT,
	              tables.shape.data() );
	for( GLuint texture : { shapeTexture, columnTexture } )
	{
		glBindTexture( GL_TEXTURE_2D, texture );
		if( texture == columnTexture )
		{
			std::vector< float > rows( tables.column );
			rows.insert( rows.end(), tables.prompt.begin(), tables.prompt.end() );
			glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, emission::kEnergies, 2, 0, GL_RGBA, GL_FLOAT, rows.data() );
		}
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	}
	glBindTexture( GL_TEXTURE_2D, 0 );
}

//---------------------------------------------------------------------------
void BorealPlugin::buildView( int width, int height )
{
	(void)width;
	(void)height;
	const double az   = LookAzimuthFromParam( Effective( PT_LOOK_AZIMUTH ) ) * kPi / 180.0;
	const double el   = LookElevationFromParam( Effective( PT_LOOK_ELEVATION ) ) * kPi / 180.0;
	const double roll = RollFromParam( Effective( PT_ROLL ) ) * kPi / 180.0;
	const double fov  = FovFromParam( Effective( PT_FOV ) ) * kPi / 180.0;

	const double f[ 3 ]  = { std::sin( az ) * std::cos( el ), std::cos( az ) * std::cos( el ), std::sin( el ) };
	const double r0[ 3 ] = { std::cos( az ), -std::sin( az ), 0.0 };
	const double u0[ 3 ] = { -std::sin( az ) * std::sin( el ), -std::cos( az ) * std::sin( el ), std::cos( el ) };
	for( int i = 0; i < 3; ++i )
	{
		view.forward[ i ] = static_cast< float >( f[ i ] );
		view.right[ i ]   = static_cast< float >( r0[ i ] * std::cos( roll ) + u0[ i ] * std::sin( roll ) );
		view.up[ i ]      = static_cast< float >( u0[ i ] * std::cos( roll ) - r0[ i ] * std::sin( roll ) );
	}
	view.cameraKind = optionIndex( Effective( PT_CAMERA ), static_cast< int >( CameraKind::Count ) );
	view.tanHalf    = static_cast< float >( std::tan( 0.5 * fov ) );
	view.lookAz     = static_cast< float >( az );

	//The field. Going DOWN a field line moves poleward in either hemisphere, so
	//a curtain's top leans equatorward and its rays converge on the magnetic
	//zenith, which is equatorward of the zenith by 90 - dip.
	const double dip  = DipFromParam( Effective( PT_DIP ) ) * kPi / 180.0;
	const double decl = DeclinationFromParam( Effective( PT_DECLINATION ) ) * kPi / 180.0;
	const double sign = optionIndex( Effective( PT_HEMISPHERE ), 2 ) == 1 ? -1.0 : 1.0;
	const double pole[ 2 ] = { sign * std::sin( decl ), sign * std::cos( decl ) };
	const double east[ 2 ] = { std::cos( decl ), -std::sin( decl ) };
	view.magPole[ 0 ]      = static_cast< float >( pole[ 0 ] );
	view.magPole[ 1 ]      = static_cast< float >( pole[ 1 ] );
	view.magEast[ 0 ]      = static_cast< float >( east[ 0 ] );
	view.magEast[ 1 ]      = static_cast< float >( east[ 1 ] );
	view.fieldDown[ 0 ]    = static_cast< float >( std::cos( dip ) * pole[ 0 ] );
	view.fieldDown[ 1 ]    = static_cast< float >( std::cos( dip ) * pole[ 1 ] );
	view.fieldDown[ 2 ]    = static_cast< float >( -std::sin( dip ) );
}

engine::Params BorealPlugin::engineParams() const
{
	engine::Params p;
	p.arcs        = std::clamp( static_cast< int >( std::lround( Effective( PT_ARCS ) ) ), 1, 5 );
	p.spacing     = ArcSpacingFromParam( Effective( PT_ARC_SPACING ) );
	p.gamma       = SheetStrengthFromParam( Effective( PT_SHEET_STRENGTH ) );
	p.delta       = CurlSizeFromParam( Effective( PT_CURL_SIZE ) );
	p.disturbance = DisturbanceFromParam( Effective( PT_DISTURBANCE ) );
	p.drift       = DriftFromParam( Effective( PT_DRIFT ) );
	p.knight      = KnightFromParam( Effective( PT_KNIGHT ) );
	p.seed        = SeedFromParam( params[ PT_SEED ] ) + 1u;
	p.threads     = std::clamp( static_cast< int >( std::thread::hardware_concurrency() ) / 2, 1, 4 );
	return p;
}

//---------------------------------------------------------------------------
void BorealPlugin::UpdateClock()
{
	const double wallNow = wallSeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;
	const double raw = hostTime;

	//Resolume has been seen sending seconds and milliseconds through SetTime:
	//vote on the unit against the wall clock (rosette's code, via millpond).
	if( clockScale == 0.0 && raw >= 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;
			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
			{
				clockScale  = millisVotes > secondsVotes ? 0.001 : 1.0;
				settledJump = true;
			}
		}
	}
	if( raw >= 0.0 )
		lastRawTime = raw;
	lastWallTime = wallNow;
	now          = ( raw >= 0.0 && clockScale != 0.0 ) ? raw * clockScale : wallNow - wallStart;
}

//---------------------------------------------------------------------------
void BorealPlugin::splat( const engine::Snapshot& snap )
{
	constexpr double kPeriod = 4096.0;
	const float oval         = OvalDistanceFromParam( Effective( PT_OVAL_DISTANCE ) );
	const float limit        = kMapU + 60.0f;

	std::vector< float > vertices;
	vertices.reserve( snap.nodes.size() * 6 * 11 );
	for( size_t a = 0; a + 1 < snap.arcStart.size(); ++a )
	{
		const int begin = snap.arcStart[ a ], end = snap.arcStart[ a + 1 ];
		const int n     = end - begin;
		//Each arc's rays are its own: its labels are offset so two arcs laid
		//down together do not carry the same filaments.
		const float labelOffset = 1777.7f * static_cast< float >( a );
		for( int i = 0; i < n; ++i )
		{
			const engine::Node& na = snap.nodes[ static_cast< size_t >( begin + i ) ];
			const engine::Node& nb = snap.nodes[ static_cast< size_t >( begin + ( i + 1 ) % n ) ];
			const float ax = na.x, ay = na.y + oval;
			float bx       = nb.x;
			bx -= static_cast< float >( kPeriod * std::round( ( bx - ax ) / kPeriod ) );
			const float by = nb.y + oval;
			if( ( std::fabs( ax ) > limit && std::fabs( bx ) > limit ) || ( std::fabs( ay ) > limit && std::fabs( by ) > limit ) )
				continue;
			float lb = nb.label;
			lb -= static_cast< float >( kPeriod * std::round( ( lb - na.label ) / kPeriod ) );
			for( int corner = 0; corner < 6; ++corner )
			{
				const float v[ 11 ] = { ax, ay, bx, by, na.flux, nb.flux, na.lnE0, nb.lnE0, na.label + labelOffset,
					                    lb + labelOffset, static_cast< float >( corner ) };
				vertices.insert( vertices.end(), v, v + 11 );
			}
		}
	}

	ScopedFBOBinding fbo( production.GetGLID(), ScopedFBOBinding::RB_REVERT );
	glViewport( 0, 0, kMapSize, kMapSize );
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClear( GL_COLOR_BUFFER_BIT );
	if( vertices.empty() )
		return;

	const float flux = FluxFromParam( Effective( PT_FLUX ) );
	const float audioFlux = 1.0f + 2.0f * std::clamp( params[ PT_AUDIO_FLUX ], 0.0f, 1.0f ) * analyser.Level();

	ScopedShaderBinding shader( splatShader.GetGLID() );
	splatShader.Set( "MapA", kMapA );
	splatShader.Set( "MapS", std::asinh( kMapU / kMapA ) );
	splatShader.Set( "MapSize", static_cast< float >( kMapSize ) );
	splatShader.Set( "Thickness", ThicknessFromParam( Effective( PT_THICKNESS ) ) );
	splatShader.Set( "FluxScale", flux * audioFlux );
	splatShader.Set( "LnEnergy", std::log( EnergyFromParam( Effective( PT_ENERGY ) ) ) );
	setAdditiveBlend();
	glBindVertexArray( splatVAO );
	glBindBuffer( GL_ARRAY_BUFFER, splatVBO );
	glBufferData( GL_ARRAY_BUFFER, static_cast< GLsizeiptr >( vertices.size() * sizeof( float ) ), vertices.data(),
	              GL_STREAM_DRAW );
	const GLsizei stride = 11 * sizeof( float );
	glEnableVertexAttribArray( 0 );
	glVertexAttribPointer( 0, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast< const void* >( 0 ) );
	glEnableVertexAttribArray( 1 );
	glVertexAttribPointer( 1, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast< const void* >( 4 * sizeof( float ) ) );
	glEnableVertexAttribArray( 2 );
	glVertexAttribPointer( 2, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast< const void* >( 8 * sizeof( float ) ) );
	glDrawArrays( GL_TRIANGLES, 0, static_cast< GLsizei >( vertices.size() / 11 ) );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glBindVertexArray( 0 );
	glDisable( GL_BLEND );
}

//---------------------------------------------------------------------------
void BorealPlugin::setMarchUniforms( FFGLShader& shader )
{
	shader.Set( "State", 0 );
	shader.Set( "Production", 1 );
	shader.Set( "Occupancy", 2 );
	shader.Set( "Shapes", 3 );
	shader.Set( "Columns", 4 );
	shader.Set( "MapA", kMapA );
	shader.Set( "MapS", std::asinh( kMapU / kMapA ) );
	shader.Set( "MapSize", static_cast< float >( kMapSize ) );
	shader.Set( "OccThreshold", kOccThreshold );
	shader.Set( "LnLow", static_cast< float >( std::log( emission::kLowKeV ) ) );
	shader.Set( "LnHigh", static_cast< float >( std::log( emission::kHighKeV ) ) );
	shader.Set( "FieldDown", view.fieldDown[ 0 ], view.fieldDown[ 1 ], view.fieldDown[ 2 ] );
	shader.Set( "MagEast", view.magEast[ 0 ], view.magEast[ 1 ] );
	shader.Set( "MagPole", view.magPole[ 0 ], view.magPole[ 1 ] );
	shader.Set( "A5577", static_cast< float >( emission::kA5577 ) );
	shader.Set( "ARed", static_cast< float >( emission::kA6300 + emission::kA6364 ) );
	shader.Set( "AirglowKR", AirglowFromParam( Effective( PT_AIRGLOW ) ) );
	shader.Set( "AirglowKm", static_cast< float >( optics::kAirglowKm ) );
	shader.Set( "AirglowSigma", static_cast< float >( optics::kAirglowSigma ) );
	shader.Set( "MaxSteps", kMaxMarchSteps );
	shader.Set( "ChannelMask", channelMask[ 0 ], channelMask[ 1 ], channelMask[ 2 ], channelMask[ 3 ] );
	shader.Set( "ClipSouthForTest", clipSouth ? 1 : 0 );

	//The ray spectrum: seeded wavelengths from 0.5 to 5 km (a filament a few
	//hundred metres across is the order of the thinnest rays photographed),
	//amplitudes normalised so the mean of s^2 is 1; the phases drift at
	//0.01-0.06 Hz and ride the convection, reduced in double.
	{
		float rayK[ shaders::kRayTerms ], rayAmp[ shaders::kRayTerms ], rayPhase[ shaders::kRayTerms ];
		const uint32_t seed = SeedFromParam( params[ PT_SEED ] ) * 7919u + 17u;
		const double drift  = DriftFromParam( Effective( PT_DRIFT ) );
		double norm         = 0.0;
		for( int k = 0; k < shaders::kRayTerms; ++k )
		{
			const auto h = [ & ]( uint32_t salt ) {
				return engine::PcgHash( seed + static_cast< uint32_t >( k ) * 131u + salt ) / 4294967296.0;
			};
			const double lambda = kRayShortest * std::pow( 10.0, ( k + h( 1 ) ) / shaders::kRayTerms );
			const double wave   = 2.0 * kPi / lambda;
			const double omega  = 2.0 * kPi * ( 0.01 + 0.05 * h( 2 ) ) * ( h( 3 ) < 0.5 ? -1.0 : 1.0 );
			double phase        = 2.0 * kPi * h( 4 ) + omega * skyTime - wave * std::fmod( drift * skyTime, lambda );
			phase -= 2.0 * kPi * std::floor( phase / ( 2.0 * kPi ) );
			rayK[ k ]     = static_cast< float >( wave );
			//Longer filaments carry more: amplitude ~ sqrt( lambda ).
			rayAmp[ k ]   = static_cast< float >( std::sqrt( lambda ) * ( 0.5 + h( 5 ) ) );
			rayPhase[ k ] = static_cast< float >( phase );
			norm += rayAmp[ k ] * rayAmp[ k ];
		}
		for( float& amp : rayAmp )
			amp *= static_cast< float >( std::sqrt( 2.0 / norm ) );
		const GLuint program = shader.GetGLID();
		glUniform1fv( glGetUniformLocation( program, "RayK" ), shaders::kRayTerms, rayK );
		glUniform1fv( glGetUniformLocation( program, "RayAmp" ), shaders::kRayTerms, rayAmp );
		glUniform1fv( glGetUniformLocation( program, "RayPhase" ), shaders::kRayTerms, rayPhase );
		shader.Set( "Rays", RaysFromParam( Effective( PT_RAYS ) ) );
	}

	float xyz[ optics::kComponents * 3 ], scot[ optics::kComponents ], nm[ optics::kComponents ];
	int channel[ optics::kComponents ];
	for( int c = 0; c < optics::kComponents; ++c )
	{
		double v[ 3 ], s = 0.0;
		optics::ComponentXYZ( optics::kComponentTable[ c ], v, s );
		for( int i = 0; i < 3; ++i )
			xyz[ c * 3 + i ] = static_cast< float >( v[ i ] );
		scot[ c ]    = static_cast< float >( s );
		nm[ c ]      = static_cast< float >( optics::kComponentTable[ c ].nm );
		channel[ c ] = optics::kComponentTable[ c ].channel;
	}
	const GLuint program = shader.GetGLID();
	glUniform3fv( glGetUniformLocation( program, "CompXYZ" ), optics::kComponents, xyz );
	glUniform1fv( glGetUniformLocation( program, "CompScotopic" ), optics::kComponents, scot );
	glUniform1fv( glGetUniformLocation( program, "CompNm" ), optics::kComponents, nm );
	glUniform1iv( glGetUniformLocation( program, "CompChannel" ), optics::kComponents, channel );
	shader.Set( "Extinction", ExtinctionFromParam( Effective( PT_EXTINCTION ) ) );
	shader.Set( "DisplayGain", std::exp2( ExposureFromParam( Effective( PT_EXPOSURE ) ) ) * kDisplayScale );
	shader.Set( "Observer", optionIndex( Effective( PT_OBSERVER ), 2 ) );
}

//---------------------------------------------------------------------------
FFResult BorealPlugin::ProcessOpenGL( ProcessOpenGLStruct* pgl )
{
	if( pgl == nullptr )
		return FF_FAIL;
	const FFGLTextureStruct* input = nullptr;
	if( isEffect )
	{
		if( pgl->numInputTextures < 1 || pgl->inputTextures[ 0 ] == nullptr )
			return FF_FAIL;
		input = pgl->inputTextures[ 0 ];
	}

	ScopedGLState restore;
	const GLint* hostViewport = restore.saved.viewport;
	const int width           = input ? static_cast< int >( input->Width ) : hostViewport[ 2 ];
	const int height          = input ? static_cast< int >( input->Height ) : hostViewport[ 3 ];
	if( width <= 0 || height <= 0 )
		return FF_FAIL;
	glDisable( GL_BLEND );

	//-------------------------------------------------------------------
	// Time. A backwards or large step is a jump -- a clip trigger or a scrub:
	// no sky time passes across it, and the audio analyser starts over
	// (primed, so the first hit after it still counts).
	//-------------------------------------------------------------------
	UpdateClock();
	if( settledJump )
	{
		lastNow     = -1.0;
		settledJump = false;
	}
	double hostDt = 0.0;
	if( lastNow >= 0.0 )
	{
		const double step = now - lastNow;
		if( step < 0.0 || step > kMaxFrameDelta )
			analyser.Reset();
		else
			hostDt = step;
	}
	lastNow = now;

	{
		float bins[ audio::kBins ] = {};
		int count                  = 0;
		if( const ParamInfo* info = FindParamInfo( PT_AUDIO ) )
		{
			count = static_cast< int >( std::min< size_t >( info->elements.size(), audio::kBins ) );
			for( int i = 0; i < count; ++i )
				bins[ i ] = info->elements[ static_cast< size_t >( i ) ].value;
		}
		audio::Settings settings;
		settings.sensitivity = std::clamp( params[ PT_AUDIO_SUBSTORM ], 0.0f, 1.0f );
		analyser.SetPrimingForTest( !unprimed );
		analyser.Update( bins, count, static_cast< float >( hostDt ), settings );
		if( params[ PT_AUDIO_SUBSTORM ] > 0.0f && analyser.Fired() )
			++substormPresses;
	}

	frameDt = hostDt * SpeedFromParam( Effective( PT_SPEED ) );
	skyTime += frameDt;

	//-------------------------------------------------------------------
	// Everything allocated before anything is bound.
	//-------------------------------------------------------------------
	ensureTables( ActivityFromParam( Effective( PT_ACTIVITY ) ) );
	if( !ensureBuffers( width, height ) )
	{
		diag::error( "could not allocate the pass buffers" );
		return FF_FAIL;
	}
	buildView( width, height );

	//-------------------------------------------------------------------
	// The engine: take the previous job's sheet, post this frame's.
	//-------------------------------------------------------------------
	if( frozen )
		shown = frozenSnapshot;
	else
	{
		engine::Engine::Job job;
		job.skyTime   = skyTime;
		job.params    = engineParams();
		job.substorms = substormPresses;
		job.calm      = calmWanted;
		substormsFired += static_cast< unsigned long long >( substormPresses );
		substormPresses = 0;
		calmWanted      = false;
		if( !engineStarted )
		{
			engineCore.Run( job );
			shown         = engineCore.Wait();
			engineStarted = true;
		}
		else
		{
			shown = engineCore.Wait();
			engineCore.Submit( job );
		}
	}

	//-------------------------------------------------------------------
	// 1. Splat.
	//-------------------------------------------------------------------
	splat( shown );

	//-------------------------------------------------------------------
	// 2. Update the populations.
	//-------------------------------------------------------------------
	const float mapS = std::asinh( kMapU / kMapA );
	{
		PassBuffer& target = state[ 1 - stateIndex ];
		ScopedFBOBinding fbo( target.GetGLID(), ScopedFBOBinding::RB_REVERT );
		glViewport( 0, 0, kMapSize, kMapSize );
		ScopedShaderBinding shader( updateShader.GetGLID() );
		bindUnit( 0, state[ stateIndex ].TextureID() );
		bindUnit( 1, production.TextureID() );
		bindUnit( 2, columnTexture );
		updateShader.Set( "State", 0 );
		updateShader.Set( "Production", 1 );
		updateShader.Set( "Columns", 2 );
		updateShader.Set( "LnLow", static_cast< float >( std::log( emission::kLowKeV ) ) );
		updateShader.Set( "LnHigh", static_cast< float >( std::log( emission::kHighKeV ) ) );
		updateShader.Set( "MapA", kMapA );
		updateShader.Set( "MapS", mapS );
		updateShader.Set( "MapSize", static_cast< float >( kMapSize ) );
		updateShader.Set( "Dt", static_cast< float >( frameDt ) );
		const float wind = WindFromParam( Effective( PT_WIND ) );
		//Eastward geographic wind, in (magnetic east, poleward) components.
		updateShader.Set( "Wind", wind * view.magEast[ 0 ], wind * view.magPole[ 0 ] );
		updateShader.Set( "Diffusion", diffusion ? static_cast< float >( std::sqrt( 8.0 * kRedDiffusion * std::max( frameDt, 0.0 ) ) ) : 0.0f );
		quad.Draw();
		unbindTextureUnits( 3 );
	}
	stateIndex = 1 - stateIndex;

	//-------------------------------------------------------------------
	// 3. Occupancy, and its mip chain.
	//-------------------------------------------------------------------
	{
		ScopedFBOBinding fbo( occupancy.GetGLID(), ScopedFBOBinding::RB_REVERT );
		glViewport( 0, 0, kMapSize, kMapSize );
		ScopedShaderBinding shader( occupancyShader.GetGLID() );
		bindUnit( 0, state[ stateIndex ].TextureID() );
		bindUnit( 1, production.TextureID() );
		bindUnit( 2, columnTexture );
		occupancyShader.Set( "State", 0 );
		occupancyShader.Set( "Production", 1 );
		occupancyShader.Set( "Columns", 2 );
		occupancyShader.Set( "LnLow", static_cast< float >( std::log( emission::kLowKeV ) ) );
		occupancyShader.Set( "LnHigh", static_cast< float >( std::log( emission::kHighKeV ) ) );
		occupancyShader.Set( "MapSize", static_cast< float >( kMapSize ) );
		occupancyShader.Set( "A5577", static_cast< float >( emission::kA5577 ) );
		occupancyShader.Set( "ARed", static_cast< float >( emission::kA6300 + emission::kA6364 ) );
		quad.Draw();
		unbindTextureUnits( 3 );
	}
	occupancy.GenerateMipmaps();

	//-------------------------------------------------------------------
	// 4. March.
	//-------------------------------------------------------------------
	auto bindMarchInputs = [ & ]() {
		bindUnit( 0, state[ stateIndex ].TextureID() );
		bindUnit( 1, production.TextureID() );
		bindUnit( 2, occupancy.TextureID() );
		bindUnit( 3, shapeTexture );
		bindUnit( 4, columnTexture );
	};
	auto setCamera = [ & ]( FFGLShader& shader ) {
		shader.Set( "CameraKind", view.cameraKind );
		shader.Set( "Forward", view.forward[ 0 ], view.forward[ 1 ], view.forward[ 2 ] );
		shader.Set( "Right", view.right[ 0 ], view.right[ 1 ], view.right[ 2 ] );
		shader.Set( "Up", view.up[ 0 ], view.up[ 1 ], view.up[ 2 ] );
		shader.Set( "TanHalf", view.tanHalf );
		shader.Set( "LookAz", view.lookAz );
	};
	{
		ScopedFBOBinding fbo( march.GetGLID(), ScopedFBOBinding::RB_REVERT );
		glViewport( 0, 0, marchWidth, marchHeight );
		ScopedShaderBinding shader( marchShader.GetGLID() );
		bindMarchInputs();
		setMarchUniforms( marchShader );
		setCamera( marchShader );
		marchShader.Set( "Raster", static_cast< float >( marchWidth ), static_cast< float >( marchHeight ) );
		quad.Draw();
		unbindTextureUnits( 5 );
	}

	//-------------------------------------------------------------------
	// 5. The whole sky, for the light it throws on the scene.
	//-------------------------------------------------------------------
	const float illumination = isEffect ? IlluminationFromParam( params[ PT_ILLUMINATION ] ) : 0.0f;
	const int horizon        = optionIndex( Effective( PT_HORIZON ), static_cast< int >( Horizon::Count ) );
	const bool wantSky       = isEffect ? illumination > 0.0f : horizon != static_cast< int >( Horizon::None );
	if( wantSky )
	{
		ScopedFBOBinding fbo( allSky.GetGLID(), ScopedFBOBinding::RB_REVERT );
		glViewport( 0, 0, kAllSkySize, kAllSkySize );
		ScopedShaderBinding shader( allSkyShader.GetGLID() );
		bindMarchInputs();
		setMarchUniforms( allSkyShader );
		quad.Draw();
		unbindTextureUnits( 5 );
		allSky.GenerateMipmaps();
	}

	//-------------------------------------------------------------------
	// 6. Composite, into the host's framebuffer and viewport.
	//-------------------------------------------------------------------
	glBindFramebuffer( GL_FRAMEBUFFER, pgl->HostFBO );
	glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );
	{
		ScopedShaderBinding shader( compositeShader.GetGLID() );
		bindMarchInputs();
		//A sampler bound to texture 0 is "unloadable" to this driver, which says
		//so on stderr; the source has no clip, so it gets any real texture.
		bindUnit( 5, input ? input->Handle : march.TextureID() );
		bindUnit( 6, march.TextureID() );
		bindUnit( 7, allSky.TextureID() );
		setMarchUniforms( compositeShader );
		setCamera( compositeShader );
		compositeShader.Set( "InputTexture", 5 );
		compositeShader.Set( "March", 6 );
		compositeShader.Set( "AllSky", 7 );
		if( input )
		{
			const FFGLTexCoords maxCoords = GetMaxGLTexCoords( *input );
			compositeShader.Set( "MaxUV", maxCoords.s, maxCoords.t );
		}
		else
			compositeShader.Set( "MaxUV", 1.0f, 1.0f );
		compositeShader.Set( "IsEffect", isEffect ? 1 : 0 );
		compositeShader.Set( "Raster", static_cast< float >( width ), static_cast< float >( height ) );
		compositeShader.Set( "MarchSize", static_cast< float >( marchWidth ), static_cast< float >( marchHeight ) );
		compositeShader.Set( "OutputXYZ", outputXYZ ? 1 : 0 );
		compositeShader.Set( "SkyLight", wantSky ? 1.0f : 0.0f );
		compositeShader.Set( "Illumination", illumination );
		compositeShader.Set( "MaskMode", isEffect ? optionIndex( params[ PT_SKY_MASK ], 3 ) : 0 );
		compositeShader.Set( "MaskThreshold", MaskThresholdFromParam( params[ PT_MASK_THRESHOLD ] ) );
		compositeShader.Set( "MixAmount", isEffect ? std::clamp( params[ PT_MIX ], 0.0f, 1.0f ) : 1.0f );
		compositeShader.Set( "HorizonKind", horizon );
		compositeShader.Set( "SnowAlbedo", kSnowAlbedo );
		compositeShader.Set( "StarGain", StarsFromParam( Effective( PT_STARS ) ) );
		const GLuint program = compositeShader.GetGLID();
		const uint32_t seed  = SeedFromParam( params[ PT_SEED ] );
		glUniform1ui( glGetUniformLocation( program, "HorizonSeed" ), engine::PcgHash( seed * 3u + 1u ) );
		glUniform1ui( glGetUniformLocation( program, "StarSeed" ), engine::PcgHash( seed * 3u + 2u ) );

		//Local (east, north, up) -> celestial: the pole at the observer's
		//latitude, turned by the sidereal angle (reduced in double).
		const double lat = ( optionIndex( Effective( PT_HEMISPHERE ), 2 ) == 1 ? -1.0 : 1.0 ) * kObserverLatDeg * kPi / 180.0;
		const double pole[ 3 ] = { 0.0, std::cos( lat ), std::sin( lat ) };
		const double ex[ 3 ]   = { 1.0, 0.0, 0.0 };
		const double ey[ 3 ]   = { pole[ 1 ] * ex[ 2 ] - pole[ 2 ] * ex[ 1 ], pole[ 2 ] * ex[ 0 ] - pole[ 0 ] * ex[ 2 ],
			                       pole[ 0 ] * ex[ 1 ] - pole[ 1 ] * ex[ 0 ] };
		double angle = Effective( PT_STAR_MOTION ) > 0.5f ? 2.0 * kPi * skyTime / kSiderealDay : 0.0;
		angle -= 2.0 * kPi * std::floor( angle / ( 2.0 * kPi ) );
		const double c = std::cos( angle ), s = std::sin( angle );
		float m[ 9 ];//column-major: m[ col * 3 + row ], rows are the celestial axes
		for( int col = 0; col < 3; ++col )
		{
			const double rx = ex[ col ], ry = ey[ col ], rz = pole[ col ];
			m[ col * 3 + 0 ] = static_cast< float >( c * rx - s * ry );
			m[ col * 3 + 1 ] = static_cast< float >( s * rx + c * ry );
			m[ col * 3 + 2 ] = static_cast< float >( rz );
		}
		glUniformMatrix3fv( glGetUniformLocation( program, "LocalToSky" ), 1, GL_FALSE, m );
		const float pixelAngle = view.cameraKind == 0
		                             ? 2.0f * view.tanHalf / static_cast< float >( height )
		                             : static_cast< float >( kPi ) / static_cast< float >( std::min( width, height ) );
		compositeShader.Set( "PixelAngle", pixelAngle );
		quad.Draw();
		unbindTextureUnits( 8 );
	}

	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult BorealPlugin::DeInitGL()
{
	for( FFGLShader* shader : { &splatShader, &updateShader, &occupancyShader, &marchShader, &allSkyShader, &compositeShader } )
		shader->FreeGLResources();
	quad.Release();
	production.Destroy();
	for( PassBuffer& buffer : state )
		buffer.Destroy();
	occupancy.Destroy();
	march.Destroy();
	allSky.Destroy();
	if( splatVBO )
		glDeleteBuffers( 1, &splatVBO );
	if( splatVAO )
		glDeleteVertexArrays( 1, &splatVAO );
	if( shapeTexture )
		glDeleteTextures( 1, &shapeTexture );
	if( columnTexture )
		glDeleteTextures( 1, &columnTexture );
	splatVBO = splatVAO = shapeTexture = columnTexture = 0;
	tablesActivity = -1.0;
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
float BorealPlugin::Effective( unsigned int index ) const
{
	const int preset = optionIndex( params[ PT_PRESET ], presets::kCount + 1 );
	if( preset > 0 )
	{
		const presets::Preset& row = presets::kPresets[ preset - 1 ];
		for( int c = 0; c < presets::kParamCount; ++c )
			if( kPresetTarget[ c ] == index )
				return row.v[ c ];
	}
	return index < PT_COUNT ? params[ index ] : 0.0f;
}

FFResult BorealPlugin::SetTime( double time )
{
	hostTime = time;
	return FF_SUCCESS;
}

char* BorealPlugin::GetTextParameter( unsigned int index )
{
	if( index == PT_ABOUT_TEXT )
	{
		static const std::string text = stoatworks::about::textParam( 0 );
		return const_cast< char* >( text.c_str() );
	}
	return CFFGLPlugin::GetTextParameter( index );
}

FFResult BorealPlugin::SetTextParameter( unsigned int index, const char* value )
{
	if( index == PT_ABOUT_TEXT )
		return FF_SUCCESS;
	return CFFGLPlugin::SetTextParameter( index, value );
}

FFResult BorealPlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= ParamCount() )
		return FF_FAIL;
	if( index >= PT_ABOUT_TEXT && index < PT_SOURCE_COUNT )
		return stoatworks::about::handleParam( index - PT_ABOUT_TEXT, value ) ? FF_SUCCESS : FF_FAIL;

	//Events act on the rising edge, once.
	const bool down = value >= 0.5f;
	if( index == PT_SUBSTORM )
	{
		if( down && !substormHeld )
			++substormPresses;
		substormHeld = down;
	}
	else if( index == PT_CALM )
	{
		if( down && !calmHeld )
			calmWanted = true;
		calmHeld = down;
	}
	params[ index ] = value;
	return FF_SUCCESS;
}

float BorealPlugin::GetFloatParameter( unsigned int index )
{
	return index < ParamCount() ? params[ index ] : 0.0f;
}

//---------------------------------------------------------------------------
void BorealPlugin::SetClockScaleForTest( double scale )
{
	clockScale = scale;
}

void BorealPlugin::FreezeNodesForTest( const std::vector< engine::Node >& nodes, const std::vector< int >& arcStart )
{
	frozen                  = true;
	frozenSnapshot.nodes    = nodes;
	frozenSnapshot.arcStart = arcStart;
}

void BorealPlugin::SetChannelMaskForTest( float g, float r, float b, float p )
{
	channelMask[ 0 ] = g;
	channelMask[ 1 ] = r;
	channelMask[ 2 ] = b;
	channelMask[ 3 ] = p;
}

void BorealPlugin::SetOutputXYZForTest( bool xyz )
{
	outputXYZ = xyz;
}

void BorealPlugin::SetUnprimedForTest( bool value )
{
	unprimed = value;
}

GLuint BorealPlugin::StateTextureID() const
{
	return state[ stateIndex ].TextureID();
}

GLuint BorealPlugin::MarchTextureID() const
{
	return march.TextureID();
}

} // namespace boreal
