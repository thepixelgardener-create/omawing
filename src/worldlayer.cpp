#include "worldlayer.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <algorithm>
#include <cmath>

#include "shipshape.h"

using namespace geo;
using namespace draw;

namespace {
// The soft sprites particles are drawn with: the exhaust's cooling ramp,
// then a spark, two glitters, and smoke.
constexpr int ExhaustSprites = 8, SparkSprite = 8, GlitterSprite = 9, SmokeSprite = 11;
}

void WorldLayer::build(const View &view) {
    m_view = &view;
    m_faces.clear();
    preparePuffs();
    buildScenery();
    buildShip();
    buildParticles();
}

void WorldLayer::face(const std::vector<Vec> &points, const QColor &fill, const QColor &stroke, double opacity, bool ship,
                 bool glow) {
    const auto clipped = m_view->clip(points);
    if (clipped.size() < 3)
        return;
    double depth = 0;
    for (const Vec &p : clipped)
        depth += p[2];
    m_faces.push_back({m_view->polygonOf(clipped), fill, stroke, opacity, depth / clipped.size(), ship, glow});
}

void WorldLayer::mesh(const Mesh &geometry, const Vec &at, const Vec &rotation, const std::vector<Vec> &colors,
                 const MeshOptions &options) {
    const Vec centreCamera = m_view->toCamera(at);
    if (centreCamera[2] < -options.radius || centreCamera[2] > View::Far + options.radius ||
        std::abs(centreCamera[0]) > centreCamera[2] * m_view->width / (2 * View::Focal) + options.radius * 2)
        return;
    std::vector<Vec> vertices, projected;
    vertices.reserve(geometry.vertices.size());
    projected.reserve(geometry.vertices.size());
    // Calculate the rotation once per mesh, rather than six trig functions
    // per vertex (especially costly on the bouncing sphere).
    const Vec rx = rotate({1, 0, 0}, rotation), ry = rotate({0, 1, 0}, rotation), rz = rotate({0, 0, 1}, rotation);
    Vec centre{0, 0, 0};
    for (const Vec &v : geometry.vertices) {
        vertices.push_back({at[0] + rx[0]*v[0] + ry[0]*v[1] + rz[0]*v[2],
                            at[1] + rx[1]*v[0] + ry[1]*v[1] + rz[1]*v[2],
                            at[2] + rx[2]*v[0] + ry[2]*v[1] + rz[2]*v[2]});
        centre = translated(centre, scaled(vertices.back(), 1.0 / geometry.vertices.size()));
    }
    for (const Vec &v : vertices)
        projected.push_back(m_view->toCamera(v));
    for (size_t i = 0; i < geometry.faces.size(); ++i) {
        const auto &indices = geometry.faces[i];
        const Vec &v0 = vertices[indices[0]];
        Vec n = normalized(cross(sub(vertices[indices[1]], v0), sub(vertices[indices[2]], v0)));
        if (dot(n, sub(v0, centre)) < 0)
            n = scaled(n, -1);
        // Solid faces turned away from the camera are hidden; wireframes show all edges.
        if (!options.wire && dot(n, sub(v0, {m_view->camera.x, m_view->camera.y, m_view->camera.z})) >= 0)
            continue;
        std::vector<Vec> points;
        points.reserve(indices.size());
        double depth = 0;
        for (int index : indices) {
            points.push_back(projected[index]);
            depth += projected[index][2];
        }
        depth /= indices.size();
        const Vec &source = colors[i % colors.size()];
        const double brightness = options.emissive ? 1 : 0.5 + 0.5 * std::max(0.0, dot(n, light()));
        const QColor fill = options.wire ? QColor() : rgb(blend(scaled(source, brightness), m_view->palette.floor, m_view->fog(depth)));
        const QColor stroke = options.wire ? rgb(blend(source, m_view->palette.floor, m_view->fog(depth)))
                            : options.glow ? QColor()
                            : options.stroke.isValid() ? options.stroke : fill;
        face(points, fill, stroke, options.opacity, options.ship, options.glow);
    }
}

void WorldLayer::ring(const Ring &r, double spin) {
    const int count = 12;
    const double radius = World::RingRadius, thickness = 0.18;
    // Dissolve a gate as it nears the camera, before it sweeps through and
    // covers the entire view.
    const double opacity = smooth(clampTo((m_view->toCamera({r.x, r.y, r.z})[2] - 13.5) / 7, 0, 1));
    if (opacity <= 0)
        return;
    for (int i = 0; i < count; ++i) {
        const double a = i * 2 * M_PI / count + spin, b = (i + 1) * 2 * M_PI / count + spin, inner = radius - thickness;
        const std::vector<Vec> points{m_view->toCamera({r.x + std::cos(a) * radius, r.y + std::sin(a) * radius, r.z}),
                                      m_view->toCamera({r.x + std::cos(b) * radius, r.y + std::sin(b) * radius, r.z}),
                                      m_view->toCamera({r.x + std::cos(b) * inner, r.y + std::sin(b) * inner, r.z}),
                                      m_view->toCamera({r.x + std::cos(a) * inner, r.y + std::sin(a) * inner, r.z})};
        double depth = 0;
        for (const Vec &p : points)
            depth += p[2] / 4;
        face(points, rgb(blend(i % 3 ? m_view->palette.accent : m_view->palette.crystal, m_view->palette.floor, m_view->fog(depth))), QColor(),
             opacity, false);
    }
}

void WorldLayer::buildScenery() {
    static const Mesh pyramid{{{-5, 0, -5}, {5, 0, -5}, {5, 0, 5}, {-5, 0, 5}, {0, 8, 0}},
                              {{0, 1, 4}, {1, 2, 4}, {2, 3, 4}, {3, 0, 4}}};
    static const Mesh crystal{{{0, 4, 0}, {-1.5, 0, 0}, {0, 0, 1.5}, {1.5, 0, 0}, {0, 0, -1.5}, {0, -1, 0}},
                              {{0, 1, 2}, {0, 2, 3}, {0, 3, 4}, {0, 4, 1}, {5, 2, 1}, {5, 3, 2}, {5, 4, 3}, {5, 1, 4}}};
    static const auto [sphere, ballColors] = [] {
        Mesh m;
        std::vector<Vec> colors;
        for (int j = 0; j <= 8; ++j)
            for (int i = 0; i < 16; ++i)
                m.vertices.push_back({3.2 * std::sin(j / 8.0 * M_PI) * std::cos(i / 16.0 * M_PI * 2), 3.2 * std::cos(j / 8.0 * M_PI),
                                      3.2 * std::sin(j / 8.0 * M_PI) * std::sin(i / 16.0 * M_PI * 2)});
        for (int j = 0; j < 8; ++j)
            for (int i = 0; i < 16; ++i) {
                const int a = j * 16 + i, b = j * 16 + (i + 1) % 16, c = (j + 1) * 16 + (i + 1) % 16, d = (j + 1) * 16 + i;
                m.faces.push_back(j == 0 ? std::vector<int>{a, c, d} : j == 7 ? std::vector<int>{a, b, c} : std::vector<int>{a, b, c, d});
                colors.push_back(hex((i + j) % 2 ? "#f7e6c7" : "#db4c70"));
            }
        return std::pair{m, colors};
    }();

    const View::Act &act = m_view->style();
    const double time = m_view->flight->time;
    for (const Obstacle &o : World::scenery(m_view->camera.x, m_view->camera.z, 4, time)) {
        if (std::hypot(o.x - m_view->camera.x, o.z - m_view->camera.z) > View::Far)
            continue;
        if (o.kind == Obstacle::Pyramid)
            // Wireframes are drawn in the bright highlights: the sky's dark middle
            // colour that shades solid faces vanishes against the ground.
            mesh(pyramid, {o.x, o.y, o.z}, {0, 0, 0},
                 act.wirePyramids ? std::vector<Vec>{m_view->palette.accent, m_view->palette.crystal} : std::vector<Vec>{m_view->palette.mid, m_view->palette.crystal},
                 {.wire = act.wirePyramids, .radius = 9});
        else
            mesh(crystal, {o.x, o.y, o.z}, {o.spin, 0, 0}, {m_view->palette.crystal, m_view->palette.accent},
                 {.wire = act.wireCrystals, .radius = 6});
    }
    // Rings already flown through are gone; the rest stand along the course
    // in both directions.
    const int first = int(std::floor((m_view->camera.z - View::Far - 24) / World::RingSpacing));
    const int last = int(std::ceil((m_view->camera.z + View::Far - 24) / World::RingSpacing));
    for (int i = first; i <= last; ++i) {
        const Ring r = World::ring(i);
        if (!m_view->flight->game.collected.count(i) && std::hypot(r.x - m_view->camera.x, r.z - m_view->camera.z) < View::Far)
            ring(r, act.spinRings ? time * 0.15 : 0);
    }
    const Obstacle ball = World::ball(m_view->flight->ship.z, time);
    mesh(sphere, {ball.x, ball.y, ball.z}, {ball.spin, 0, 0.25}, ballColors, {.wire = act.wireBall, .radius = 5});
    if (act.floater)
        mesh(crystal, {-12, 8 + std::sin(time), m_view->flight->ship.z + 45}, {time * 0.7, time * 0.3, 0}, {m_view->palette.crystal},
             {.wire = true, .radius = 6});
}

void WorldLayer::buildShip() {
    static const Mesh hull{ShipShape::HullVertices, ShipShape::HullFaces};
    static const Mesh canopy{{{0, 0.13, 1.65}, {-0.34, 0.29, -0.2}, {0.34, 0.29, -0.2}, {0, 0.76, -0.35}},
                             {{0, 1, 3}, {0, 3, 2}, {1, 2, 3}}};
    static const Mesh flame{{{-0.28, 0.22, -1.6}, {0.28, 0.22, -1.6}, {0.28, -0.2, -1.6}, {-0.28, -0.2, -1.6}, {0, 0, -3.6}},
                            {{0, 1, 4}, {1, 2, 4}, {2, 3, 4}, {3, 0, 4}}};

    const auto &ship = m_view->flight->ship;
    const Vec at{ship.x, ship.y, ship.z}, rotation{ship.yaw, ship.pitch, ship.bank};
    // After a hit the ship flickers until it can be hurt again.
    const double opacity = m_view->flight->game.hurt > 0 && std::fmod(m_view->flight->game.hurt, 0.2) < 0.1 ? 0.35 : 1;
    mesh(hull, at, rotation, {hex("#e8e8dc"), hex("#92a9b7"), hex("#59638d")},
         {.ship = true, .stroke = QColor("#dfd6cd"), .radius = 5, .opacity = opacity});

    auto place = [&](const Vec &v) { return m_view->toCamera(translated(rotate(v, rotation), at)); };
    for (const double side : {-1.0, 1.0}) {
        const auto panels = ShipShape::wingPanels(ship, side);
        const char *const colors[] = {"#e4b3ab", "#eec3b6"};
        for (int i = 0; i < 2; ++i) {
            const auto &panel = panels[i];
            // The wings are thin and double-sided, lit from whichever side faces the light.
            const Vec n = normalized(cross(sub(rotate(panel[1], rotation), rotate(panel[0], rotation)),
                                           sub(rotate(panel[2], rotation), rotate(panel[0], rotation))));
            std::vector<Vec> points;
            for (const Vec &v : panel)
                points.push_back(place(v));
            face(points, rgb(scaled(hex(colors[i]), 0.7 + 0.3 * std::abs(dot(n, light())))), QColor("#f8dfc1"), opacity, true);
        }
        face({place({0.45 * side, 0.1, -1.45}), place({0.75 * side, 1, -1.9}), place({0.7 * side, 0.1, -0.65})},
             QColor("#cf557a"), QColor("#f8dfc1"), opacity, true);
    }
    mesh(canopy, at, rotation, {hex("#7de6e1"), hex("#398da8"), hex("#286285")},
         {.emissive = true, .ship = true, .stroke = QColor("#baf8e6"), .radius = 5, .opacity = opacity});

    // The flames are see-through light: a long cone, and a short white-hot
    // core inside it. The particles do the rest.
    if (m_view->flight->game.wrecked >= 0)
        return;
    const double thrust = 1 + (ship.speed - 13) / 18, flicker = std::sin(m_view->flight->time * 28);
    Mesh outer = flame, core = flame;
    outer.vertices[4][2] = -2.4 - thrust * (0.8 + 0.2 * flicker);
    for (int i = 0; i < 4; ++i)
        core.vertices[i] = {core.vertices[i][0] * 0.55, core.vertices[i][1] * 0.55, -1.6};
    core.vertices[4][2] = -2 - thrust * 0.35 * (1 + 0.15 * flicker);
    for (double offset : {-0.65, 0.65}) {
        const Vec nozzle = translated(rotate({offset, -0.05, 0}, rotation), at);
        mesh(outer, nozzle, rotation, {hex("#ff9a5c"), hex("#ffc27a"), hex("#f7689a")},
             {.emissive = true, .glow = true, .radius = 5, .opacity = 0.42});
        mesh(core, nozzle, rotation, {hex("#fff6d8")}, {.emissive = true, .glow = true, .radius = 5, .opacity = 0.75});
    }
}

void WorldLayer::buildParticles() {
    for (const Particle &particle : m_view->flight->particles) {
        const Vec c = m_view->toCamera(particle.position);
        if (c[2] < View::Near + 0.4 || c[2] > View::Far)
            continue;
        const double t = clampTo(particle.age / particle.life, 0, 1);
        double size = particle.size, opacity = 1;
        int sprite = 0;
        bool glow = true;
        switch (particle.kind) {
        case Particle::Exhaust:
            // White hot at the nozzle, cooling through orange to pink.
            size *= 0.6 + 1.7 * t;
            opacity = 0.85 * std::pow(1 - t, 1.4);
            sprite = std::min(ExhaustSprites - 1, int(t * ExhaustSprites));
            break;
        case Particle::Spark:
            opacity = 1 - t * t;
            sprite = SparkSprite;
            break;
        case Particle::Glitter:
            size *= 1 + 0.5 * std::sin(particle.age * 31 + particle.position[0]);
            opacity = (1 - t) * (0.6 + 0.4 * std::sin(particle.age * 23 + particle.position[1] * 3));
            sprite = GlitterSprite + (int(std::abs(particle.position[0] * 7)) % 2);
            break;
        case Particle::Smoke:
            size *= 1 + 2.2 * t;
            opacity = 0.55 * (1 - t);
            sprite = SmokeSprite;
            glow = false;
            break;
        }
        // Fade what comes right up to the lens instead of filling the view with it.
        opacity *= clampTo((c[2] - View::Near - 0.4) / 2, 0, 1) * (1 - m_view->fog(c[2]));
        const QPointF at = m_view->project(c);
        const double r = std::min(90.0, View::Focal * size / c[2]);
        if (opacity <= 0.01 || at.x() + r < 0 || at.x() - r > m_view->width || at.y() + r < 0 || at.y() - r > m_view->height)
            continue;
        SceneFace f;
        f.opacity = opacity;
        f.depth = c[2];
        f.glow = glow;
        f.sprite = sprite;
        f.box = QRectF(at.x() - r, at.y() - r, r * 2, r * 2);
        m_faces.push_back(std::move(f));
    }
}

void WorldLayer::preparePuffs() {
    if (!m_puffs.empty())
        return;
    auto puff = [](const QColor &core, const QColor &edge, bool star = false) {
        QImage image(64, 64, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        QRadialGradient g(32, 32, 32);
        g.setColorAt(0, core);
        g.setColorAt(0.3, withAlpha(edge, 0.6));
        g.setColorAt(1, withAlpha(edge, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(QRectF(0, 0, 64, 64));
        if (star) {
            // A four-pointed glint.
            QPainterPath glint;
            glint.moveTo(32, 0);
            glint.quadTo(32, 32, 64, 32);
            glint.quadTo(32, 32, 32, 64);
            glint.quadTo(32, 32, 0, 32);
            glint.quadTo(32, 32, 32, 0);
            p.fillPath(glint, core);
        }
        return image;
    };
    const QColor ramp[] = {QColor("#fff8dc"), QColor("#ffd27a"), QColor("#ff9150"), QColor("#f2577f"), QColor("#7a3a8f")};
    for (int i = 0; i < ExhaustSprites; ++i) {
        const double t = i / double(ExhaustSprites - 1) * 4;
        const int a = std::min(3, int(t));
        const QColor c = rgb(blend(vecOf(ramp[a]), vecOf(ramp[a + 1]), t - a));
        m_puffs.push_back(puff(i < 2 ? QColor("#ffffff") : c, c));
    }
    m_puffs.push_back(puff(QColor("#ffffff"), QColor("#ffb347")));
    m_puffs.push_back(puff(QColor("#fff8e0"), QColor("#ffd36b"), true));
    m_puffs.push_back(puff(QColor("#f0fdff"), QColor("#7fe3ff"), true));
    m_puffs.push_back(puff(withAlpha("#3b3346", 0.9), QColor("#5a5266")));
}

// Painter's algorithm: far faces first, near faces over them.
void WorldLayer::paint(QPainter &p) {
    std::stable_sort(m_faces.begin(), m_faces.end(), [](const SceneFace &a, const SceneFace &b) { return a.depth > b.depth; });
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    for (const SceneFace &f : m_faces) {
        p.setOpacity(f.opacity);
        p.setCompositionMode(f.glow ? QPainter::CompositionMode_Plus : QPainter::CompositionMode_SourceOver);
        if (f.sprite >= 0) {
            p.drawImage(f.box, m_puffs[f.sprite]);
            continue;
        }
        p.setBrush(f.fill.isValid() ? QBrush(f.fill) : QBrush(Qt::NoBrush));
        // A one-pixel cosmetic pen takes the rasterizer's fast path.
        QPen pen(f.stroke, 0);
        pen.setCosmetic(true);
        p.setPen(f.stroke.isValid() ? pen : QPen(Qt::NoPen));
        p.drawConvexPolygon(f.polygon);
    }
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.setOpacity(1);
}
