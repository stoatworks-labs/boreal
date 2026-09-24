# demo/ — the browser demo

Live at **<https://boreal-demo.stoatworks-labs.com>**. Not served from this
README: `.assetsignore` keeps this file and `tools/` out of the upload.

    index.html      the shell
    plugin.js       the parameters, the shaders (verbatim), the six passes
    port.js         the CPU half, PORTED: Controls, Sheet, Engine, Atmosphere, Emission, Optics
    atmosphere.js   the NRLMSIS table, GENERATED from source/physics/AtmosphereTable.cpp
    vendor/         the shared kit, copied in by sync.sh — DO NOT EDIT
    tools/          check_shaders.py (run by tools/verify.sh), bake_atmosphere.py
    _headers        CSP and caching, honoured by the Cloudflare assets runtime

## What this page is, exactly

A **port**, not a recording and not the plugin.

The shaders are the plugin's, copied across unedited: the ten `R"( ... )"`
bodies of `source/Shaders.cpp`, assembled the way `Assemble()` assembles them
and run as the same six passes, in the same order, at the same map sizes as
`BorealPlugin::ProcessOpenGL`. `tools/check_shaders.py` compares them character
for character — and the NRLMSIS table and the seven preset rows, which are data
copied rather than ported — and `../tools/verify.sh` runs it.

The CPU half is a port, and **nothing checks it but a reader**: the vortex
sheet and its RK4, the engine (arcs, forcing, Substorm, Calm), the Knight
relation, the atmosphere's interpolation, Fang 2008 and the emission tables,
the colour components. The sheet is capped at **1024 nodes**, where the plugin
allows 4096, because the page runs the O(N²) sum on one JavaScript thread.

## What is deliberately absent

- **Anything audio**: the `Audio` FFT buffer, Audio Substorm and Audio Flux.
- **The About block.**
- **Seed 100–9999**: Seed is a dropdown of 0–99 (the kit has no integer control).

## Working on it

```bash
python3 -m http.server 8947 --directory demo   # from the repo root
python3 demo/tools/check_shaders.py            # the copies still match the C++
python3 demo/tools/bake_atmosphere.py          # after a rebake of AtmosphereTable.cpp
tools/verify.sh                                # everything, including the above
```

There is no build step: hand-written ES modules, and what is committed is what
is served. **After changing a shader in `source/Shaders.cpp`, copy it across**
(the five backticks in its comments are escaped as \\` in the template
literals); check_shaders.py names the first differing line. Do not edit the
GLSL in `plugin.js` to make something compile in WebGL2 — `port()` in
`vendor/gl.js` handles the version line and precision, and anything else is a
difference to say on the page. **After changing Sheet, Engine, Emission,
Atmosphere, Optics or Controls, change `port.js` to match.**

Deploy from the repo root with `cf-run npx wrangler deploy` (a push to main also
deploys, through `.github/workflows/deploy.yml`), and verify by content:

```bash
curl -s 'https://boreal-demo.stoatworks-labs.com/?cb=1' | grep -o '<title>[^<]*'
```
