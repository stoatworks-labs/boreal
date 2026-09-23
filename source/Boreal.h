#pragma once

#include "Audio.h"
#include "Controls.h"
#include "PassBuffer.h"
#include "Presets.h"
#include "engine/Engine.h"
#include "physics/Emission.h"

#include <FFGLSDK.h>

// After FFGLSDK.h, which is where FFUInt32 comes from.
#include "StoatworksAboutParams.h"

#include <array>
#include <memory>
#include <vector>

namespace boreal
{
/// The geometry of the sky for one frame: what the shaders are handed, and
/// what the harness reproduces on the CPU to know where a pixel looks.
struct View
{
	int cameraKind = 0;
	float forward[ 3 ] = { 0, 1, 0 };
	float right[ 3 ]   = { 1, 0, 0 };
	float up[ 3 ]      = { 0, 0, 1 };
	float tanHalf      = 1.0f;
	float lookAz       = 0.0f;///< radians
	float fieldDown[ 3 ] = { 0, 0, -1 };
	float magEast[ 2 ]   = { 1, 0 };
	float magPole[ 2 ]   = { 0, 1 };
};

/**
    The plugin: the source (the night sky) and, with `isEffect`, the Over
    effect (the aurora added to the clip as light). One class, two
    registrations; see SourcePlugin.cpp and EffectPlugin.cpp.
*/
class BorealPlugin : public CFFGLPlugin
{
public:
	explicit BorealPlugin( bool isEffect );
	~BorealPlugin() override;

	FFResult InitGL( const FFGLViewportStruct* viewport ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* input ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetTextParameter( unsigned int index ) override;
	/// See millpond: the base class's stub fails, and a failed default deletes
	/// the instance -- so without this no real host can load the plugin.
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	FFResult SetTime( double time ) override;

	/// The value a parameter renders with: the active preset's where it has
	/// one, the host's otherwise. Presets are an OVERRIDE (Resolume ignores
	/// value events), and this is the one place that reads them.
	float Effective( unsigned int index ) const;

	bool IsEffect() const
	{
		return isEffect;
	}
	unsigned int ParamCount() const
	{
		return isEffect ? PT_COUNT : PT_SOURCE_COUNT;
	}

	//-------------------------------------------------------------------
	// For the harness. Nothing in the plugin's own operation calls these.
	//-------------------------------------------------------------------
	void SetClockScaleForTest( double scale );
	/// Replace the engine's output with these nodes (engine coordinates),
	/// every frame from now on. The dynamics stop; the optics are then
	/// measured against a footprint whose every node is known.
	void FreezeNodesForTest( const std::vector< engine::Node >& nodes, const std::vector< int >& arcStart );
	void SetChannelMaskForTest( float g, float r, float b, float p );
	void SetOutputXYZForTest( bool xyz );
	/// Keep the analyser deaf on its first frame too (--onset's negative control).
	void SetUnprimedForTest( bool unprimed );
	/// The red population's small diffusion, off, so --lifetime sees decay alone.
	void SetDiffusionForTest( bool on )
	{
		diffusion = on;
	}
	GLuint StateTextureID() const;
	GLuint MarchTextureID() const;
	GLuint ShapeTextureID() const
	{
		return shapeTexture;
	}
	GLuint ColumnTextureID() const
	{
		return columnTexture;
	}
	int MarchWidth() const
	{
		return marchWidth;
	}
	int MarchHeight() const
	{
		return marchHeight;
	}
	int MapSize() const
	{
		return kMapSize;
	}
	const View& CurrentView() const
	{
		return view;
	}
	double SkyTime() const
	{
		return skyTime;
	}
	const engine::Snapshot& LastSnapshot() const
	{
		return shown;
	}
	unsigned long long SubstormsFired() const
	{
		return substormsFired;
	}
	const emission::Tables& CurrentTables() const
	{
		return tables;
	}
	double EngineMs() const
	{
		return shown.cpuMs;
	}

	static constexpr int kMapSize = 1024;
	static constexpr float kMapA  = 80.0f;  ///< km: the stretch's linear core
	static constexpr float kMapU  = 2000.0f;///< km: the map's half-width

private:
	void UpdateClock();
	bool ensureBuffers( int width, int height );
	void ensureTables( double activity );
	void buildView( int width, int height );
	void splat( const engine::Snapshot& snap );
	void setMarchUniforms( ffglex::FFGLShader& shader );
	engine::Params engineParams() const;

	const bool isEffect;
	float params[ PT_COUNT ] = {};

	ffglex::FFGLShader splatShader, updateShader, occupancyShader, marchShader, allSkyShader, compositeShader;
	ffglex::FFGLScreenQuad quad;

	PassBuffer production;
	PassBuffer state[ 2 ];
	PassBuffer occupancy;
	PassBuffer march;
	PassBuffer allSky;
	int stateIndex  = 0;
	int marchWidth  = 0;
	int marchHeight = 0;

	GLuint splatVAO = 0, splatVBO = 0;
	GLuint shapeTexture = 0, columnTexture = 0;
	emission::Tables tables;
	double tablesActivity = -1.0;

	View view;

	engine::Engine engineCore;
	engine::Snapshot shown;
	bool engineStarted = false;
	bool frozen        = false;
	engine::Snapshot frozenSnapshot;

	//Time. Resolume has sent both seconds and milliseconds (see millpond).
	double hostTime = -1.0, lastRawTime = -1.0, lastWallTime = -1.0, wallStart = -1.0;
	double clockScale = 0.0;
	int secondsVotes = 0, millisVotes = 0;
	double now = 0.0, lastNow = -1.0;
	bool settledJump = false;
	double skyTime   = 0.0;
	double frameDt   = 0.0;///< sky seconds this frame

	int substormPresses = 0;
	bool substormHeld = false, calmHeld = false, calmWanted = false;
	audio::Analyser analyser;
	bool unprimed = false;
	unsigned long long substormsFired = 0;

	float channelMask[ 4 ] = { 1, 1, 1, 1 };
	bool outputXYZ         = false;
	bool diffusion         = true;
};

} // namespace boreal
