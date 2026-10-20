#pragma once

#include <QColor>
#include <QHash>
#include <QSizeF>
#include <QString>
#include <array>
#include <vector>

#include "overlay.h"
#include "view.h"
#include "worldlayer.h"

class QPainter;

// Draws one frame of a Flight, layer by layer: Sky, Ground, the world
// (scenery, ship, particles), and the Overlay. Everything is laid out on a
// virtual canvas at least 540 units tall and 860 wide, in the target's aspect
// ratio, then scaled, so the picture fills any window: a wide window gets a
// wider view, a tall one more sky and ground.
class Scene {
public:
    using Vec = ::Vec;
    using Hud = SceneHud;
    using Look = SceneLook;
    using Face = SceneFace;

    static constexpr double BaseHeight = View::BaseHeight, MinWidth = View::MinWidth, Focal = View::Focal,
                            Near = View::Near, Far = View::Far;
    static constexpr int ThemeAct = View::ThemeAct;

    // Paints a frame and returns how many polygons and particles the world used.
    int paint(QPainter &painter, const Flight &flight, const QSizeF &size, const Hud &hud, const Look &look);
    int paint(QPainter &painter, const Flight &flight, const QSizeF &size, const Hud &hud) {
        return paint(painter, flight, size, hud, Look{});
    }

    // The projected world faces of the last frame, in canvas units; tests use
    // them to check that geometry stays finite and inside the view.
    const std::vector<Face> &faces() const { return m_world.faces(); }
    double width() const { return m_view.width; }
    double height() const { return m_view.height; }

    // The scene names are titles in the demo's artwork, like its scroller.
    // The theme's act takes the theme's name.
    static QString actName(int act, const QString &themeName = {});
    // The sky, ground, and highlights of the theme's act, from the theme's
    // named colours (colors.toml).
    static std::array<QColor, 8> themeColors(const QHash<QString, QString> &colors);

private:
    View m_view;
    WorldLayer m_world;
    Overlay m_overlay;
};
