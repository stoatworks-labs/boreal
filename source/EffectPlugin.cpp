#include "Boreal.h"

/**
    The effect: the same sky, added to the clip as LIGHT -- emission is additive,
    never an alpha composite -- with a mask to keep a skyline in front and the
    aurora's own light thrown back onto the scene.

    See SourcePlugin.cpp for why this file is listed in its own target.
*/
namespace
{
class BorealEffect : public boreal::BorealPlugin
{
public:
	BorealEffect() :
		BorealPlugin( true )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< BorealEffect >,// Create method
	"BR02",                       // Plugin unique ID of maximum length 4
	"SW Boreal Over",             // Plugin name
	2,                            // API major version number
	1,                            // API minor version number
	0,                            // Plugin major version number
	1,                            // Plugin minor version number
	FF_EFFECT,                    // Plugin type
	"The aurora over the clip, added as light. Sky Mask keeps a skyline in front (its alpha, or its dark areas as "
	"the sky); Illumination lights the scene with the sky's own colour, so snow goes green under a strong display. "
	"Start from a Preset.",
	"Boreal FFGL effect"          // About
);

extern "C" const char* BorealEffectBuildStamp()
{
	return "boreal " BOREAL_VERSION " effect, built " __DATE__ " " __TIME__;
}
