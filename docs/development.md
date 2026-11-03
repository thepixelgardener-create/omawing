# Developing Omawing

[Home](../README.md) · [Gallery](gallery.md) · [Playing](playing.md) · [Install](install.md) · [Development](development.md)

Omawing uses C++ and Qt Quick, built with qmake6. Install Qt 6.8 or newer
(Base, Declarative, Wayland, Multimedia with its FFmpeg backend, and SVG) and a
C++20 compiler. The build stops with a clear message on an older Qt: the beat
tracker listens through `QAudioBufferOutput`, new in 6.8.
The renderer projects low-poly geometry into a GPU-backed QPainter surface.

## Build and install from a clone

```sh
bin/build      # build/omawing; extra arguments go to qmake
bin/test       # Qt Test, offscreen; audio checks run silently
bin/benchmark  # GPU frame times at 1080p and 4K (--cpu: without OpenGL; --parts: per layer)
bin/install    # makepkg -fsi: installs the binary, launcher entry, and icon
```

After `bin/install`, Omawing is in the app launcher (Super + Alt + Space).

`OMAWING_STATS=1 omawing` prints the frame rate, paint time, longest gap between
frames, and the range of simulation steps once a second, logs every hitch (a gap
over 1.5 display refresh periods), and says whether painting runs on the GPU or the CPU.
The game stops ticking while paused once its fades have settled, and while its
window is hidden; the first frame after either isn't counted as a hitch.

The renderer rejects invisible polygons early and uses the GPU's convex-polygon
path for the checkerboard and meshes. It retains native resolution and the full
nine-scene sequence. `bin/benchmark` measures complete GPU frames (including a
GPU finish), using a four-sample framebuffer and an eight-bit stencil
buffer in both the context and framebuffer. Each run reports the renderer and
actual buffer settings and fails if these are unavailable. Its timings differ
from the CPU paint timings in `OMAWING_STATS`.
Besides each scene it times a few of the heaviest moments: flying at the
ceiling, a full sky of particles over Neon Mirage's mirror floor, a wreck, a
strong beat, and the change into Neon Mirage. `--parts` also times each layer (sky, ground, building the
world, painting it, and the overlay), waiting for the GPU after each, so the
totals come out a little higher.

Timings depend on the machine; the painting-work check is a deterministic
regression proxy. `bin/benchmark --check tests/benchmark-baseline.tsv` paints
two seconds of each scene after warming it, one whole colour transition. From
the busiest frame it takes the draw calls, polygon points, and painter state
changes, which may exceed the baseline by 3% and 16 units of rounding slack.
Over all the frames it counts new filled-path points (geometry the GPU engine
must cut into triangles again), new images (textures it must upload again),
and HUD strings laid out afresh; the images and layouts may not exceed the
baseline at all, since a few more of them is what a cache going wrong looks
like. Besides the scenes and heavy moments, it paints a HUD that changes every
frame, the same in Arabic, more new strings than the HUD's cache holds, a
window being resized, and a move between screens of different scales.
The counters do not measure GPU work or actual triangulation: Qt can
stencil-fill paths, and its path caches go by the path, not its value as the
counter does, so a path made again with the same points isn't counted. Use
frame timings to validate optimizations.
CI checks the counters on pushes to main and pull requests, and reports CPU
frame times on the run's summary page. After reviewing an intentional change
in painting work, record a new baseline with
`bin/benchmark --record tests/benchmark-baseline.tsv` and commit it.

The tests check the pictures too, on the CPU, where painting is repeatable: a
scene that has painted other acts, sizes, display scales, Arabic, and more HUD
strings than its cache holds must paint a frame exactly as a new scene does;
changing one line of the HUD must change the picture there and nowhere else;
and twice the display scale must give the same frame as twice the size. With
OpenGL available (not in CI) a frame painted on the GPU is compared with the
CPU's, averaged over 32-pixel blocks.

For a local timing run without opening a window:

```sh
QT_QPA_PLATFORM=offscreen bin/benchmark --frames 120
QT_QPA_PLATFORM=offscreen bin/benchmark --cpu --parts --frames 20 --size 1920x1080
```

Keep the machine idle and repeat runs with the same frame count, dimensions,
and graphics driver. These timings cover scene painting, not simulation,
Qt Quick composition, presentation, or display latency. `--parts` adds GPU
synchronization between layers; use normal runs for overall frame times.

The build uses `-Wall -Wextra` and the tree compiles without warnings. The
sources are listed once, in `src/src.pri`, which the app, the tests, and the
benchmark all include; the version is set there too. Every push and pull
request is built and tested by [a workflow](../.github/workflows/ci.yml), and
the release recipe's `check()` runs the tests on the tagged source before it is
packaged.

When releasing, the version is in `src/src.pri`, `pkgbuild/PKGBUILD`, and
`release/PKGBUILD`; the release workflow checks they agree.

## Source

| File | Job |
| --- | --- |
| `src/flight.{h,cpp}` | The ship, chase camera, particles, scenery layout, and act timing. No Qt types. |
| `src/game.{h,cpp}` | The arcade rules: rings, the multiplier, hull damage, and wrecks. No Qt types. |
| `src/geometry.h` | Vector helpers, in namespace `geo`. |
| `src/scene.{h,cpp}` | Puts a frame together from the layers below. |
| `src/view.{h,cpp}` | What the layers share: projection, clipping, fog, and the act's colours and style. |
| `src/sky.{h,cpp}` | Sky, copper bars, stars, sun, clouds, parallax hills, and the ground's gradient. |
| `src/ground.{h,cpp}` | The checkerboard, grid lines, and the ship's shadow. |
| `src/worldlayer.{h,cpp}` | Scenery, rings, the ship, and particles, sorted far to near. |
| `src/overlay.{h,cpp}` | Speed lines, ring points, scanlines, title, HUD, radar, and the sine scroller. |
| `src/shipshape.h` | The hull and wing geometry, shared by the ship and its shadow. |
| `src/scoreboard.{h,cpp}` | The saved top ten, as a list model for QML. |
| `src/audio.{h,cpp}` | The MP3 playlist and gameplay effects, through Qt Multimedia. |
| `src/backend.{h,cpp}` | State and actions for QML: pause, autopilot, sound, input, and preferences. |
| `src/flightview.{h,cpp}` | The Qt Quick item that paints the scene. |
| `src/Main.qml` | The window, toolbar, shortcut overlay, volume panel, and high-score board. |
| `src/theme.*`, `src/translator.*` | Omarchy colours, rounding, and language. |
| `src/main.cpp` | Starts the app: style, translator, theme, backend, and the QML window. |
| `src/i18n/*.json` | One catalog per Omarchy language, English source text to translation. |
| `src/audio/` | The soundtrack (MP3, installed as files) and the effects (WAV, compiled in). |
| `src/resources.qrc` | What is compiled in: `Main.qml`, the effects, and the catalogs. |
| `src/src.pri` | The source list, version, and compiler settings shared by every build. |
| `tests/omawing_tests.cpp` | The Qt Test suite (`bin/test`). |
| `tests/render_benchmark.cpp` | The frame-time benchmark and painting-work check (`bin/benchmark`). |
| `tests/benchmark-baseline.tsv` | The painting work each benchmark scene is held to. |
| `bin/generate-effects` | Writes the WAV effects in `src/audio`. |
| `pkgbuild/`, `release/` | The local package recipe (`bin/install`) and the release recipe. |

## Limits

Collisions use simple ship-centred bounds rather than exact wing geometry.
Faces are sorted by average depth, so intersecting geometry can occasionally
overlap wrongly. The high-score board is local to this computer.


The benchmark also exercises a sustained maximum-particle load and repeated
Neon Mirage colour transitions. It reports p99 alongside median and p95;
use long runs (for example `--frames 600`) to make the tail statistics useful.
Particle replenishment and fixture setup occur outside the paint timer.
The HUD text cache evicts its least recently used entry at its 512-entry
limit, preserving frequently used labels instead of clearing all layouts.
