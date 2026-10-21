# Developing Omawing

[Home](../README.md) · [Gallery](gallery.md) · [Playing](playing.md) · [Install](install.md) · [Development](development.md)

Omawing uses C++ and Qt Quick, built with qmake6. Install Qt 6 Base,
Declarative, Wayland, and Multimedia development packages and a C++ compiler.
The renderer projects low-poly geometry into a GPU-backed QPainter surface.

## Build and install from a clone

```sh
bin/build      # build/omawing
bin/test       # Qt Test, offscreen; audio checks run silently
bin/benchmark  # GPU rendering benchmark at 1080p and 4K
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

## Limits

Collisions use simple ship-centred bounds rather than exact wing geometry.
Faces are sorted by average depth, so intersecting geometry can occasionally
overlap wrongly. The high-score board is local to this computer.

