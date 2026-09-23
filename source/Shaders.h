#pragma once

#include <string>

/**
    The GLSL, as text.

    `kCommon` is a LIBRARY, not a shader: no #version, no main. Every pass is
    assembled as kVersion + kCommon + its body, and so is the harness's probe
    (`brtest --extinction`, `--corona`), so a check runs the text the plugin
    runs rather than a transcription of it. Pieces are kept under MSVC's
    ~16 KB literal cap; tools/verify.sh reassembles them for glslc.

    Passes, in order:

      1. splat     the sheet's segments -> production map (energy flux, flux ln E0)
      2. update    production + last state -> state: the exact exponential
                   integrator for the O(1S) and O(1D) populations; the red one
                   advected by the neutral wind
      3. occupancy the column emission of every map texel, kR, mipmapped: the
                   march's empty-space test and the upsample's guide
      4. march     one ray per pixel of a Detail-sized buffer through the
                   80-800 km shell; four channels of kR
      5. allsky    the same march on a 32x32 all-sky grid, cosine-weighted, for
                   the Over effect's Illumination
      6. composite upsample, extinction, colour, observer, stars, horizon, clip
*/
namespace boreal::shaders
{
extern const char* const kVersion;
extern const char* const kCommon;

extern const char* const kQuadVertex;
extern const char* const kSplatVertex;
extern const char* const kSplatFragment;
extern const char* const kUpdateFragment;
extern const char* const kOccupancyFragment;
extern const char* const kMarchLibrary;///< the march itself, shared by passes 4 and 5
extern const char* const kMarchFragment;
extern const char* const kAllSkyFragment;
extern const char* const kCompositeFragment;

/// kVersion + kCommon + the pieces, in order.
std::string Assemble( const char* a, const char* b = nullptr, const char* c = nullptr );

/// The ray modulation's spectrum has this many terms.
constexpr int kRayTerms = 8;

} // namespace boreal::shaders
