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
bin/benchmark  # GPU rendering benchmark at 1080p and 4K (--cpu: without OpenGL)
bin/install    # makepkg -fsi: installs the binary, launcher entry, and icon
```

After `bin/install`, Omawing is in the app launcher (Super + Alt + Space).

`OMAWING_STATS=1 omawing` prints the frame rate, paint time, longest gap between
frames, and the range of simulation steps once a second, logs every hitch (a gap
over 25 ms), and says whether painting runs on the GPU or the CPU.

The renderer rejects invisible polygons early and uses the GPU's convex-polygon
path for the checkerboard and meshes. It retains native resolution and the full
nine-scene sequence. `bin/benchmark` measures complete GPU frames (including a
GPU finish), so its timings differ from the CPU paint timings in `OMAWING_STATS`.
Besides each scene it times a few of the heaviest moments: flying at the
ceiling, a full sky of particles over Neon Mirage's mirror floor, a wreck, and
a strong beat.

The build uses `-Wall -Wextra` and the tree compiles without warnings. The
sources are listed once, in `src/src.pri`, which the app, the tests, and the
benchmark all include; the version is set there too. Every push and pull
request is built and tested by [a workflow](../.github/workflows/ci.yml), and
the release recipe's `check()` runs the tests on the tagged source before it is
packaged.

When releasing, the version is in `src/src.pri`, `pkgbuild/PKGBUILD`,
`aur/PKGBUILD`, and `aur/.SRCINFO` (`aur/publish` regenerates it); the release
workflow checks they agree.

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
| `tests/render_benchmark.cpp` | The frame-time benchmark (`bin/benchmark`). |
| `bin/generate-effects` | Writes the WAV effects in `src/audio`. |
| `pkgbuild/`, `aur/` | The local package recipe (`bin/install`) and the release recipe. |

## Limits

Collisions use simple ship-centred bounds rather than exact wing geometry.
Faces are sorted by average depth, so intersecting geometry can occasionally
overlap wrongly. The high-score board is local to this computer.

