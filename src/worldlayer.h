#pragma once

// Everything standing in the world: scenery, rings, the ship, and particles,
// projected and drawn far to near (the painter's algorithm).

#include <QColor>
#include <QImage>
#include <vector>

#include "view.h"

class QPainter;

class WorldLayer {
public:
    void build(const View &view);
    void paint(QPainter &p);
    // The projected faces of the last frame, in canvas units.
    const std::vector<SceneFace> &faces() const { return m_faces; }

private:
    struct Mesh {
        std::vector<Vec> vertices;
        std::vector<std::vector<int>> faces;
    };
    struct MeshOptions {
        bool wire = false, emissive = false, ship = false, glow = false;
        QColor stroke;
        // lift brightens shading towards fully lit, 0 to 1, smoothly.
        double radius = 8, opacity = 1, scale = 1, lift = 0;
        // How far away it is still drawn. Ordinary scenery fades in before Far;
        // the far landmarks reach much further.
        double far = View::Far;
    };

    void face(const std::vector<Vec> &points, const QColor &fill, const QColor &stroke, double opacity, bool ship,
              bool glow = false);
    void mesh(const Mesh &geometry, const Vec &at, const Vec &rotation, const std::vector<Vec> &colors,
              const MeshOptions &options);
    void ring(const Ring &ring, double spin);
    void buildScenery();
    // A few huge, sparse monoliths and pyramids far out on the plane, for
    // the eye to measure the distance by.
    void buildLandmarks();
    void buildShip();
    void buildParticles();
    void preparePuffs();

    bool m_reflecting = false;
    const View *m_view = nullptr;
    std::vector<SceneFace> m_faces;
    std::vector<QImage> m_puffs;
};
