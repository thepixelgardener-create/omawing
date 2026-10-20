#pragma once

#include <QColor>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QPainterPath>
#include <QPolygonF>
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
    using Vec = std::array<double, 3>;

    struct Hud {
        QString status, mode, thrust, altitude, subtitle;
    };

    struct Face {
        QPolygonF polygon;
        QColor fill, stroke;   // An invalid colour means none.
        double opacity = 1, depth = 0;
        bool ship = false;
    };

    static constexpr double BaseHeight = 540, MinWidth = 860, Focal = 570, Near = 0.3, Far = 100;

    // Paints a frame and returns how many polygons the world used.
    int paint(QPainter &painter, const Flight &flight, const QSizeF &size, const Hud &hud);

    // The projected world faces of the last frame, in virtual units; tests use
    // them to check that geometry stays finite and inside the view.
    const std::vector<Face> &faces() const { return m_faces; }
    double width() const { return m_width; }
    double height() const { return m_height; }

    // The scene names are titles in the demo's artwork, like its scroller.
    static QString actName(int act);

private:
    struct Palette {
        Vec top, mid, horizon, floor, tileA, tileB, accent, crystal;
    };
    struct Mesh {
        std::vector<Vec> vertices;
        std::vector<std::vector<int>> faces;
    };
    struct MeshOptions {
        bool wire = false, emissive = false, ship = false;
        QColor stroke;
        double radius = 8, opacity = 1;
    };

    void begin(const Flight &flight, const QSizeF &size);
    Vec toCamera(const Vec &v) const;
    QPointF project(const Vec &p) const;
    std::vector<Vec> clip(std::vector<Vec> polygon) const;
    QPolygonF polygonOf(const std::vector<Vec> &points) const;
    double fog(double depth) const;
    void face(const std::vector<Vec> &points, const QColor &fill, const QColor &stroke, double opacity, bool ship);
    void mesh(const Mesh &geometry, const Vec &at, const Vec &rotation, const std::vector<Vec> &colors,
              const MeshOptions &options);
    void ring(double x, double y, double z, double spin);

    void paintSky(QPainter &p);
    void paintGround(QPainter &p);
    void buildScenery();
    void buildShip();
    void paintWorld(QPainter &p);
    void paintOverlay(QPainter &p, const Hud &hud);
    // Text, glow, and scanlines never change shape, so they are drawn once
    // per window size into images and reused every frame.
    struct Sprite {
        QImage image;
        QPointF offset;   // Virtual units from the anchor to the image's corner.
    };
    void prepare(double pixelsPerUnit);
    Sprite render(const QRectF &bounds, const std::function<void(QPainter &)> &draw) const;
    void blit(QPainter &p, const Sprite &sprite, const QPointF &anchor) const;
    const Sprite &glyph(QChar c);

    const Flight *m_flight = nullptr;
    Palette m_palette;
    int m_act = 0;
    double m_width = 960, m_height = BaseHeight;
    Vec m_right{1, 0, 0}, m_up{0, 1, 0}, m_forward{0, 0, 1};
    std::vector<Face> m_faces;
    double m_pixelsPerUnit = 0, m_preparedWidth = 0, m_preparedHeight = 0;
    QHash<QChar, Sprite> m_glyphs;
    Sprite m_title, m_halo, m_scanlines;
};
