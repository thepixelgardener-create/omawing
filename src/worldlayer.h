#pragma once

// Everything standing in the world: scenery, rings, the ship, and particles,
// projected and drawn far to near (the painter's algorithm).

#include <QColor>
#include <QImage>
#include <cstdint>
#include <initializer_list>
#include <utility>
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
    // A solid, in its own frame. Each face's normal is worked out once, here,
    // pointing away from the middle; drawing only turns it with the mesh.
    struct Mesh {
        Mesh(std::vector<Vec> vertices, std::vector<std::vector<int>> faces);
        std::vector<Vec> vertices;
        std::vector<std::vector<int>> faces;
        std::vector<Vec> normals;
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

    // Clips `points` (camera space) in place and queues what is left.
    void face(std::vector<Vec> &points, const QColor &fill, const QColor &stroke, double opacity, bool ship,
              bool glow = false);
    // Places a mesh in the world and queues its faces; on Neon Mirage's
    // mirror floor, its reflection too.
    void mesh(const Mesh &geometry, const Vec &at, const Vec &rotation, const std::vector<Vec> &colors,
              const MeshOptions &options);
    // Queues the faces of the mesh placed in m_vertices, turned by the
    // rotation axes rx, ry, rz.
    void meshFaces(const Mesh &geometry, const std::array<Vec, 3> &axes, const std::vector<Vec> &colors,
                   const MeshOptions &options, double opacity);
    // A short list of colours in reusable storage, for passing to mesh().
    const std::vector<Vec> &colors(std::initializer_list<Vec> list) {
        m_colors.assign(list);
        return m_colors;
    }
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
    // Working space reused every frame, so building a frame barely allocates:
    // a mesh's world and camera-space vertices, one face's points, clipping
    // scratch, colours, and the drawing order by depth.
    std::vector<Vec> m_vertices, m_projected, m_points, m_scratch, m_colors, m_sphereColors;
    std::vector<std::pair<double, std::uint32_t>> m_order;
};
