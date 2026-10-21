#pragma once

// What every layer of a frame shares: the camera, the projection onto the
// virtual canvas, the act's colours and style, and the look the app asked
// for. Scene fills one in per frame; Sky, Ground, WorldLayer, and Overlay
// draw from it.

#include <QColor>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <array>
#include <vector>

#include "flight.h"

// The HUD's text, translated by the app.
struct SceneHud {
    QString status, mode, thrust, altitude, subtitle;
    QString hull, score, points, best, alert;
    // The names of the three view switches (U, L, B).
    QString titlesSwitch, toolbarSwitch, bannerSwitch;
};

// How the app wants the frame dressed: the scroller on or off, and the
// colours of the act that follows the Omarchy theme.
struct SceneLook {
    bool banner = true;
    QString themeName;
    std::array<QColor, 8> theme;   // Invalid colours fall back to built-in ones.
    // The music: beat jumps to 1 on a kick and dies away; level is loudness.
    double beat = 0, level = 0;
    // How much of the titles at the top shows, 0 to 1, as they fade.
    double titles = 1;
    // The switch keys at the bottom right: how much they show, and what is on.
    double switches = 0;
    bool titlesOn = true, toolbarOn = true;
};

// One thing to draw in the world, in virtual units, sorted by depth.
struct SceneFace {
    QPolygonF polygon;
    QColor fill, stroke;   // An invalid colour means none.
    double opacity = 1, depth = 0;
    bool ship = false;
    // Glowing faces add their light to what is behind them.
    bool glow = false;
    // A particle: which soft sprite, and the box it fills. Its polygon is empty.
    int sprite = -1;
    QRectF box;
};

// Colour helpers shared by the layers. Colours are mixed as Vecs of 0-255
// channels, then turned into QColors.
namespace draw {
Vec hex(const char *code);
Vec vecOf(const QColor &c);
QColor rgb(const Vec &v);
QColor withAlpha(const char *code, double alpha);
QColor withAlpha(const QColor &color, double alpha);
// The direction light falls from, for shading solid faces.
const Vec &light();
}

class View {
public:
    // The canvas is at least BaseHeight tall and MinWidth wide; Focal is the
    // projection's focal length in canvas units, and the world is drawn
    // between Near and Far.
    static constexpr double BaseHeight = 540, MinWidth = 860, Focal = 570, Near = 0.3, Far = 100;
    // The act whose colours come from the Omarchy theme.
    static constexpr int ThemeAct = 3;

    struct Palette {
        Vec top, mid, horizon, floor, tileA, tileB, accent, crystal;
    };
    // Each act has its own colours and its own mix of solid and wireframe.
    struct Act {
        const char *name;
        std::array<const char *, 8> colors;   // top, mid, horizon, floor, tileA, tileB, accent, crystal
        bool wirePyramids, wireCrystals, wireBall, grid, dimSun, spinRings, floater;
    };
    static const Act &actAt(int act);

    // Sets up the frame: canvas size, colours blended between acts, and the
    // camera, shaken after a hit.
    void begin(const Flight &flight, const QSizeF &size, const SceneLook &look);

    const Flight *flight = nullptr;
    ChaseCamera camera;
    SceneLook look;
    Palette palette;
    int act = 0;
    double width = 960, height = BaseHeight;
    Vec right{1, 0, 0}, up{0, 1, 0}, forward{0, 0, 1};

    const Act &style() const { return actAt(act); }
    Vec toCamera(const Vec &v) const;
    QPointF project(const Vec &p) const;
    // Clips against the near plane and the four sides of the view.
    std::vector<Vec> clip(std::vector<Vec> polygon) const;
    QPolygonF polygonOf(const std::vector<Vec> &points) const;
    // Aerial perspective: how far a thing at this depth has drifted towards
    // the colour of the air, 0 up close and at most a third at Far. A tint,
    // not a fog: the distance stays clear.
    double fog(double depth) const;
    // Scenery fades in over the last stretch before Far instead of popping.
    double fadeIn(double depth) const;
    // The checkerboard's contrast at a depth: 1 while a tile is large on
    // screen, falling to 0 as it shrinks to a fraction of a unit, where its
    // light and dark squares blend into their average. Mipmapping by hand:
    // the far plane converges smoothly instead of shimmering.
    double contrast(double depth) const;
    // The floor's light and dark squares at a depth, contrast and air applied.
    Vec floorLight(double depth) const;
    Vec floorDark(double depth) const;
    // How far the checkerboard reaches: where its tiles stop showing.
    double floorReach() const;
    static constexpr double Tile = 12;
    // Shared atmospheric light keeps scenery and the ground joined at infinity.
    Vec atmosphere() const;
    double valley = 0; // Neon Mirage treatment, blended at scene boundaries.

private:
    Palette paletteOf(int act) const;
};
