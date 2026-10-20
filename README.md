# Omawing

![Omawing flying over the Copper Coast](screenshot.png)

An Amiga-style vector flight demo for Omarchy. A low-poly ship flies over an
endless checkerboard under copper-bar skies, with a chase camera, a sine-wave
scroller, and an optional chip-style soundtrack. It plays a 72-second loop of
three scenes on autopilot; press any flight key to take the controls.

It is a native Qt Quick app: the flight, the 3D projection, and the synth are
C++, and the picture is painted straight into a GPU framebuffer. It began as
an SVG-in-the-browser demo; the renderer is a port of that code.

## Keys

| Key | Action |
| --- | --- |
| A D / ← → | Steer |
| W S / ↑ ↓ | Climb or descend |
| Shift | Boost |
| Drag | Steer with the mouse |
| Space | Pause or play |
| T | Take control or watch the demo |
| N | Next scene |
| M | Turn sound on or off |
| R | Restart from the beginning |
| F or F11 | Full screen on or off |
| ? | Show or hide shortcuts |
| Q | Quit |

The toolbar appears when the mouse moves and fades out after a few seconds.
Sound stays off until you turn it on; if there is no audio output, the window
says so.

## What it follows

- **Any window shape.** The canvas is at least 860 by 540 units and takes the
  window's aspect ratio, so a wide window sees more of the world and a tall
  one more sky and ground. Nothing is letterboxed.
- **The Omarchy theme.** The toolbar and shortcut overlay use the theme's
  accent and Hyprland's window rounding, and follow theme switches live. The
  demo's own palettes are part of its artwork and stay as they are.
- **The Omarchy language.** Every control and HUD line is translated into
  Omarchy's 50 languages and follows Setup > Language live. Right-to-left
  languages mirror the toolbar. The scene names and the scroller are the
  demo's artwork, like its title, and stay in English.

## Build and install

```sh
bin/build      # build/omawing
bin/test       # Qt Test, offscreen
bin/install    # makepkg -fsi: installs the binary, launcher entry, and icon
```

After `bin/install`, Omawing is in the app launcher (Super + Space).

`OMAWING_STATS=1 omawing` prints the frame rate and paint time once a second,
and whether painting runs on the GPU or the CPU.

## Source

| File | Job |
| --- | --- |
| `src/flight.{h,cpp}` | The ship, chase camera, autopilot, and act timing. No Qt types. |
| `src/scene.{h,cpp}` | Projection, clipping, painter's-algorithm sorting, and every layer of the picture. |
| `src/synth.{h,cpp}` | The chip loop as samples, played through Qt Multimedia. |
| `src/backend.{h,cpp}` | State and actions for QML: pause, autopilot, sound, input. |
| `src/flightview.{h,cpp}` | The Qt Quick item that paints the scene. |
| `src/Main.qml` | The window, shortcuts, toolbar, and shortcut overlay. |
| `src/theme.*`, `src/translator.*` | Omarchy accent, rounding, and language. |

## Limits

The ship flies through scenery; there is no collision. Faces are sorted by
average depth, so intersecting geometry can occasionally overlap wrongly.
