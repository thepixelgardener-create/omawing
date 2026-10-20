#pragma once

#include <QColor>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QPainterPath>
#include <QPolygonF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <array>
#include <functional>
#include <vector>

#include "flight.h"

class QPainter;

// Draws one frame of a Flight: sky, ground, scenery, ship, and the overlay.
// Everything is laid out on a virtual canvas at least 540 units tall and 860
// wide, in the target's aspect ratio, then scaled, so the picture fills any
// window: a wide window gets a wider view, a tall one more sky and ground.
class Scene {
public:
    using Vec = ::Vec;

    struct Hud {
        QString status, mode, thrust, altitude, subtitle;
        QString hull, score, points, best, alert;
    };

    // How the app wants the frame dressed: the scroller on or off, and the
    // colours of the act that follows the Omarchy theme.
    struct Look {
        bool banner = true;
        QString themeName;
        std::array<QColor, 8> theme;   // Invalid colours fall back to built-in ones.
    };

    struct Face {
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

    static constexpr double BaseHeight = 540, MinWidth = 860, Focal = 570, Near = 0.3, Far = 100;
    // The act whose colours come from the Omarchy theme.
    static constexpr int ThemeAct = 3;

    // Paints a frame and returns how many polygons and particles the world used.
    int paint(QPainter &painter, const Flight &flight, const QSizeF &size, const Hud &hud, const Look &look);
    int paint(QPainter &painter, const Flight &flight, const QSizeF &size, const Hud &hud) {
        return paint(painter, flight, size, hud, Look{});
    }

    // The projected world faces of the last frame, in virtual units; tests use
    // them to check that geometry stays finite and inside the view.
    const std::vector<Face> &faces() const { return m_faces; }
    double width() const { return m_width; }
    double height() const { return m_height; }

    // The scene names are titles in the demo's artwork, like its scroller.
    // The theme's act takes the theme's name.
    static QString actName(int act, const QString &themeName = {});
    // The sky, ground, and highlights of the theme's act, from the theme's
    // named colours (colors.toml).
    static std::array<QColor, 8> themeColors(const QHash<QString, QString> &colors);

private:
    struct Palette {
        Vec top, mid, horizon, floor, tileA, tileB, accent, crystal;
    };
    struct Mesh {
        std::vector<Vec> vertices;
        std::vector<std::vector<int>> faces;
    };
    struct MeshOptions {
        bool wire = false, emissive = false, ship = false, glow = false;
        QColor stroke;
        double radius = 8, opacity = 1;
    };

    Palette paletteOf(int act) const;
    void begin(const Flight &flight, const QSizeF &size, const Look &look);
    Vec toCamera(const Vec &v) const;
    QPointF project(const Vec &p) const;
    std::vector<Vec> clip(std::vector<Vec> polygon) const;
    QPolygonF polygonOf(const std::vector<Vec> &points) const;
    double fog(double depth) const;
    void face(const std::vector<Vec> &points, const QColor &fill, const QColor &stroke, double opacity, bool ship,
              bool glow = false);
    void mesh(const Mesh &geometry, const Vec &at, const Vec &rotation, const std::vector<Vec> &colors,
              const MeshOptions &options);
    void ring(const Ring &ring, double spin);

    void paintSky(QPainter &p);
    void paintBackdrop(QPainter &p, double horizon);
    void paintGround(QPainter &p);
    void buildScenery();
    void buildShip();
    void buildParticles();
    void paintWorld(QPainter &p);
    void paintOverlay(QPainter &p, const Hud &hud);
    // Text never changes shape, so they are drawn once per
    // window size into images and reused every frame.
    struct Sprite {
        QImage image;
        QPointF offset;   // Virtual units from the anchor to the image's corner.
        QRect source;     // The part of a shared atlas image, or empty for all of it.
    };
    void prepare(double pixelsPerUnit);
    Sprite render(const QRectF &bounds, const std::function<void(QPainter &)> &draw) const;
    void blit(QPainter &p, const Sprite &sprite, const QPointF &anchor) const;
    // Draws a sprite resized by `scale`, for text that moves in depth.
    void blitScaled(QPainter &p, const Sprite &sprite, const QPointF &anchor, double scale) const;
    const Sprite &glyph(QChar c);
    void prepareGlyphs();
    void preparePuffs();

    const Flight *m_flight = nullptr;
    ChaseCamera m_camera;   // The flight's camera, shaken after a hit.
    Look m_look;
    Palette m_palette;
    int m_act = 0;
    double m_width = 960, m_height = BaseHeight;
    Vec m_right{1, 0, 0}, m_up{0, 1, 0}, m_forward{0, 0, 1};
    std::vector<Face> m_faces;
    double m_pixelsPerUnit = 0, m_preparedWidth = 0, m_preparedHeight = 0;
    QHash<QChar, Sprite> m_glyphs;
    double m_glyphAdvance = 16;
    Sprite m_title;
    QImage m_scanlines;   // One period of the scanlines, a pixel wide, tiled over the view.
    std::vector<QImage> m_puffs;
};
