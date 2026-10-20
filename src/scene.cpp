// A port of the Omawing SVG demo's renderer to QPainter. The SVG edition
// set the attributes of SVG elements each frame; this draws the same layers,
// in the same order, straight onto the window.
#include "scene.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QRadialGradient>
#include <algorithm>
#include <cmath>

namespace {
using Vec = Scene::Vec;

double mix(double a, double b, double t) { return a + (b - a) * t; }
double clampTo(double x, double a, double b) { return std::max(a, std::min(b, x)); }
double smooth(double t) { return t * t * (3 - 2 * t); }
double dot(const Vec &a, const Vec &b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec sub(const Vec &a, const Vec &b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec cross(const Vec &a, const Vec &b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Vec normalized(const Vec &v) {
    const double n = std::hypot(v[0], v[1], v[2]);
    return n ? Vec{v[0] / n, v[1] / n, v[2] / n} : v;
}
Vec scaled(const Vec &v, double s) { return {v[0] * s, v[1] * s, v[2] * s}; }
Vec blend(const Vec &a, const Vec &b, double t) { return {mix(a[0], b[0], t), mix(a[1], b[1], t), mix(a[2], b[2], t)}; }
Vec hex(const char *code) {
    const QColor c(code);
    return {double(c.red()), double(c.green()), double(c.blue())};
}
QColor rgb(const Vec &v) {
    return QColor(int(std::lround(clampTo(v[0], 0, 255))), int(std::lround(clampTo(v[1], 0, 255))),
                  int(std::lround(clampTo(v[2], 0, 255))));
}
// A stable pseudo-random number in [0, 1) for a grid cell, so scenery stays
// put as the camera moves.
double hash(double x, double z) {
    const double n = std::sin(x * 127.1 + z * 311.7) * 43758.5453;
    return n - std::floor(n);
}
Vec rotate(const Vec &v, const Vec &r) {
    const double yaw = r[0], pitch = r[1], bank = r[2];
    const double x = v[0] * std::cos(bank) - v[1] * std::sin(bank), y = v[0] * std::sin(bank) + v[1] * std::cos(bank);
    const double yy = y * std::cos(pitch) - v[2] * std::sin(pitch), z = y * std::sin(pitch) + v[2] * std::cos(pitch);
    return {x * std::cos(yaw) + z * std::sin(yaw), yy, -x * std::sin(yaw) + z * std::cos(yaw)};
}
Vec translated(const Vec &v, const Vec &by) { return {v[0] + by[0], v[1] + by[1], v[2] + by[2]}; }
QColor withAlpha(const char *code, double alpha) {
    QColor c(code);
    c.setAlphaF(alpha);
    return c;
}

struct Theme {
    Vec top, mid, horizon, floor, tileA, tileB, accent, crystal;
};
const std::array<Theme, 3> &themes() {
    static const std::array<Theme, 3> list{{
        {hex("#100d33"), hex("#62276f"), hex("#e99489"), hex("#5e3b67"), hex("#e9afab"), hex("#38284f"), hex("#ffd093"), hex("#80e3df")},
        {hex("#091b32"), hex("#163f5f"), hex("#67b7b2"), hex("#1a3e53"), hex("#5e9f9c"), hex("#122f49"), hex("#9bf4ca"), hex("#e3b5f5")},
        {hex("#070c22"), hex("#1d2056"), hex("#7b528b"), hex("#282144"), hex("#705878"), hex("#151b35"), hex("#ffa1bc"), hex("#91d5fa")},
    }};
    return list;
}

const Vec Light = normalized({-0.5, 0.85, -0.4});
const char *const ScrollText =
    "   *** OMAWING ***   GREETINGS TO THE DREAMERS, CODERS & PIXEL PUSHERS   //   NO DISKS REQUIRED   //"
    "   ALL VECTORS, ALL VIBES   //   TAKE THE CONTROLS AND CHASE THE HORIZON!   ";

QFont monoFont(double pixelSize, double spacing = 0, int weight = QFont::Normal) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(std::max(1, int(std::lround(pixelSize))));
    font.setLetterSpacing(QFont::AbsoluteSpacing, spacing);
    font.setWeight(QFont::Weight(weight));
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
}

// The HUD spaces its letters out, which only suits scripts whose letters
// stand alone; joined scripts like Arabic, and the Indic scripts, keep
// their natural spacing.
QFont hudFont(double pixelSize, double spacing, const QString &text) {
    for (const QChar c : text) {
        const QChar::Script script = c.script();
        if (script != QChar::Script_Latin && script != QChar::Script_Greek && script != QChar::Script_Cyrillic &&
            script != QChar::Script_Common && script != QChar::Script_Inherited)
            return monoFont(pixelSize, 0);
    }
    return monoFont(pixelSize, spacing);
}

// Draws text with its end at x, like SVG's text-anchor="end".
void drawEnd(QPainter &p, double x, double y, const QString &text) {
    p.drawText(QPointF(x - QFontMetricsF(p.font()).horizontalAdvance(text), y), text);
}
} // namespace

QString Scene::actName(int act) {
    static const char *const names[] = {"COPPER COAST", "VECTOR GARDEN", "STAR RUN"};
    return names[std::clamp(act, 0, 2)];
}

void Scene::begin(const Flight &flight, const QSizeF &size) {
    m_flight = &flight;
    const double aspect = size.height() > 0 ? size.width() / size.height() : 16.0 / 9;
    m_width = std::max(MinWidth, BaseHeight * aspect);
    m_height = m_width / aspect;
    m_act = flight.act();

    // Colours blend over the first two seconds of each act.
    const Theme &previous = themes()[(m_act + 2) % 3], &next = themes()[m_act];
    const double t = flight.time < 2 ? 1 : smooth(clampTo(std::fmod(flight.time, Flight::ActLength) / 2, 0, 1));
    m_palette = {blend(previous.top, next.top, t), blend(previous.mid, next.mid, t),
                 blend(previous.horizon, next.horizon, t), blend(previous.floor, next.floor, t),
                 blend(previous.tileA, next.tileA, t), blend(previous.tileB, next.tileB, t),
                 blend(previous.accent, next.accent, t), blend(previous.crystal, next.crystal, t)};

    const auto &cam = flight.camera;
    m_forward = {std::sin(cam.yaw) * std::cos(cam.pitch), std::sin(cam.pitch), std::cos(cam.yaw) * std::cos(cam.pitch)};
    m_right = {std::cos(cam.yaw), 0, -std::sin(cam.yaw)};
    m_up = cross(m_forward, m_right);
    m_faces.clear();
}

Vec Scene::toCamera(const Vec &v) const {
    const auto &cam = m_flight->camera;
    const Vec d{v[0] - cam.x, v[1] - cam.y, v[2] - cam.z};
    return {dot(d, m_right), dot(d, m_up), dot(d, m_forward)};
}

QPointF Scene::project(const Vec &p) const {
    return {m_width / 2 + Focal * p[0] / p[2], m_height / 2 - Focal * p[1] / p[2]};
}

// Clips against the near plane and the four sides of the view, so nothing
// projects behind the camera or to coordinates far outside the window.
std::vector<Vec> Scene::clip(std::vector<Vec> polygon) const {
    const double sx = m_width / (2 * Focal), sy = m_height / (2 * Focal);
    auto distance = [&](int plane, const Vec &p) {
        switch (plane) {
        case 0: return p[2] - Near;
        case 1: return p[0] + p[2] * sx;
        case 2: return -p[0] + p[2] * sx;
        case 3: return p[1] + p[2] * sy;
        default: return -p[1] + p[2] * sy;
        }
    };
    for (int plane = 0; plane < 5 && !polygon.empty(); ++plane) {
        std::vector<Vec> out;
        for (size_t i = 0; i < polygon.size(); ++i) {
            const Vec &a = polygon[i], &b = polygon[(i + 1) % polygon.size()];
            const double da = distance(plane, a), db = distance(plane, b);
            if (da >= 0)
                out.push_back(a);
            if ((da >= 0) != (db >= 0))
                out.push_back(blend(a, b, da / (da - db)));
        }
        polygon = std::move(out);
    }
    return polygon;
}

QPolygonF Scene::polygonOf(const std::vector<Vec> &points) const {
    QPolygonF polygon;
    polygon.reserve(int(points.size()));
    for (const Vec &p : points)
        polygon << project(p);
    return polygon;
}

double Scene::fog(double depth) const { return smooth(clampTo((depth - 35) / (Far - 35), 0, 1)); }

void Scene::face(const std::vector<Vec> &points, const QColor &fill, const QColor &stroke, double opacity, bool ship) {
    const auto clipped = clip(points);
    if (clipped.size() < 3)
        return;
    double depth = 0;
    for (const Vec &p : clipped)
        depth += p[2];
    m_faces.push_back({polygonOf(clipped), fill, stroke, opacity, depth / clipped.size(), ship});
}

void Scene::mesh(const Mesh &geometry, const Vec &at, const Vec &rotation, const std::vector<Vec> &colors,
                 const MeshOptions &options) {
    const Vec centreCamera = toCamera(at);
    if (centreCamera[2] < -options.radius || centreCamera[2] > Far + options.radius ||
        std::abs(centreCamera[0]) > centreCamera[2] * m_width / (2 * Focal) + options.radius * 2)
        return;
    std::vector<Vec> vertices, projected;
    Vec centre{0, 0, 0};
    for (const Vec &v : geometry.vertices) {
        vertices.push_back(translated(rotate(v, rotation), at));
        centre = translated(centre, scaled(vertices.back(), 1.0 / geometry.vertices.size()));
    }
    for (const Vec &v : vertices)
        projected.push_back(toCamera(v));
    const auto &cam = m_flight->camera;
    for (size_t i = 0; i < geometry.faces.size(); ++i) {
        const auto &indices = geometry.faces[i];
        const Vec &v0 = vertices[indices[0]];
        Vec n = normalized(cross(sub(vertices[indices[1]], v0), sub(vertices[indices[2]], v0)));
        if (dot(n, sub(v0, centre)) < 0)
            n = scaled(n, -1);
        // Solid faces turned away from the camera are hidden; wireframes show all edges.
        if (!options.wire && dot(n, sub(v0, {cam.x, cam.y, cam.z})) >= 0)
            continue;
        std::vector<Vec> points;
        double depth = 0;
        for (int index : indices) {
            points.push_back(projected[index]);
            depth += projected[index][2];
        }
        depth /= indices.size();
        const Vec &source = colors[i % colors.size()];
        const double brightness = options.emissive ? 1 : 0.5 + 0.5 * std::max(0.0, dot(n, Light));
        const QColor fill = options.wire ? QColor() : rgb(blend(scaled(source, brightness), m_palette.floor, fog(depth)));
        const QColor stroke = options.wire ? rgb(blend(source, m_palette.floor, fog(depth)))
                            : options.stroke.isValid() ? options.stroke : fill;
        face(points, fill, stroke, options.opacity, options.ship);
    }
}

void Scene::ring(double x, double y, double z, double spin) {
    const int count = 12;
    const double radius = 5, thickness = 0.18;
    // Dissolve a gate as the ship passes it, before it sweeps through the
    // chase camera and covers the entire view.
    const double opacity = smooth(clampTo((z - m_flight->ship.z - 2) / 7, 0, 1));
    if (opacity <= 0)
        return;
    for (int i = 0; i < count; ++i) {
        const double a = i * 2 * M_PI / count + spin, b = (i + 1) * 2 * M_PI / count + spin, r = radius - thickness;
        const std::vector<Vec> points{toCamera({x + std::cos(a) * radius, y + std::sin(a) * radius, z}),
                                      toCamera({x + std::cos(b) * radius, y + std::sin(b) * radius, z}),
                                      toCamera({x + std::cos(b) * r, y + std::sin(b) * r, z}),
                                      toCamera({x + std::cos(a) * r, y + std::sin(a) * r, z})};
        double depth = 0;
        for (const Vec &p : points)
            depth += p[2] / 4;
        face(points, rgb(blend(i % 3 ? m_palette.accent : m_palette.crystal, m_palette.floor, fog(depth))), QColor(),
             opacity, false);
    }
}

void Scene::paintSky(QPainter &p) {
    const double time = m_flight->time, W = m_width;
    const double horizon = m_height / 2 + Focal * std::tan(m_flight->camera.pitch), split = clampTo(horizon, 0, m_height);

    QLinearGradient sky(0, 0, 0, std::max(1.0, split));
    sky.setColorAt(0, rgb(m_palette.top));
    sky.setColorAt(0.66, rgb(m_palette.mid));
    sky.setColorAt(1, rgb(m_palette.horizon));
    p.fillRect(QRectF(0, 0, W, std::max(1.0, split)), sky);

    // Copper bars: the Amiga's copper changed colours line by line.
    for (int i = 0; i < 24; ++i) {
        const double y = horizon - 170 + i * 7 + std::sin(time * 0.7 + i * 0.18) * 6;
        p.setOpacity(0.12 + i / 90.0);
        p.fillRect(QRectF(0, y, W, 6), rgb(blend(m_palette.mid, m_palette.horizon, i / 24.0)));
    }
    p.setOpacity(1);

    // Stars sit at infinity: only the camera's rotation moves them.
    const QColor starColor("#fcecc6");
    for (int i = 0; i < 130; ++i) {
        const double az = hash(i, 2) * 2 * M_PI, elevation = 0.05 + hash(i, 3) * 1.2, phase = hash(i, 4);
        const Vec direction{std::sin(az) * std::cos(elevation), std::sin(elevation), std::cos(az) * std::cos(elevation)};
        const Vec c{dot(direction, m_right), dot(direction, m_up), dot(direction, m_forward)};
        if (c[2] <= 0.05)
            continue;
        const QPointF at = project(c);
        if (at.x() < 0 || at.x() > W || at.y() < 88 || at.y() > horizon)
            continue;
        const double s = 0.3 + phase * (0.6 + 0.3 * std::sin(time + phase * 10));
        p.fillRect(QRectF(at.x() - 0.8, at.y() - 0.8, s + 1.6, 1.6), starColor);
    }

    const Vec sunDirection{std::sin(0.44) * std::cos(0.1), std::sin(0.1), std::cos(0.44) * std::cos(0.1)};
    const Vec sun{dot(sunDirection, m_right), dot(sunDirection, m_up), dot(sunDirection, m_forward)};
    if (sun[2] > 0.15) {
        p.save();
        p.translate(project(sun));
        p.setOpacity(m_act == 2 ? 0.2 : 0.85);
        blit(p, m_halo, {0, 0});
        for (int i = 0; i < 10; ++i) {
            const double y = -68 + i * 14, half = std::sqrt(std::max(0.0, 70.0 * 70 - (y + 5) * (y + 5)));
            p.fillRect(QRectF(-half, y, half * 2, i < 4 ? 14 : 10 - (i - 4)), QColor("#ffd098"));
        }
        p.restore();
    }

    QPainterPath mountains(QPointF(0, horizon + 12));
    for (int i = 0; i <= 28; ++i) {
        const double peak = 12 + 44 * hash(i, 9) + 13 * std::sin(i * 1.4 + m_flight->camera.yaw);
        mountains.lineTo(i * W / 28, horizon - peak);
    }
    mountains.lineTo(W, horizon + 15);
    mountains.closeSubpath();
    p.fillPath(mountains, rgb(blend(m_palette.floor, m_palette.mid, 0.5)));

    // The camera never rolls, so ground depth depends only on the screen row:
    // the dark squares, fogged with distance, are one vertical gradient, and
    // only the light squares are drawn as polygons over it.
    if (split < m_height) {
        const auto &cam = m_flight->camera;
        QLinearGradient ground(0, split, 0, m_height);
        ground.setColorAt(0, rgb(m_palette.floor));
        for (double d = Far; d >= 3; d *= 0.8) {
            const Vec c = toCamera({cam.x + std::sin(cam.yaw) * d, 0, cam.z + std::cos(cam.yaw) * d});
            if (c[2] <= Near)
                continue;
            const double y = project(c).y();
            if (y > split && y < m_height)
                ground.setColorAt((y - split) / (m_height - split), rgb(blend(m_palette.tileB, m_palette.floor, fog(c[2]))));
        }
        ground.setColorAt(1, rgb(m_palette.tileB));
        p.fillRect(QRectF(0, split, W, m_height - split), ground);
    }
}

void Scene::paintGround(QPainter &p) {
    const auto &cam = m_flight->camera;
    const int step = 6, extent = 15;
    const int gx = int(std::floor(cam.x / step)), gz = int(std::floor(cam.z / step));
    std::vector<std::vector<Vec>> grid;
    for (int j = -extent; j <= extent; ++j) {
        std::vector<Vec> row;
        for (int i = -extent; i <= extent; ++i)
            row.push_back(toCamera({double(gx + i) * step, 0, double(gz + j) * step}));
        grid.push_back(std::move(row));
    }
    // Tiles are grouped by distance and colour, so the checkerboard is 16
    // paths rather than hundreds of shapes.
    std::array<std::vector<QPolygonF>, 8> tiles;
    QPainterPath lines;
    for (int j = 0; j < extent * 2; ++j)
        for (int i = 0; i < extent * 2; ++i) {
            const std::vector<Vec> points{grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i]};
            double depth = 0;
            for (const Vec &q : points)
                depth += q[2] / 4;
            const int parity = (((gx + gz + i + j) % 2) + 2) % 2;
            if (depth < -5 || depth > Far || (!parity && m_act != 2))
                continue;
            const auto clipped = clip(points);
            if (clipped.size() < 3)
                continue;
            const int bucket = std::min(7, int(std::floor(std::max(0.0, depth) / Far * 8)));
            const QPolygonF polygon = polygonOf(clipped);
            if (parity)
                tiles[bucket].push_back(polygon);
            if (m_act == 2 && depth < 65) {
                lines.addPolygon(polygon);
                lines.closeSubpath();
            }
        }
    for (int i = 0; i < 8; ++i) {
        const double haze = fog((i + 0.5) * Far / 8);
        if (haze >= 0.95)
            continue;
        p.setPen(Qt::NoPen);
        p.setBrush(rgb(blend(m_palette.tileA, m_palette.floor, haze)));
        for (const QPolygonF &polygon : tiles[i])
            p.drawPolygon(polygon);
    }
    if (!lines.isEmpty()) {
        p.setOpacity(0.23);
        p.strokePath(lines, QPen(QColor("#d4a3be"), 0.55));
        p.setOpacity(1);
    }

    const auto &ship = m_flight->ship;
    std::vector<Vec> shadow;
    for (int i = 0; i < 24; ++i)
        shadow.push_back(toCamera({ship.x + std::cos(i / 12.0 * M_PI) * 2.6, 0.03, ship.z + std::sin(i / 12.0 * M_PI) * 1.7}));
    const auto clipped = clip(shadow);
    if (clipped.size() >= 3) {
        QPainterPath path;
        path.addPolygon(polygonOf(clipped));
        p.setOpacity(clampTo(0.55 - ship.y * 0.025, 0.12, 0.5));
        p.fillPath(path, QColor("#12102a"));
        p.setOpacity(1);
    }
}

void Scene::buildScenery() {
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

    const double time = m_flight->time;
    const auto &cam = m_flight->camera;
    const int cx = int(std::floor(cam.x / 24)), cz = int(std::floor(cam.z / 24));
    for (int iz = cz - 4; iz <= cz + 4; ++iz)
        for (int ix = cx - 4; ix <= cx + 4; ++ix) {
            const double x = ix * 24 + 12 + (hash(ix, iz) - 0.5) * 8, z = iz * 24 + 14;
            if (std::hypot(x - cam.x, z - cam.z) > Far || std::abs(x) < 8)
                continue;
            if ((ix + iz) % 3 == 0)
                mesh(pyramid, {x, 0, z}, {0, 0, 0}, {m_palette.mid, m_palette.crystal}, {.wire = m_act != 0, .radius = 9});
            else
                mesh(crystal, {x, 2 + std::sin(time * 1.4 + ix + iz) * 0.5, z}, {time * 0.4 + ix, 0, 0},
                     {m_palette.crystal, m_palette.accent}, {.wire = m_act == 2, .radius = 6});
        }
    const int start = int(std::floor((m_flight->ship.z - 15) / 32));
    for (int i = start; i < start + 4; ++i)
        ring(5 * std::sin(i * 1.1), 5.5, i * 32 + 24, m_act == 2 ? time * 0.15 : 0);
    const double ballZ = std::floor((m_flight->ship.z + 8) / 110) * 110 + 36;
    mesh(sphere, {14, 4.3 + std::abs(std::sin(time * 1.25)) * 3.5, ballZ}, {time * 0.5, 0, 0.25}, ballColors,
         {.wire = m_act == 2, .radius = 5});
    if (m_act == 1)
        mesh(crystal, {-12, 8 + std::sin(time), m_flight->ship.z + 45}, {time * 0.7, time * 0.3, 0}, {m_palette.crystal},
             {.wire = true, .radius = 6});
}

void Scene::buildShip() {
    static const Mesh hull{{{0, 0, 2.8}, {-0.65, 0, -1.6}, {0.65, 0, -1.6}, {0, 0.65, -0.4}, {0, -0.4, -0.6}},
                           {{0, 1, 3}, {0, 3, 2}, {1, 2, 3}, {0, 4, 1}, {0, 2, 4}, {1, 4, 2}}};
    static const Mesh wings{{{-0.5, 0, 1}, {-3.3, -0.12, -1.9}, {-0.55, 0.05, -1.2}, {0.5, 0, 1}, {3.3, -0.12, -1.9},
                             {0.55, 0.05, -1.2}, {-0.45, 0.1, -1.45}, {-0.75, 1, -1.9}, {-0.7, 0.1, -0.65},
                             {0.45, 0.1, -1.45}, {0.75, 1, -1.9}, {0.7, 0.1, -0.65}},
                            {{0, 1, 2}, {3, 5, 4}, {6, 7, 8}, {9, 11, 10}}};
    static const Mesh canopy{{{0, 0.13, 1.65}, {-0.34, 0.29, -0.2}, {0.34, 0.29, -0.2}, {0, 0.76, -0.35}},
                             {{0, 1, 3}, {0, 3, 2}, {1, 2, 3}}};
    static const Mesh exhaust{{{-0.28, 0.22, -1.6}, {0.28, 0.22, -1.6}, {0.28, -0.2, -1.6}, {-0.28, -0.2, -1.6}, {0, 0, -3.6}},
                              {{0, 1, 4}, {1, 2, 4}, {2, 3, 4}, {3, 0, 4}}};

    const auto &ship = m_flight->ship;
    const Vec at{ship.x, ship.y, ship.z}, rotation{ship.yaw, ship.pitch, ship.bank};
    mesh(hull, at, rotation, {hex("#e8e8dc"), hex("#92a9b7"), hex("#59638d")},
         {.ship = true, .stroke = QColor("#dfd6cd"), .radius = 5});
    // The thin wings are intentionally double-sided.
    for (size_t i = 0; i < wings.faces.size(); ++i) {
        std::vector<Vec> points;
        for (int index : wings.faces[i])
            points.push_back(toCamera(translated(rotate(wings.vertices[index], rotation), at)));
        face(points, QColor(i < 2 ? "#e4b3ab" : "#cf557a"), QColor("#f8dfc1"), 1, true);
    }
    mesh(canopy, at, rotation, {hex("#7de6e1"), hex("#398da8"), hex("#286285")},
         {.emissive = true, .ship = true, .stroke = QColor("#baf8e6"), .radius = 5});
    const double thrust = 1 + (ship.speed - 13) / 18;
    Mesh jet = exhaust;
    jet.vertices[4][2] = -2.5 - thrust * (0.8 + 0.2 * std::sin(m_flight->time * 28));
    for (double offset : {-0.65, 0.65})
        mesh(jet, translated(rotate({offset, -0.05, 0}, rotation), at), rotation,
             {hex("#ffbe8b"), hex("#fff2b6"), hex("#f77699")}, {.emissive = true, .radius = 5});
}

// Painter's algorithm: far faces first, near faces over them.
void Scene::paintWorld(QPainter &p) {
    std::stable_sort(m_faces.begin(), m_faces.end(), [](const Face &a, const Face &b) { return a.depth > b.depth; });
    for (const Face &f : m_faces) {
        p.setOpacity(f.opacity);
        p.setBrush(f.fill.isValid() ? QBrush(f.fill) : QBrush(Qt::NoBrush));
        // A one-pixel cosmetic pen takes the rasterizer's fast path.
        QPen pen(f.stroke, 0);
        pen.setCosmetic(true);
        p.setPen(f.stroke.isValid() ? pen : QPen(Qt::NoPen));
        p.drawPolygon(f.polygon);
    }
    p.setOpacity(1);
}

namespace {
// The demo's chrome: a sunset ramp with a hard horizon line through the middle.
QLinearGradient chrome(const QRectF &box) {
    QLinearGradient g(0, box.top(), 0, box.bottom());
    g.setColorAt(0, QColor("#fbf0b3"));
    g.setColorAt(0.44, QColor("#fff7d9"));
    g.setColorAt(0.49, QColor("#edaa84"));
    g.setColorAt(0.5, QColor("#a53b7d"));
    g.setColorAt(0.73, QColor("#f16c96"));
    g.setColorAt(1, QColor("#f6cb9d"));
    return g;
}
}

Scene::Sprite Scene::render(const QRectF &bounds, const std::function<void(QPainter &)> &draw) const {
    const QRectF box = bounds.adjusted(-1, -1, 1, 1);
    QImage image(QSize(std::max(1, int(std::ceil(box.width() * m_pixelsPerUnit))),
                       std::max(1, int(std::ceil(box.height() * m_pixelsPerUnit)))),
                 QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(m_pixelsPerUnit, m_pixelsPerUnit);
    p.translate(-box.topLeft());
    draw(p);
    return {image, box.topLeft()};
}

// Draws a sprite pixel for pixel, without scaling it again.
void Scene::blit(QPainter &p, const Sprite &sprite, const QPointF &anchor) const {
    const QPointF at = p.worldTransform().map(anchor + sprite.offset);
    p.save();
    p.resetTransform();
    p.drawImage(QPointF(std::round(at.x()), std::round(at.y())), sprite.image);
    p.restore();
}

const Scene::Sprite &Scene::glyph(QChar c) {
    auto it = m_glyphs.find(c);
    if (it == m_glyphs.end()) {
        QPainterPath path;
        const QFont font = monoFont(26, 0, QFont::Black);
        path.addText(-QFontMetricsF(font).horizontalAdvance(c) / 2, 0, font, QString(c));
        it = m_glyphs.insert(c, render(path.boundingRect(), [&](QPainter &p) { p.fillPath(path, chrome(path.boundingRect())); }));
    }
    return *it;
}

void Scene::prepare(double pixelsPerUnit) {
    if (std::abs(pixelsPerUnit - m_pixelsPerUnit) < 1e-6 && std::abs(m_preparedWidth - m_width) < 1e-6 &&
        std::abs(m_preparedHeight - m_height) < 1e-6)
        return;
    const bool rescaled = std::abs(pixelsPerUnit - m_pixelsPerUnit) >= 1e-6;
    m_pixelsPerUnit = pixelsPerUnit;
    m_preparedWidth = m_width;
    m_preparedHeight = m_height;
    m_scanlines = render(QRectF(0, 0, m_width, m_height), [&](QPainter &p) {
        p.setRenderHint(QPainter::Antialiasing, false);
        for (double y = 3; y < m_height; y += 4)
            p.fillRect(QRectF(0, y, m_width, 1), withAlpha("#090f29", 0.14));
    });
    m_scanlines.offset = {0, 0};
    if (!rescaled)
        return;
    m_glyphs.clear();
    QFont font("Liberation Sans");
    font.setPixelSize(49);
    font.setWeight(QFont::Black);
    font.setItalic(true);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
    QPainterPath title;
    title.addText(0, 0, font, "OMAWING");
    // The shadow sits 2 units right and 4 down from the chrome.
    m_title = render(title.boundingRect().united(title.boundingRect().translated(2, 4)), [&](QPainter &p) {
        p.setOpacity(0.85);
        p.fillPath(title.translated(2, 4), QColor("#211638"));
        p.setOpacity(1);
        p.fillPath(title, chrome(title.boundingRect()));
    });
    m_halo = render(QRectF(-150, -150, 300, 300), [](QPainter &p) {
        QRadialGradient halo(0, 0, 150);
        halo.setColorAt(0, withAlpha("#ffb26c", 0.4));
        halo.setColorAt(1, withAlpha("#ffb26c", 0));
        p.setPen(Qt::NoPen);
        p.setBrush(halo);
        p.drawEllipse(QPointF(0, 0), 150, 150);
    });
}

void Scene::paintOverlay(QPainter &p, const Hud &hud) {
    const double time = m_flight->time, W = m_width, boost = m_flight->boostAmount();
    // The thrust gauge, status, and scroller stay pinned to the bottom.
    const double b = m_height - BaseHeight;

    // Speed lines streak outwards while boosting.
    for (int i = 0; i < 18; ++i) {
        const double a = i / 18.0 * M_PI * 2 + 0.05 * time;
        const double length = 24 + hash(i, 4) * 70, r = 200 + std::fmod(time * 170 + i * 39, 340);
        const QPointF from(W / 2 + std::cos(a) * r, m_height / 2 + 10 + std::sin(a) * r * 0.6);
        p.setOpacity(boost * 0.35 + (m_act == 2 ? 0.16 : 0));
        p.setPen(QPen(QColor("#a3e5f4"), 1.2));
        p.drawLine(from, from + QPointF(std::cos(a) * length, std::sin(a) * length * 0.6));
    }
    p.setOpacity(1);

    // Scanlines, like a CRT.
    blit(p, m_scanlines, {0, 0});
    blit(p, m_title, {26, 59});

    p.setPen(QColor("#f4cfb2"));
    p.setFont(hudFont(10, 3.5, hud.subtitle));
    p.drawText(QPointF(30, 80), hud.subtitle);
    p.setPen(QColor("#ffe2b3"));
    p.setFont(monoFont(13, 2));
    drawEnd(p, W - 30, 36, QString("0%1 / %2").arg(m_act + 1).arg(actName(m_act)));
    p.setPen(QColor("#d3bdd5"));
    p.setFont(hudFont(10, 1.6, hud.mode));
    drawEnd(p, W - 30, 59, hud.mode);
    for (int i = 0; i < 3; ++i)
        p.fillRect(QRectF(W - 82 + i * 19, 74, 12, 3), i == m_act ? rgb(m_palette.accent) : QColor("#65506f"));

    p.setPen(QColor("#e2c5cf"));
    p.setFont(hudFont(9, 2, hud.thrust));
    p.drawText(QPointF(30, b + 421), hud.thrust);
    p.fillRect(QRectF(30, b + 430, 94, 4), QColor("#2e2544"));
    p.fillRect(QRectF(30, b + 430, 43 + boost * 51, 4), QColor("#ffd19c"));
    p.setFont(monoFont(10));
    p.drawText(QPointF(30, b + 454), hud.altitude);
    p.setPen(QColor("#ffe0b0"));
    p.setFont(hudFont(11, 1, hud.status));
    drawEnd(p, W - 30, b + 453, hud.status);

    // The sine scroller along the bottom.
    p.setOpacity(0.84);
    p.fillRect(QRectF(0, b + 480, W, 60), QColor("#101024"));
    p.setOpacity(1);
    p.setPen(QPen(withAlpha("#e6a67c", 0.65), 1));
    p.drawLine(QPointF(0, b + 481), QPointF(W, b + 481));
    p.save();
    p.setClipRect(QRectF(0, b + 482, W, 58), Qt::IntersectClip);
    static const QString text = QString::fromLatin1(ScrollText);
    const double scroll = time * 8;
    const int offset = int(std::floor(scroll)), letters = int(std::ceil(W / 20)) + 3;
    for (int i = 0; i < letters; ++i) {
        const double x = (i - (scroll - offset)) * 20 - 20;
        const QChar c = text[(i + offset) % text.size()];
        if (c != ' ')
            blit(p, glyph(c), {x, b + 519 + std::sin(x * 0.012 + time * 2.4) * 7});
    }
    p.restore();
}

int Scene::paint(QPainter &painter, const Flight &flight, const QSizeF &size, const Hud &hud) {
    begin(flight, size);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(size.width() / m_width, size.width() / m_width);
    painter.setClipRect(QRectF(0, 0, m_width, m_height));
    prepare(painter.worldTransform().m11());
    paintSky(painter);
    paintGround(painter);
    buildScenery();
    buildShip();
    paintWorld(painter);
    paintOverlay(painter, hud);
    painter.restore();
    return int(m_faces.size());
}
