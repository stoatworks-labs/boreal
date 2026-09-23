#include "Boreal.h"

/**
    The source: the night sky, no input.

    Listed directly in the BorealSource target, not in boreal_core: both plugins
    share the class and not the `CFFGLPluginInfo` below, and putting either
    registration in the shared library would register both plugins into both
    bundles. The core is an OBJECT library because this registers itself from a
    file-scope constructor nothing references (see CMakeLists.txt).

    `SW Boreal` is nine characters; the FFGL name field is char[ 16 ] and not
    null-terminated. `oxbow probe` reads it back the way a host does.
*/
namespace
{
class BorealSource : public boreal::BorealPlugin
{
public:
	BorealSource() :
		BorealPlugin( false )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< BorealSource >,// Create method
	"BR01",                       // Plugin unique ID of maximum length 4
	"SW Boreal",                  // Plugin name
	2,                            // API major version number
	1,                            // API minor version number
	0,                            // Plugin major version number
	1,                            // Plugin minor version number
	FF_SOURCE,                    // Plugin type
	"The aurora borealis and australis. The arc is a vortex sheet of charge that rolls itself into curls, folds "
	"and surges; the electrons it accelerates light the green, red, blue and pink lines at the heights the "
	"atmosphere puts them; and a camera on the ground looks up through it all. Start from a Preset, at the bottom.",
	"Boreal FFGL source"          // About
);

extern "C" const char* BorealSourceBuildStamp()
{
	return "boreal " BOREAL_VERSION " source, built " __DATE__ " " __TIME__;
}
