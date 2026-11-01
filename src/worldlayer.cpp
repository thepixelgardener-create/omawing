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
constexpr int ExhaustSprites = 8, SparkSprite = 8, GlitterSprite = 9, SmokeSprite = 11, FlashSprite = 12;
}

WorldLayer::Mesh::Mesh(std::vector<Vec> v, std::vector<std::vector<int>> f) : vertices(std::move(v)), faces(std::move(f)) {
    Vec centre{0, 0, 0};
    for (const Vec &p : vertices)
        centre = translated(centre, scaled(p, 1.0 / vertices.size()));
    normals.reserve(faces.size());
    for (const auto &indices : faces) {
        const Vec &v0 = vertices[indices[0]];
        Vec n = normalized(cross(sub(vertices[indices[1]], v0), sub(vertices[indices[2]], v0)));
        // Rotating, mirroring, or scaling the mesh evenly keeps a normal
        // pointing away from the middle, so it only needs turning to match.
        if (dot(n, sub(v0, centre)) < 0)
            n = scaled(n, -1);
        normals.push_back(n);
    }
}

void WorldLayer::build(const View &view) {
    m_view = &view;
    m_faces.clear();
    preparePuffs();
    buildLandmarks();
    buildScenery();
    buildShip();
    buildParticles();
}

void WorldLayer::face(std::vector<Vec> &points, const QColor &fill, const QColor &stroke, double opacity, bool ship,
                 bool glow) {
    if (m_reflecting) opacity *= 0.24 * m_view->valley;
    m_view->clipInPlace(points, m_scratch);
    if (points.size() < 3)
        return;
    double depth = 0;
    for (const Vec &p : points)
        depth += p[2];
    m_faces.push_back({m_view->polygonOf(points), fill, stroke, opacity, depth / points.size(), ship, glow});
}

void WorldLayer::mesh(const Mesh &geometry, const Vec &at, const Vec &rotation, const std::vector<Vec> &colors,
                 const MeshOptions &options) {
    const double away = std::hypot(at[0] - m_view->camera.x, at[2] - m_view->camera.z);
    const double fade = options.far > View::Far || options.ship || options.glow ? 1 : m_view->fadeIn(away);
    if (options.opacity * fade <= 0) return;
    const Vec centreCamera = m_view->toCamera(at);
    if (centreCamera[2] < -options.radius || centreCamera[2] > options.far + options.radius ||
        std::abs(centreCamera[0]) > centreCamera[2] * m_view->width / (2 * View::Focal) + options.radius * 2)
        return;
    // Calculate the rotation once per mesh, rather than six trig functions
    // per vertex (especially costly on the bouncing sphere).
    const std::array<Vec, 3> axes{rotate({1, 0, 0}, rotation), rotate({0, 1, 0}, rotation), rotate({0, 0, 1}, rotation)};
    const Vec &rx = axes[0], &ry = axes[1], &rz = axes[2];
    m_vertices.clear();
    for (const Vec &unscaled : geometry.vertices) {
        const Vec v = scaled(unscaled, options.scale);
        m_vertices.push_back({at[0] + rx[0]*v[0] + ry[0]*v[1] + rz[0]*v[2],
                              at[1] + rx[1]*v[0] + ry[1]*v[1] + rz[1]*v[2],
                              at[2] + rx[2]*v[0] + ry[2]*v[1] + rz[2]*v[2]});
    }
    meshFaces(geometry, axes, colors, options, options.opacity * fade);
    // Neon Mirage's floor mirrors the scenery: the same mesh, flipped below
    // the ground and faint.
    if (m_view->valley > 0 && !options.ship && !options.glow) {
        for (Vec &v : m_vertices)
            v[1] = -v[1];
        m_reflecting = true;
        meshFaces(geometry, axes, colors, options, options.opacity * fade);
        m_reflecting = false;
    }
}

void WorldLayer::meshFaces(const Mesh &geometry, const std::array<Vec, 3> &axes, const std::vector<Vec> &colors,
                           const MeshOptions &options, double opacity) {
    m_projected.clear();
    for (const Vec &v : m_vertices)
        m_projected.push_back(m_view->toCamera(v));
    const Vec eye{m_view->camera.x, m_view->camera.y, m_view->camera.z};
    for (size_t i = 0; i < geometry.faces.size(); ++i) {
        const auto &indices = geometry.faces[i];
        const Vec &local = geometry.normals[i];
        Vec n{axes[0][0] * local[0] + axes[1][0] * local[1] + axes[2][0] * local[2],
              axes[0][1] * local[0] + axes[1][1] * local[1] + axes[2][1] * local[2],
              axes[0][2] * local[0] + axes[1][2] * local[1] + axes[2][2] * local[2]};
        if (m_reflecting) n[1] = -n[1];
        // Solid faces turned away from the camera are hidden; wireframes show all edges.
        if (!options.wire && dot(n, sub(m_vertices[indices[0]], eye)) >= 0)
            continue;
        m_points.clear();
        double depth = 0;
        for (int index : indices) {
            m_points.push_back(m_projected[index]);
            depth += m_projected[index][2];
        }
        depth /= indices.size();
        const Vec &source = colors[i % colors.size()];
        const double brightness = options.emissive ? 1 : mix(0.5 + 0.5 * std::max(0.0, dot(n, light())), 1, options.lift);
        const double haze = options.ship || options.glow ? 0 : m_view->fog(depth);
        const QColor fill = options.wire ? QColor() : rgb(blend(scaled(source, brightness), m_view->atmosphere(), haze));
        const QColor stroke = options.wire ? rgb(blend(source, m_view->atmosphere(), haze))
                            : options.glow ? QColor()
                            : options.stroke.isValid() ? options.stroke : fill;
        face(m_points, fill, stroke, opacity, options.ship, options.glow);
    }
}

void WorldLayer::ring(const Ring &r, double spin) {
    auto point = [&](const Vec &v) { Vec q = v; if (m_reflecting) q[1] = -q[1]; return m_view->toCamera(q); };
    const int count = 12;
    const double radius = World::RingRadius, thickness = 0.18;
    // Dissolve a gate as it nears the camera, before it sweeps through and
    // covers the entire view.
    const double opacity = smooth(clampTo((m_view->toCamera({r.x, r.y, r.z})[2] - 13.5) / 7, 0, 1))
                         * m_view->fadeIn(std::hypot(r.x - m_view->camera.x, r.z - m_view->camera.z));
    if (opacity <= 0)
        return;
    for (int i = 0; i < count; ++i) {
        const double a = i * 2 * M_PI / count + spin, b = (i + 1) * 2 * M_PI / count + spin, inner = radius - thickness;
        m_points.assign({point({r.x + std::cos(a) * radius, r.y + std::sin(a) * radius, r.z}),
                         point({r.x + std::cos(b) * radius, r.y + std::sin(b) * radius, r.z}),
                         point({r.x + std::cos(b) * inner, r.y + std::sin(b) * inner, r.z}),
                         point({r.x + std::cos(a) * inner, r.y + std::sin(a) * inner, r.z})});
        double depth = 0;
        for (const Vec &p : m_points)
            depth += p[2] / 4;
        face(m_points, rgb(blend(i % 3 ? m_view->palette.accent : m_view->palette.crystal, m_view->atmosphere(), m_view->fog(depth))), QColor(),
             opacity, false);
    }
    if (!m_reflecting && m_view->valley > 0) {
        m_reflecting = true;
        ring(r, spin);
        m_reflecting = false;
    }
}

void WorldLayer::buildLandmarks() {
    // At size 1: a great pyramid, and a tall monolith with a pointed cap.
    static const Mesh greatPyramid{{{-22, 0, -22}, {22, 0, -22}, {22, 0, 22}, {-22, 0, 22}, {0, 70, 0}},
                                   {{0, 1, 4}, {1, 2, 4}, {2, 3, 4}, {3, 0, 4}}};
    static const Mesh monolith = [] {
        const double half = 5, top = 0.72 * half, height = 85;
        return Mesh{{{-half, 0, -half}, {half, 0, -half}, {half, 0, half}, {-half, 0, half},
                     {-top, height * 0.9, -top}, {top, height * 0.9, -top}, {top, height * 0.9, top}, {-top, height * 0.9, top},
                     {0, height, 0}},
                    {{0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}, {4, 5, 8}, {5, 6, 8}, {6, 7, 8}, {7, 4, 8}}};
    }();
    constexpr double Cell = 320, Nearest = 320, Farthest = 1900;
    const Vec &mid = m_view->palette.mid, &top = m_view->palette.top;
    // Dark, cool silhouettes, a touch lighter than the night sky behind the
    // hills; aerial perspective lifts the farthest towards the air.
    const Vec body = blend(blend(mid, top, 0.45), m_view->palette.tileB, 0.3);
    const Vec edge = blend(body, m_view->palette.accent, 0.18);
    const int cx = int(std::floor(m_view->camera.x / Cell)), cz = int(std::floor(m_view->camera.z / Cell));
    const int reach = int(std::ceil(Farthest / Cell));
    for (int iz = cz - reach; iz <= cz + reach; ++iz)
        for (int ix = cx - reach; ix <= cx + reach; ++ix) {
            // Sparse: fewer than half the cells hold one.
            if (hash(ix * 3.1 + 500, iz * 7.7) < 0.66)
                continue;
            const double x = (ix + 0.5 + (hash(ix, iz + 900) - 0.5) * 0.7) * Cell;
            const double z = (iz + 0.5 + (hash(ix + 900, iz) - 0.5) * 0.7) * Cell;
            const double away = std::hypot(x - m_view->camera.x, z - m_view->camera.z);
            if (away < Nearest || away > Farthest || std::abs(x) < 140)
                continue;
            const double shown = smooth(clampTo((away - Nearest) / 160, 0, 1))
                               * smooth(clampTo((Farthest - away) / 320, 0, 1));
            const double size = 0.55 + hash(ix + 40, iz + 70) * 0.6;
            const double spin = hash(ix + 13, iz + 31) * M_PI;
            const Mesh &shape = hash(ix + 7, iz + 3) < 0.6 ? greatPyramid : monolith;
            // The further away, the more of the air's colour: the oldest trick
            // for distance, on top of their size shrinking.
            const double air = 0.22 + 0.5 * smooth(clampTo((away - Nearest) / (Farthest - Nearest), 0, 1));
            mesh(shape, {x, 0, z}, {spin, 0, 0}, colors({blend(body, m_view->atmosphere(), air), blend(edge, m_view->atmosphere(), air)}),
                 {.radius = 130 * size, .opacity = shown, .scale = size, .far = Farthest});
        }
}

void WorldLayer::buildScenery() {
    // The shapes match what the flight collides with (World's dimensions).
    using namespace World;
    static const Mesh pyramid{{{-PyramidHalf, 0, -PyramidHalf}, {PyramidHalf, 0, -PyramidHalf}, {PyramidHalf, 0, PyramidHalf},
                               {-PyramidHalf, 0, PyramidHalf}, {0, PyramidHeight, 0}},
                              {{0, 1, 4}, {1, 2, 4}, {2, 3, 4}, {3, 0, 4}}};
    static const Mesh crystal{{{0, CrystalHeight, 0}, {-CrystalHalf, 0, 0}, {0, 0, CrystalHalf}, {CrystalHalf, 0, 0},
                               {0, 0, -CrystalHalf}, {0, -CrystalDepth, 0}},
                              {{0, 1, 2}, {0, 2, 3}, {0, 3, 4}, {0, 4, 1}, {5, 2, 1}, {5, 3, 2}, {5, 4, 3}, {5, 1, 4}}};
    static const auto [sphere, ballColors] = [] {
        std::vector<Vec> vertices, colors;
        std::vector<std::vector<int>> faces;
        for (int j = 0; j <= 8; ++j)
            for (int i = 0; i < 16; ++i)
                vertices.push_back({BallRadius * std::sin(j / 8.0 * M_PI) * std::cos(i / 16.0 * M_PI * 2), BallRadius * std::cos(j / 8.0 * M_PI),
                                    BallRadius * std::sin(j / 8.0 * M_PI) * std::sin(i / 16.0 * M_PI * 2)});
        for (int j = 0; j < 8; ++j)
            for (int i = 0; i < 16; ++i) {
                const int a = j * 16 + i, b = j * 16 + (i + 1) % 16, c = (j + 1) * 16 + (i + 1) % 16, d = (j + 1) * 16 + i;
                faces.push_back(j == 0 ? std::vector<int>{a, c, d} : j == 7 ? std::vector<int>{a, b, c} : std::vector<int>{a, b, c, d});
                colors.push_back(hex((i + j) % 2 ? "#f7e6c7" : "#db4c70"));
            }
        return std::pair{Mesh{vertices, faces}, colors};
    }();
    static const Vec mirageLight = hex("#64547e"), mirageDark = hex("#333456");

    const View::Act &act = m_view->style();
    const double time = m_view->flight->time, beat = m_view->look.beat, level = m_view->look.level;
    // The scenery thins out with distance, like the floor's detail: each
    // piece has its own distance, between Sparse and Far, at which it
    // starts to fade in. Far off only a few stand; closer, all of them.
    constexpr double Sparse = 100, Fade = 40;
    const int reach = int(std::ceil(View::Far / Cell)) + 1;
    forEachScenery(m_view->camera.x, m_view->camera.z, reach, time, [&](const Obstacle &o) {
        const double away = std::hypot(o.x - m_view->camera.x, o.z - m_view->camera.z);
        if (away > View::Far)
            return;
        const double chance = hash(o.cellX * 1.7 + 31, o.cellZ * 2.3 + 77);
        const double appear = mix(Sparse, View::Far, chance * chance);
        if (away > appear)
            return;
        const double shown = smooth(clampTo((appear - away) / Fade, 0, 1));
        if (o.kind == Obstacle::Pyramid)
            // Wireframes are drawn in the bright highlights: the sky's dark middle
            // colour that shades solid faces vanishes against the ground.
            mesh(pyramid, {o.x, o.y, o.z}, {0, 0, 0},
                 colors({act.wirePyramids ? m_view->palette.accent : m_view->palette.mid, m_view->palette.crystal}),
                 {.wire = act.wirePyramids, .radius = 9, .opacity = shown});
        else
            // The crystals swell and brighten softly with the music's kicks,
            // and breathe a little with its loudness. Every change is gentle
            // and gradual: a glow, never a flash.
            mesh(crystal, {o.x, o.y, o.z}, {o.spin, 0, 0},
                 colors({blend(m_view->palette.crystal, {255, 255, 255}, 0.2 * beat), blend(m_view->palette.accent, {255, 255, 255}, 0.12 * beat)}),
                 {.wire = act.wireCrystals, .radius = 6, .opacity = shown, .scale = 1 + 0.03 * level + 0.14 * beat, .lift = 0.35 * beat});
    });
    // Rings already flown through are gone; the rest stand along the course
    // in both directions.
    const auto [first, last] = ringsBetween(m_view->camera.z - View::Far, m_view->camera.z + View::Far);
    for (int i = first; i <= last; ++i) {
        const Ring r = World::ring(i);
        if (!m_view->flight->game.collected.count(i) && std::hypot(r.x - m_view->camera.x, r.z - m_view->camera.z) < View::Far)
            ring(r, act.spinRings ? time * 0.15 : 0);
    }
    // On the mirror floor the ball takes on the valley's dusk colours.
    if (m_view->valley > 0) {
        m_sphereColors.resize(ballColors.size());
        for (size_t i = 0; i < ballColors.size(); ++i)
            m_sphereColors[i] = blend(ballColors[i], i % 2 ? mirageLight : mirageDark, m_view->valley);
    }
    const std::vector<Vec> &sphereColors = m_view->valley > 0 ? m_sphereColors : ballColors;
    // A ball belongs to its fixed world cell, not to the ship's current
    // segment. Crossing a segment boundary must not replace a visible ball.
    for (const Obstacle &ball : World::balls(m_view->camera.z, View::Far, time))
        mesh(sphere, {ball.x, ball.y, ball.z}, {ball.spin, 0, 0.25}, sphereColors,
             {.wire = act.wireBall, .radius = 5});
    if (act.floater)
        mesh(crystal, {-12, 8 + std::sin(time), m_view->flight->ship.z + 45}, {time * 0.7, time * 0.3, 0}, colors({m_view->palette.crystal}),
             {.wire = true, .radius = 6});
}

void WorldLayer::buildShip() {
    static const Mesh hull{ShipShape::HullVertices, ShipShape::HullFaces};
    static const Mesh canopy{{{0, 0.13, 2.7}, {-0.4, 0.25, 0.45}, {0.4, 0.25, 0.45},
                              {0, 0.98, 1.2}, {0, 0.68, 0.2}},
                             {{0, 1, 3}, {0, 3, 2}, {1, 4, 3}, {3, 4, 2}, {1, 2, 4}}};
    static const Mesh engine{{{-0.35, 0.22, 0.65}, {0.35, 0.22, 0.65},
                              {-0.32, 0.22, -1.65}, {0.32, 0.22, -1.65},
                              {-0.4, -0.3, -1.7}, {0.4, -0.3, -1.7},
                              {0, 0.5, -0.55}, {0, -0.25, 0.8}},
                             {{0, 6, 1}, {0, 2, 6}, {1, 6, 3}, {2, 3, 6},
                              {2, 4, 5}, {2, 5, 3}, {0, 7, 4}, {0, 4, 2}, {1, 3, 5}, {1, 5, 7}}};

    // The ship's paint, parsed once.
    static const std::vector<Vec> hullColors{hex("#f4c8ba"), hex("#b998ae"), hex("#515575"), hex("#d7a9b6"), hex("#847790"), hex("#373f5d")};
    static const std::vector<Vec> engineColors{hex("#fff0cb"), hex("#dcb8bb"), hex("#7e6589"), hex("#fff1d8"), hex("#423956")};
    static const std::vector<Vec> canopyColors{hex("#28475e"), hex("#35516d"), hex("#52617e"), hex("#303e5c")};
    static const std::array<Vec, 6> wingColors{hex("#ffb4a6"), hex("#f5a399"), hex("#ff32b9"), hex("#655d80"), hex("#403d60"), hex("#c290a6")};
    static const QColor hullStroke("#8e829a"), canopyStroke("#80b8cd"), finStroke("#ee63bf");
    static const std::array<QColor, 2> finColors{QColor("#ef3caa"), QColor("#982377")};

    const auto &ship = m_view->flight->ship;
    const Vec at{ship.x, ship.y, ship.z}, rotation{ship.yaw, ship.pitch, ship.bank};
    // After a hit the ship flickers until it can be hurt again.
    const double opacity = m_view->flight->game.hurt > 0 && std::fmod(m_view->flight->game.hurt, 0.2) < 0.1 ? 0.35 : 1;
    mesh(hull, at, rotation, hullColors, {.ship = true, .stroke = hullStroke, .radius = 5, .opacity = opacity});

    auto place = [&](const Vec &v) { return m_view->toCamera(translated(rotate(v, rotation), at)); };
    for (const double side : {-1.0, 1.0}) {
        const auto panels = ShipShape::wingPanels(ship, side);
        for (size_t i = 0; i < panels.size(); ++i) {
            const auto &panel = panels[i];
            // The wings are thin and double-sided, lit from whichever side faces the light.
            const Vec n = normalized(cross(sub(rotate(panel[1], rotation), rotate(panel[0], rotation)),
                                           sub(rotate(panel[2], rotation), rotate(panel[0], rotation))));
            m_points.clear();
            for (const Vec &v : panel)
                m_points.push_back(place(v));
            face(m_points, rgb(scaled(wingColors[i], 0.7 + 0.3 * std::abs(dot(n, light())))), rgb(scaled(wingColors[i], 1.08)), opacity, true);
        }
        const auto fins = ShipShape::finPanels(side);
        for (size_t i = 0; i < fins.size(); ++i) {
            m_points.clear();
            for (const Vec &v : fins[i]) m_points.push_back(place(v));
            face(m_points, finColors[i], finStroke, opacity, true);
        }
        const Vec engineAt = translated(rotate({ShipShape::EngineX * side, 0, 0}, rotation), at);
        mesh(engine, engineAt, rotation, engineColors, {.ship = true, .radius = 5, .opacity = opacity, .lift = 0.3});
    }
    mesh(canopy, at, rotation, canopyColors,
         {.emissive = true, .ship = true, .stroke = canopyStroke, .radius = 5, .opacity = opacity});

    // Short nested jets give the nozzles a continuous white/cyan core;
    // cached soft particles carry their cooling magenta trail behind them.
    if (m_view->flight->game.wrecked >= 0) return;
    const double thrust = 1 + m_view->flight->boostAmount();
    const double length = (1.2 + 0.08 * std::sin(m_view->flight->time * 28)) * thrust;
    for (double side : {-1.0, 1.0}) {
        const Vec nozzle = translated(rotate({ShipShape::EngineX * side, ShipShape::EngineY, ShipShape::EngineZ}, rotation), at);
        for (int layer = 0; layer < 2; ++layer) {
            const double r = layer ? 0.21 : 0.32, end = -length * (layer ? 0.7 : 1);
            const Mesh jet{{{-r, r, 0}, {r, r, 0}, {r, -r, 0}, {-r, -r, 0},
                            {-r * 0.55, 0, end}, {r * 0.55, 0, end}},
                           {{0,1,2}, {0,2,3}, {0,4,5}, {0,5,1}, {3,2,5}, {3,5,4}}};
            static const std::vector<Vec> core{hex("#efffff")}, sheath{hex("#65fff3")};
            mesh(jet, nozzle, rotation, layer ? core : sheath,
                 {.emissive = true, .glow = true, .radius = 6, .opacity = layer ? 0.9 : 0.55});
        }
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
            // White hot at the nozzle, cooling through cyan to magenta.
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
        case Particle::Flash:
            size *= 0.6 + 1.8 * t;
            opacity = 0.9 * (1 - t) * (1 - t);
            sprite = FlashSprite;
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
    const QColor ramp[] = {QColor("#edffff"), QColor("#76fff2"), QColor("#dd9dff"), QColor("#ff46c7"), QColor("#8b297f")};
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
    m_puffs.push_back(puff(QColor("#ffffff"), QColor("#ffe6a8"), true));
}

// Painter's algorithm: far faces first, near faces over them. Faces at the
// same depth keep the order they were queued in.
void WorldLayer::paint(QPainter &p) {
    m_order.clear();
    m_order.reserve(m_faces.size());
    for (size_t i = 0; i < m_faces.size(); ++i)
        m_order.emplace_back(m_faces[i].depth, std::uint32_t(i));
    std::sort(m_order.begin(), m_order.end(), [](const auto &a, const auto &b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    // Neighbouring faces are often of one kind; the painter's state only
    // changes when the next face needs something different.
    double opacity = -1;
    int glow = -1;
    QColor fill, stroke;
    bool brushSet = false, penSet = false;
    for (const auto &[depth, index] : m_order) {
        const SceneFace &f = m_faces[index];
        if (f.opacity != opacity) {
            p.setOpacity(f.opacity);
            opacity = f.opacity;
        }
        if (int(f.glow) != glow) {
            p.setCompositionMode(f.glow ? QPainter::CompositionMode_Plus : QPainter::CompositionMode_SourceOver);
            glow = f.glow;
        }
        if (f.sprite >= 0) {
            p.drawImage(f.box, m_puffs[f.sprite]);
            continue;
        }
        if (!brushSet || f.fill != fill) {
            p.setBrush(f.fill.isValid() ? QBrush(f.fill) : QBrush(Qt::NoBrush));
            fill = f.fill;
            brushSet = true;
        }
        if (!penSet || f.stroke != stroke) {
            // A zero-width pen is cosmetic, one pixel wide: the rasterizer's fast path.
            p.setPen(f.stroke.isValid() ? QPen(f.stroke, 0) : QPen(Qt::NoPen));
            stroke = f.stroke;
            penSet = true;
        }
        p.drawConvexPolygon(f.polygon);
    }
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.setOpacity(1);
}
