// A port of the Omawing SVG demo's renderer to QPainter. The SVG edition
// set the attributes of SVG elements each frame; this draws the same layers,
// in the same order, straight onto the window.
#include "scene.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QLocale>
#include <QPainter>
#include <QRadialGradient>
#include <algorithm>
#include <cmath>

namespace {
Vec hex(const char *code) {
    const QColor c(code);
    return {double(c.red()), double(c.green()), double(c.blue())};
}
Vec vecOf(const QColor &c) { return {double(c.red()), double(c.green()), double(c.blue())}; }
QColor rgb(const Vec &v) {
    return QColor(int(std::lround(clampTo(v[0], 0, 255))), int(std::lround(clampTo(v[1], 0, 255))),
                  int(std::lround(clampTo(v[2], 0, 255))));
}
QColor withAlpha(const char *code, double alpha) {
    QColor c(code);
    c.setAlphaF(alpha);
    return c;
}
QColor withAlpha(const QColor &color, double alpha) {
    QColor c(color);
    c.setAlphaF(clampTo(alpha, 0, 1));
    return c;
}

// Each act has its own colours and its own mix of solid and wireframe.
struct Act {
    const char *name;
    std::array<const char *, 8> colors;   // top, mid, horizon, floor, tileA, tileB, accent, crystal
    bool wirePyramids, wireCrystals, wireBall, grid, dimSun, spinRings, floater;
};
const std::array<Act, Flight::Acts> &acts() {
    static const std::array<Act, Flight::Acts> list{{
        {"COPPER COAST", {"#100d33", "#62276f", "#e99489", "#5e3b67", "#e9afab", "#38284f", "#ffd093", "#80e3df"},
         false, false, false, false, false, false, false},
        {"VECTOR GARDEN", {"#091b32", "#163f5f", "#67b7b2", "#1a3e53", "#5e9f9c", "#122f49", "#9bf4ca", "#e3b5f5"},
         true, false, false, false, false, false, true},
        {"STAR RUN", {"#070c22", "#1d2056", "#7b528b", "#282144", "#705878", "#151b35", "#ffa1bc", "#91d5fa"},
         true, true, true, true, true, true, false},
        // Scene::ThemeAct: these colours stand in until the Omarchy theme is read.
        {"OMARCHY", {"#0e0f17", "#3b4a7a", "#a3b8f0", "#2f3858", "#8fa4e0", "#14151f", "#7aa2f7", "#ad8ee6"},
         false, false, false, true, false, false, false},
        {"NEON MIRAGE", {"#12002b", "#4b0f6b", "#ff3fa4", "#1a0633", "#3d1466", "#14052a", "#2ee6ff", "#ff5fd2"},
         true, true, false, true, false, true, false},
        {"AMBER TERMINAL", {"#0b0600", "#2a1700", "#a35a00", "#1c0f00", "#6b3c00", "#140a00", "#ffb000", "#ffd27a"},
         true, true, true, true, true, false, false},
        {"ICE PALACE", {"#0a2a4a", "#3f7fb5", "#d9f3ff", "#7fa8c9", "#e8f6ff", "#a7c7e0", "#ffffff", "#5fd0ff"},
         false, false, false, false, false, false, true},
        {"LAVA FIELDS", {"#1a0505", "#5a0f0a", "#ff6a1a", "#2a0a06", "#c0300f", "#1e0704", "#ffd04a", "#ff8a3a"},
         false, true, false, false, false, true, false},
        {"C64 DREAMS", {"#352879", "#6c5eb5", "#70a4b2", "#352879", "#6c5eb5", "#2a1f66", "#b8c76f", "#9ad284"},
         false, false, false, true, false, false, false},
    }};
    return list;
}

const Vec Light = normalized({-0.5, 0.85, -0.4});
const char *const ScrollText =
    "   *** OMAWING ***   GREETINGS TO THE DREAMERS, CODERS & PIXEL PUSHERS   //"
    "   OMARCHY: BEAUTIFUL, MODERN & OPINIONATED   //   OMAKASE ARCH, SERVED ON HYPRLAND   //"
    "   SUPER + RETURN SUMMONS A TERMINAL   //   SUPER + SPACE OPENS THE OMARCHY MENU   //"
    "   KEYBOARD FIRST, MOUSE OPTIONAL   //   ONE THEME SWITCH AND THE WHOLE DESKTOP FOLLOWS   //"
    "   TILES ALL THE WAY DOWN   //   NO DISKS REQUIRED   //   ALL VECTORS, ALL VIBES   //"
    "   FLY THROUGH THE RINGS AND CHASE THE HIGH SCORE!   ";

// The soft sprites particles are drawn with: the exhaust's cooling ramp,
// then a spark, two glitters, and smoke.
constexpr int ExhaustSprites = 8, SparkSprite = 8, GlitterSprite = 9, SmokeSprite = 11;

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
QFont hudFont(double pixelSize, double spacing, const QString &text, int weight = QFont::Normal) {
    for (const QChar c : text) {
        const QChar::Script script = c.script();
        if (script != QChar::Script_Latin && script != QChar::Script_Greek && script != QChar::Script_Cyrillic &&
            script != QChar::Script_Common && script != QChar::Script_Inherited)
            return monoFont(pixelSize, 0, weight);
    }
    return monoFont(pixelSize, spacing, weight);
}

// Draws text with its end at x, like SVG's text-anchor="end".
void drawEnd(QPainter &p, double x, double y, const QString &text) {
    p.drawText(QPointF(x - QFontMetricsF(p.font()).horizontalAdvance(text), y), text);
}
void drawCentred(QPainter &p, double x, double y, const QString &text) {
    p.drawText(QPointF(x - QFontMetricsF(p.font()).horizontalAdvance(text) / 2, y), text);
}

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
} // namespace

QString Scene::actName(int act, const QString &themeName) {
    const int index = std::clamp(act, 0, Flight::Acts - 1);
    if (index == ThemeAct && !themeName.trimmed().isEmpty())
        return themeName.trimmed().toUpper();
    return acts()[index].name;
}

std::array<QColor, 8> Scene::themeColors(const QHash<QString, QString> &colors) {
    auto pick = [&](std::initializer_list<const char *> keys, const char *fallback) {
        for (const char *key : keys) {
            const QColor c(colors.value(key));
            if (c.isValid())
                return vecOf(c);
        }
        return hex(fallback);
    };
    const Vec background = pick({"background"}, "#1a1b26"), foreground = pick({"foreground"}, "#a9b1d6");
    const Vec accent = pick({"accent", "blue"}, "#7aa2f7"), magenta = pick({"magenta", "bright_magenta"}, "#ad8ee6");
    // Light themes swap background and foreground, but the sky stays dark
    // at the top so the HUD reads.
    auto luminance = [](const Vec &c) { return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; };
    const bool lightTheme = luminance(background) > luminance(foreground);
    const Vec dark = lightTheme ? foreground : background, light = lightTheme ? background : foreground;
    const Vec black{0, 0, 0};
    return {rgb(blend(dark, black, 0.3)), rgb(blend(dark, accent, 0.4)), rgb(blend(accent, light, 0.4)),
            rgb(blend(dark, accent, 0.28)), rgb(blend(accent, light, 0.2)), rgb(blend(dark, black, 0.15)),
            rgb(accent), rgb(magenta)};
}

Scene::Palette Scene::paletteOf(int act) const {
    std::array<Vec, 8> c;
    for (int i = 0; i < 8; ++i)
        c[i] = act == ThemeAct && m_look.theme[i].isValid() ? vecOf(m_look.theme[i]) : hex(acts()[act].colors[i]);
    return {c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7]};
}

void Scene::begin(const Flight &flight, const QSizeF &size, const Look &look) {
    m_flight = &flight;
    m_look = look;
    const double aspect = size.height() > 0 ? size.width() / size.height() : 16.0 / 9;
    m_width = std::max(MinWidth, BaseHeight * aspect);
    m_height = m_width / aspect;
    m_act = flight.act();

    // Colours blend over the first two seconds of each act.
    const Palette previous = paletteOf((m_act + Flight::Acts - 1) % Flight::Acts), next = paletteOf(m_act);
    const double t = flight.time < 2 ? 1 : smooth(clampTo(std::fmod(flight.time, Flight::ActLength) / 2, 0, 1));
    m_palette = {blend(previous.top, next.top, t), blend(previous.mid, next.mid, t),
                 blend(previous.horizon, next.horizon, t), blend(previous.floor, next.floor, t),
                 blend(previous.tileA, next.tileA, t), blend(previous.tileB, next.tileB, t),
                 blend(previous.accent, next.accent, t), blend(previous.crystal, next.crystal, t)};

    // A hit shakes the camera, hard at first and dying away.
    m_camera = flight.camera;
    if (m_camera.shake > 0) {
        const double amount = m_camera.shake * m_camera.shake, time = flight.time;
        m_camera.x += amount * 0.45 * std::sin(time * 53);
        m_camera.y += amount * 0.35 * std::sin(time * 61 + 1);
        m_camera.yaw += amount * 0.02 * std::sin(time * 47 + 2);
        m_camera.pitch += amount * 0.02 * std::sin(time * 59 + 3);
    }
    m_forward = {std::sin(m_camera.yaw) * std::cos(m_camera.pitch), std::sin(m_camera.pitch),
                 std::cos(m_camera.yaw) * std::cos(m_camera.pitch)};
    m_right = {std::cos(m_camera.yaw), 0, -std::sin(m_camera.yaw)};
    m_up = cross(m_forward, m_right);
    m_faces.clear();
}

Vec Scene::toCamera(const Vec &v) const {
    const Vec d{v[0] - m_camera.x, v[1] - m_camera.y, v[2] - m_camera.z};
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
    // Most polygons are entirely inside or outside. Only allocate clipping
    // buffers for the few that actually cross a frustum plane.
    unsigned outsideAny = 0, outsideAll = 31;
    for (const Vec &p : polygon) {
        unsigned code = 0;
        for (int plane = 0; plane < 5; ++plane)
            if (distance(plane, p) < 0) code |= 1u << plane;
        outsideAny |= code;
        outsideAll &= code;
    }
    if (outsideAll) return {};
    if (!outsideAny) return polygon;
    std::vector<Vec> out;
    out.reserve(polygon.size() + 5);
    for (int plane = 0; plane < 5 && !polygon.empty(); ++plane) {
        if (!(outsideAny & (1u << plane))) continue;
        out.clear();
        for (size_t i = 0; i < polygon.size(); ++i) {
            const Vec &a = polygon[i], &b = polygon[(i + 1) % polygon.size()];
            const double da = distance(plane, a), db = distance(plane, b);
            if (da >= 0)
                out.push_back(a);
            if ((da >= 0) != (db >= 0))
                out.push_back(blend(a, b, da / (da - db)));
        }
        polygon.swap(out);
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

void Scene::face(const std::vector<Vec> &points, const QColor &fill, const QColor &stroke, double opacity, bool ship,
                 bool glow) {
    const auto clipped = clip(points);
    if (clipped.size() < 3)
        return;
    double depth = 0;
    for (const Vec &p : clipped)
        depth += p[2];
    m_faces.push_back({polygonOf(clipped), fill, stroke, opacity, depth / clipped.size(), ship, glow});
}

void Scene::mesh(const Mesh &geometry, const Vec &at, const Vec &rotation, const std::vector<Vec> &colors,
                 const MeshOptions &options) {
    const Vec centreCamera = toCamera(at);
    if (centreCamera[2] < -options.radius || centreCamera[2] > Far + options.radius ||
        std::abs(centreCamera[0]) > centreCamera[2] * m_width / (2 * Focal) + options.radius * 2)
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
        projected.push_back(toCamera(v));
    for (size_t i = 0; i < geometry.faces.size(); ++i) {
        const auto &indices = geometry.faces[i];
        const Vec &v0 = vertices[indices[0]];
        Vec n = normalized(cross(sub(vertices[indices[1]], v0), sub(vertices[indices[2]], v0)));
        if (dot(n, sub(v0, centre)) < 0)
            n = scaled(n, -1);
        // Solid faces turned away from the camera are hidden; wireframes show all edges.
        if (!options.wire && dot(n, sub(v0, {m_camera.x, m_camera.y, m_camera.z})) >= 0)
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
        const double brightness = options.emissive ? 1 : 0.5 + 0.5 * std::max(0.0, dot(n, Light));
        const QColor fill = options.wire ? QColor() : rgb(blend(scaled(source, brightness), m_palette.floor, fog(depth)));
        const QColor stroke = options.wire ? rgb(blend(source, m_palette.floor, fog(depth)))
                            : options.glow ? QColor()
                            : options.stroke.isValid() ? options.stroke : fill;
        face(points, fill, stroke, options.opacity, options.ship, options.glow);
    }
}

void Scene::ring(const Ring &r, double spin) {
    const int count = 12;
    const double radius = World::RingRadius, thickness = 0.18;
    // Dissolve a gate as it nears the camera, before it sweeps through and
    // covers the entire view.
    const double opacity = smooth(clampTo((toCamera({r.x, r.y, r.z})[2] - 13.5) / 7, 0, 1));
    if (opacity <= 0)
        return;
    for (int i = 0; i < count; ++i) {
        const double a = i * 2 * M_PI / count + spin, b = (i + 1) * 2 * M_PI / count + spin, inner = radius - thickness;
        const std::vector<Vec> points{toCamera({r.x + std::cos(a) * radius, r.y + std::sin(a) * radius, r.z}),
                                      toCamera({r.x + std::cos(b) * radius, r.y + std::sin(b) * radius, r.z}),
                                      toCamera({r.x + std::cos(b) * inner, r.y + std::sin(b) * inner, r.z}),
                                      toCamera({r.x + std::cos(a) * inner, r.y + std::sin(a) * inner, r.z})};
        double depth = 0;
        for (const Vec &p : points)
            depth += p[2] / 4;
        face(points, rgb(blend(i % 3 ? m_palette.accent : m_palette.crystal, m_palette.floor, fog(depth))), QColor(),
             opacity, false);
    }
}

void Scene::paintSky(QPainter &p) {
    const double time = m_flight->time, W = m_width;
    const double horizon = m_height / 2 + Focal * std::tan(m_camera.pitch), split = clampTo(horizon, 0, m_height);

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

    const Act &act = acts()[m_act];
    const Vec sunDirection{std::sin(0.44) * std::cos(0.1), std::sin(0.1), std::cos(0.44) * std::cos(0.1)};
    const Vec sun{dot(sunDirection, m_right), dot(sunDirection, m_up), dot(sunDirection, m_forward)};
    if (sun[2] > 0.15) {
        const Vec sunColor = blend(m_palette.accent, hex("#ffd098"), 0.55);
        p.save();
        p.translate(project(sun));
        p.setOpacity(act.dimSun ? 0.2 : 0.85);
        QRadialGradient halo(0, 0, 150);
        halo.setColorAt(0, withAlpha(rgb(blend(sunColor, hex("#ffb26c"), 0.5)), 0.4));
        halo.setColorAt(1, withAlpha(rgb(blend(sunColor, hex("#ffb26c"), 0.5)), 0));
        p.fillRect(QRectF(-150, -150, 300, 300), halo);
        for (int i = 0; i < 10; ++i) {
            const double y = -68 + i * 14, half = std::sqrt(std::max(0.0, 70.0 * 70 - (y + 5) * (y + 5)));
            p.fillRect(QRectF(-half, y, half * 2, i < 4 ? 14 : 10 - (i - 4)), rgb(sunColor));
        }
        p.restore();
    }

    paintBackdrop(p, horizon);

    // The camera never rolls, so ground depth depends only on the screen row:
    // the dark squares, fogged with distance, are one vertical gradient, and
    // only the light squares are drawn as polygons over it.
    if (split < m_height) {
        QLinearGradient ground(0, split, 0, m_height);
        ground.setColorAt(0, rgb(m_palette.floor));
        for (double d = Far; d >= 3; d *= 0.8) {
            const Vec c = toCamera({m_camera.x + std::sin(m_camera.yaw) * d, 0, m_camera.z + std::cos(m_camera.yaw) * d});
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

// Clouds and three ranges of hills, each sliding at its own speed: the
// nearer a layer, the further it moves when the camera turns or drifts
// sideways. Each layer wraps once per full turn.
void Scene::paintBackdrop(QPainter &p, double horizon) {
    const double W = m_width, time = m_flight->time;
    const Act &act = acts()[m_act];

    struct Clouds {
        double turn, slide, wind, low, high, opacity;
        int count;
    };
    static const Clouds clouds[] = {{1.4, 1.5, 5, 70, 130, 0.2, 9}, {2.4, 3.6, 13, 130, 240, 0.28, 8}};
    for (int layer = 0; layer < 2; ++layer) {
        const Clouds &c = clouds[layer];
        const double period = 2 * M_PI * Focal * c.turn;
        const double scroll = m_camera.yaw * Focal * c.turn + m_camera.drift * c.slide + time * c.wind;
        p.setOpacity(c.opacity * (act.dimSun ? 0.45 : 1));
        const QColor color = rgb(blend(m_palette.horizon, hex("#fff3e0"), 0.25 + layer * 0.2));
        for (int i = 0; i < c.count; ++i) {
            const double width = 80 + hash(i, layer + 30) * 170, height = 6 + hash(i, layer + 40) * 8;
            const double y = horizon - mix(c.low, c.high, hash(i, layer + 50));
            double x = std::fmod((i + hash(i, layer + 20) * 0.6) / c.count * period - scroll, period);
            if (x < 0)
                x += period;
            for (const double at : {x, x - period}) {
                if (at + width < 0 || at > W)
                    continue;
                QPainterPath cloud;
                cloud.addRoundedRect(QRectF(at, y, width, height), height / 2, height / 2);
                cloud.addRoundedRect(QRectF(at + width * 0.18, y - height * 0.7, width * 0.42, height * 1.2), height * 0.6,
                                     height * 0.6);
                cloud.setFillRule(Qt::WindingFill);
                p.fillPath(cloud, color);
            }
        }
    }
    p.setOpacity(1);

    struct Ridge {
        double turn, slide, sink, spacing, low, high;
        bool rounded;
    };
    static const Ridge ridges[] = {
        {1.0, 0.4, 0.15, 22, 26, 86, false},   // Far mountains, nearly at infinity.
        {1.8, 2.2, 0.7, 34, 14, 52, false},
        {3.0, 6, 1.8, 54, 6, 28, true},        // Rolling hills close by.
    };
    const Vec colors[] = {blend(blend(m_palette.floor, m_palette.mid, 0.5), m_palette.horizon, 0.45),
                          blend(m_palette.floor, m_palette.mid, 0.5), blend(m_palette.floor, m_palette.tileB, 0.5)};
    for (int layer = 0; layer < 3; ++layer) {
        const Ridge &r = ridges[layer];
        const double period = 2 * M_PI * Focal * r.turn;
        const int count = std::max(8, int(std::lround(period / r.spacing)));
        const double step = period / count;
        const double scroll = m_camera.yaw * Focal * r.turn + m_camera.drift * r.slide;
        // Flying higher lets the nearer hills sink below the horizon.
        const double base = horizon + std::max(0.0, m_camera.y - 2) * r.sink;
        const int first = int(std::floor(scroll / step)) - 1, points = int(std::ceil(W / step)) + 3;
        auto peak = [&](int g) {
            const int index = ((g % count) + count) % count;
            return QPointF(g * step - scroll, base - mix(r.low, r.high, hash(index, 60 + layer)));
        };
        QPainterPath path(QPointF(first * step - scroll, base + 40));
        path.lineTo(peak(first));
        for (int j = 1; j <= points; ++j) {
            const QPointF here = peak(first + j);
            if (r.rounded) {
                const QPointF before = peak(first + j - 1);
                path.quadTo(before, (before + here) / 2);
            } else {
                path.lineTo(here);
            }
        }
        path.lineTo((first + points) * step - scroll, base + 40);
        path.closeSubpath();
        p.fillPath(path, rgb(colors[layer]));
    }
}

namespace {
const std::vector<Vec> HullVertices{{0, 0, 2.8}, {-0.65, 0, -1.6}, {0.65, 0, -1.6}, {0, 0.65, -0.4}, {0, -0.4, -0.6}};
const std::vector<std::vector<int>> HullFaces{{0, 1, 3}, {0, 3, 2}, {1, 2, 3}, {0, 4, 1}, {0, 2, 4}, {1, 4, 2}};

// Each wing is an inner panel and an outer one, in the ship's own frame.
// Flex lifts the tip most and the middle a little, so the wing bends rather
// than hinges; sweep folds the tips back.
std::array<std::vector<Vec>, 2> wingPanels(const Ship &ship, double side) {
    const double flex = side < 0 ? ship.flexLeft : ship.flexRight;
    const Vec rootFront{0.5 * side, 0, 1}, rootRear{0.55 * side, 0.05, -1.2};
    const Vec tip{(3.3 - 0.55 * ship.sweep) * side, -0.12 + flex * 2.6, -1.9 - 1.1 * ship.sweep};
    Vec midFront = blend(rootFront, tip, 0.5), midRear = blend(rootRear, tip, 0.5);
    midFront[1] = mix(rootFront[1], -0.12, 0.5) + flex * 0.75;
    midRear[1] = mix(rootRear[1], -0.12, 0.5) + flex * 0.75;
    return {std::vector<Vec>{rootFront, midFront, midRear, rootRear}, std::vector<Vec>{midFront, tip, midRear}};
}
}

void Scene::paintGround(QPainter &p) {
    // The checkerboard reaches past the end of the fog, so its edge is never
    // seen and rows don't pop in as the camera crosses a tile.
    const int step = 6, extent = int(std::ceil(Far / step)) + 2;
    const int gx = int(std::floor(m_camera.x / step)), gz = int(std::floor(m_camera.z / step));
    const bool gridLines = acts()[m_act].grid;
    std::vector<std::vector<Vec>> grid(extent * 2 + 1, std::vector<Vec>(extent * 2 + 1));
    for (int j = -extent; j <= extent; ++j)
        for (int i = -extent; i <= extent; ++i)
            grid[j + extent][i + extent] = toCamera({double(gx + i) * step, 0, double(gz + j) * step});
    // Group tiles by fog colour and use the convex-polygon fast path. General
    // paths require stencil passes on the GPU even for these simple tiles.
    // The fog steps are fine enough that a tile moving between them doesn't
    // visibly change colour.
    constexpr int FogSteps = 48, LineBands = 8;
    std::array<std::vector<QPolygonF>, FogSteps> tiles;
    std::array<QPainterPath, LineBands> lines;
    for (int j = 0; j < extent * 2; ++j)
        for (int i = 0; i < extent * 2; ++i) {
            const std::vector<Vec> points{grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i]};
            double depth = 0;
            for (const Vec &q : points)
                depth += q[2] / 4;
            const int parity = (((gx + gz + i + j) % 2) + 2) % 2;
            const double haze = fog(std::max(0.0, depth));
            if (depth < -5 || haze >= 0.995 || (!parity && !gridLines))
                continue;
            const auto clipped = clip(points);
            if (clipped.size() < 3)
                continue;
            const QPolygonF polygon = polygonOf(clipped);
            if (parity)
                tiles[std::min(FogSteps - 1, int(haze * FogSteps))].push_back(polygon);
            if (gridLines) {
                // Lines fade into the fog band by band instead of stopping.
                QPainterPath &band = lines[std::min(LineBands - 1, int(haze * LineBands))];
                band.addPolygon(polygon);
                band.closeSubpath();
            }
        }
    for (int i = 0; i < FogSteps; ++i) {
        const double haze = (i + 0.5) / FogSteps;
        p.setPen(Qt::NoPen);
        p.setBrush(rgb(blend(m_palette.tileA, m_palette.floor, haze)));
        for (const QPolygonF &polygon : tiles[i]) p.drawConvexPolygon(polygon);
    }
    const QPen linePen(rgb(blend(m_palette.accent, m_palette.tileA, 0.4)), 0.55);
    for (int i = 0; i < LineBands; ++i) {
        if (lines[i].isEmpty())
            continue;
        p.setOpacity(0.25 * (1 - (i + 0.5) / LineBands));
        p.strokePath(lines[i], linePen);
    }
    p.setOpacity(1);

    // The ship's shadow: its hull and wings flattened straight down onto the
    // ground, so it banks, flexes, and sweeps with the ship. Every piece is
    // wound the same way and filled as one path, so overlaps don't darken.
    const auto &ship = m_flight->ship;
    const Vec at{ship.x, ship.y, ship.z}, rotation{ship.yaw, ship.pitch, ship.bank};
    std::vector<std::vector<Vec>> pieces;
    for (const auto &indices : HullFaces) {
        std::vector<Vec> piece;
        for (int index : indices)
            piece.push_back(HullVertices[index]);
        pieces.push_back(std::move(piece));
    }
    for (const double side : {-1.0, 1.0})
        for (const auto &panel : wingPanels(ship, side))
            pieces.push_back(panel);
    QPainterPath shadow;
    shadow.setFillRule(Qt::WindingFill);
    for (const auto &piece : pieces) {
        std::vector<Vec> flat;
        for (const Vec &v : piece) {
            Vec world = translated(rotate(v, rotation), at);
            world[1] = 0.03;
            flat.push_back(toCamera(world));
        }
        const auto clipped = clip(flat);
        if (clipped.size() < 3)
            continue;
        QPolygonF polygon = polygonOf(clipped);
        double area = 0;
        for (int i = 0; i < polygon.size(); ++i) {
            const QPointF &a = polygon[i], &b = polygon[(i + 1) % polygon.size()];
            area += a.x() * b.y() - b.x() * a.y();
        }
        if (std::abs(area) < 1e-3)
            continue;
        if (area < 0)
            std::reverse(polygon.begin(), polygon.end());
        shadow.addPolygon(polygon);
        shadow.closeSubpath();
    }
    if (!shadow.isEmpty()) {
        p.setOpacity(clampTo(0.55 - ship.y * 0.025, 0.12, 0.5));
        p.fillPath(shadow, QColor("#12102a"));
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

    const Act &act = acts()[m_act];
    const double time = m_flight->time;
    for (const Obstacle &o : World::scenery(m_camera.x, m_camera.z, 4, time)) {
        if (std::hypot(o.x - m_camera.x, o.z - m_camera.z) > Far)
            continue;
        if (o.kind == Obstacle::Pyramid)
            // Wireframes are drawn in the bright highlights: the sky's dark middle
            // colour that shades solid faces vanishes against the ground.
            mesh(pyramid, {o.x, o.y, o.z}, {0, 0, 0},
                 act.wirePyramids ? std::vector<Vec>{m_palette.accent, m_palette.crystal} : std::vector<Vec>{m_palette.mid, m_palette.crystal},
                 {.wire = act.wirePyramids, .radius = 9});
        else
            mesh(crystal, {o.x, o.y, o.z}, {o.spin, 0, 0}, {m_palette.crystal, m_palette.accent},
                 {.wire = act.wireCrystals, .radius = 6});
    }
    // Rings already flown through are gone; the rest stand along the course
    // in both directions.
    const int first = int(std::floor((m_camera.z - Far - 24) / World::RingSpacing));
    const int last = int(std::ceil((m_camera.z + Far - 24) / World::RingSpacing));
    for (int i = first; i <= last; ++i) {
        const Ring r = World::ring(i);
        if (!m_flight->collected.count(i) && std::hypot(r.x - m_camera.x, r.z - m_camera.z) < Far)
            ring(r, act.spinRings ? time * 0.15 : 0);
    }
    const Obstacle ball = World::ball(m_flight->ship.z, time);
    mesh(sphere, {ball.x, ball.y, ball.z}, {ball.spin, 0, 0.25}, ballColors, {.wire = act.wireBall, .radius = 5});
    if (act.floater)
        mesh(crystal, {-12, 8 + std::sin(time), m_flight->ship.z + 45}, {time * 0.7, time * 0.3, 0}, {m_palette.crystal},
             {.wire = true, .radius = 6});
}

void Scene::buildShip() {
    static const Mesh hull{HullVertices, HullFaces};
    static const Mesh canopy{{{0, 0.13, 1.65}, {-0.34, 0.29, -0.2}, {0.34, 0.29, -0.2}, {0, 0.76, -0.35}},
                             {{0, 1, 3}, {0, 3, 2}, {1, 2, 3}}};
    static const Mesh flame{{{-0.28, 0.22, -1.6}, {0.28, 0.22, -1.6}, {0.28, -0.2, -1.6}, {-0.28, -0.2, -1.6}, {0, 0, -3.6}},
                            {{0, 1, 4}, {1, 2, 4}, {2, 3, 4}, {3, 0, 4}}};

    const auto &ship = m_flight->ship;
    const Vec at{ship.x, ship.y, ship.z}, rotation{ship.yaw, ship.pitch, ship.bank};
    // After a hit the ship flickers until it can be hurt again.
    const double opacity = m_flight->hurt > 0 && std::fmod(m_flight->hurt, 0.2) < 0.1 ? 0.35 : 1;
    mesh(hull, at, rotation, {hex("#e8e8dc"), hex("#92a9b7"), hex("#59638d")},
         {.ship = true, .stroke = QColor("#dfd6cd"), .radius = 5, .opacity = opacity});

    auto place = [&](const Vec &v) { return toCamera(translated(rotate(v, rotation), at)); };
    for (const double side : {-1.0, 1.0}) {
        const auto panels = wingPanels(ship, side);
        const char *const colors[] = {"#e4b3ab", "#eec3b6"};
        for (int i = 0; i < 2; ++i) {
            const auto &panel = panels[i];
            // The wings are thin and double-sided, lit from whichever side faces the light.
            const Vec n = normalized(cross(sub(rotate(panel[1], rotation), rotate(panel[0], rotation)),
                                           sub(rotate(panel[2], rotation), rotate(panel[0], rotation))));
            std::vector<Vec> points;
            for (const Vec &v : panel)
                points.push_back(place(v));
            face(points, rgb(scaled(hex(colors[i]), 0.7 + 0.3 * std::abs(dot(n, Light)))), QColor("#f8dfc1"), opacity, true);
        }
        face({place({0.45 * side, 0.1, -1.45}), place({0.75 * side, 1, -1.9}), place({0.7 * side, 0.1, -0.65})},
             QColor("#cf557a"), QColor("#f8dfc1"), opacity, true);
    }
    mesh(canopy, at, rotation, {hex("#7de6e1"), hex("#398da8"), hex("#286285")},
         {.emissive = true, .ship = true, .stroke = QColor("#baf8e6"), .radius = 5, .opacity = opacity});

    // The flames are see-through light: a long cone, and a short white-hot
    // core inside it. The particles do the rest.
    if (m_flight->wrecked >= 0)
        return;
    const double thrust = 1 + (ship.speed - 13) / 18, flicker = std::sin(m_flight->time * 28);
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

void Scene::buildParticles() {
    for (const Particle &particle : m_flight->particles) {
        const Vec c = toCamera(particle.position);
        if (c[2] < Near + 0.4 || c[2] > Far)
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
        opacity *= clampTo((c[2] - Near - 0.4) / 2, 0, 1) * (1 - fog(c[2]));
        const QPointF at = project(c);
        const double r = std::min(90.0, Focal * size / c[2]);
        if (opacity <= 0.01 || at.x() + r < 0 || at.x() - r > m_width || at.y() + r < 0 || at.y() - r > m_height)
            continue;
        Face f;
        f.opacity = opacity;
        f.depth = c[2];
        f.glow = glow;
        f.sprite = sprite;
        f.box = QRectF(at.x() - r, at.y() - r, r * 2, r * 2);
        m_faces.push_back(std::move(f));
    }
}

void Scene::preparePuffs() {
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
void Scene::paintWorld(QPainter &p) {
    std::stable_sort(m_faces.begin(), m_faces.end(), [](const Face &a, const Face &b) { return a.depth > b.depth; });
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    for (const Face &f : m_faces) {
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
    p.drawImage(QPointF(std::round(at.x()), std::round(at.y())), sprite.image, sprite.source);
    p.restore();
}

void Scene::blitScaled(QPainter &p, const Sprite &sprite, const QPointF &anchor, double scale) const {
    const QSizeF size = (sprite.source.isEmpty() ? sprite.image.size() : sprite.source.size()).toSizeF();
    p.drawImage(QRectF(anchor + sprite.offset * scale, size * (scale / m_pixelsPerUnit)), sprite.image,
                sprite.source.isEmpty() ? QRectF(sprite.image.rect()) : QRectF(sprite.source));
}

// Every letter the scroller and the ring points can show, drawn up front into
// one atlas. Drawing each letter the first time it scrolled in, and uploading
// it to the GPU, made a small hitch every time a new one appeared.
void Scene::prepareGlyphs() {
    m_glyphs.clear();
    QString letters = QString::fromLatin1(ScrollText) + "+0123456789";
    std::vector<std::pair<QChar, Sprite>> parts;
    for (const QChar c : letters)
        if (c != ' ' && std::none_of(parts.begin(), parts.end(), [c](const auto &part) { return part.first == c; }))
            parts.emplace_back(c, glyph(c));
    m_glyphs.clear();
    m_glyphAdvance = QFontMetricsF(monoFont(26, 0, QFont::Black)).horizontalAdvance('0');
    const int rowWidth = 1024;
    int x = 0, y = 0, rowHeight = 0, width = 1;
    std::vector<QPoint> places;
    for (const auto &[c, sprite] : parts) {
        if (x + sprite.image.width() > rowWidth) {
            x = 0;
            y += rowHeight + 1;
            rowHeight = 0;
        }
        places.emplace_back(x, y);
        x += sprite.image.width() + 1;
        width = std::max(width, x);
        rowHeight = std::max(rowHeight, sprite.image.height());
    }
    QImage atlas(width, std::max(1, y + rowHeight), QImage::Format_ARGB32_Premultiplied);
    atlas.fill(Qt::transparent);
    {
        QPainter p(&atlas);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        for (size_t i = 0; i < parts.size(); ++i)
            p.drawImage(places[i], parts[i].second.image);
    }
    for (size_t i = 0; i < parts.size(); ++i)
        m_glyphs.insert(parts[i].first, {atlas, parts[i].second.offset, QRect(places[i], parts[i].second.image.size())});
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
    preparePuffs();
    if (std::abs(pixelsPerUnit - m_pixelsPerUnit) < 1e-6 && std::abs(m_preparedWidth - m_width) < 1e-6 &&
        std::abs(m_preparedHeight - m_height) < 1e-6)
        return;
    const bool rescaled = std::abs(pixelsPerUnit - m_pixelsPerUnit) >= 1e-6;
    m_pixelsPerUnit = pixelsPerUnit;
    m_preparedWidth = m_width;
    m_preparedHeight = m_height;
    if (!rescaled)
        return;
    // A dark line one unit tall every four units. Tiling a tiny pattern reads
    // far less memory each frame than a window-sized image, and nothing needs
    // redrawing when the window is resized.
    const int period = std::max(2, int(std::lround(4 * pixelsPerUnit)));
    m_scanlines = QImage(1, period, QImage::Format_ARGB32_Premultiplied);
    m_scanlines.fill(Qt::transparent);
    for (int y = period - std::max(1, int(std::lround(pixelsPerUnit))); y < period; ++y)
        m_scanlines.setPixelColor(0, y, withAlpha("#090f29", 0.14));
    prepareGlyphs();
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
}

void Scene::paintOverlay(QPainter &p, const Hud &hud) {
    const double time = m_flight->time, W = m_width, boost = m_flight->boostAmount();
    // The gauges, status, and scroller stay pinned to the bottom.
    const double b = m_height - BaseHeight;
    const QLocale locale;

    // Speed lines streak outwards while boosting.
    for (int i = 0; i < 18; ++i) {
        const double a = i / 18.0 * M_PI * 2 + 0.05 * time;
        const double length = 24 + hash(i, 4) * 70, r = 200 + std::fmod(time * 170 + i * 39, 340);
        const QPointF from(W / 2 + std::cos(a) * r, m_height / 2 + 10 + std::sin(a) * r * 0.6);
        p.setOpacity(boost * 0.35 + (acts()[m_act].dimSun ? 0.16 : 0));
        p.setPen(QPen(QColor("#a3e5f4"), 1.2));
        p.drawLine(from, from + QPointF(std::cos(a) * length, std::sin(a) * length * 0.6));
    }
    p.setOpacity(1);

    // Points float up from the rings they were won at.
    for (const Popup &popup : m_flight->popups) {
        const Vec c = toCamera(popup.position);
        if (c[2] < 2)
            continue;
        const QPointF at = project(c);
        p.setOpacity(clampTo(1 - popup.age / 1.4, 0, 1));
        // In the scroller's chrome letters, which are ready in the atlas;
        // text at a new size each frame would make the GPU cache new glyphs.
        const QString text = "+" + QString::number(popup.points);
        const double scale = clampTo(Focal * 1.1 / c[2], 12, 34) / 26, advance = m_glyphAdvance * scale;
        for (int i = 0; i < text.size(); ++i)
            blitScaled(p, glyph(text[i]), {at.x() + (i - (text.size() - 1) / 2.0) * advance, at.y()}, scale);
    }
    p.setOpacity(1);

    // Scanlines, like a CRT.
    p.save();
    const QRectF screen = p.worldTransform().mapRect(QRectF(0, 0, W, m_height));
    p.resetTransform();
    p.fillRect(screen, QBrush(m_scanlines));
    p.restore();
    blit(p, m_title, {26, 59});

    p.setPen(QColor("#f4cfb2"));
    p.setFont(hudFont(10, 3.5, hud.subtitle));
    p.drawText(QPointF(30, 80), hud.subtitle);
    p.setPen(QColor("#ffe2b3"));
    p.setFont(monoFont(13, 2));
    drawEnd(p, W - 30, 36, QString("%1 / %2").arg(m_act + 1, 2, 10, QChar('0')).arg(actName(m_act, m_look.themeName)));
    p.setPen(QColor("#d3bdd5"));
    p.setFont(hudFont(10, 1.6, hud.mode));
    drawEnd(p, W - 30, 59, hud.mode);
    for (int i = 0; i < Flight::Acts; ++i)
        p.fillRect(QRectF(W - 30 - (Flight::Acts - i) * 15 + 3, 74, 12, 3), i == m_act ? rgb(m_palette.accent) : QColor("#65506f"));

    // The score, top centre: the current run while flying, the best always.
    if (!hud.points.isEmpty()) {
        p.setPen(QColor("#e2c5cf"));
        p.setFont(hudFont(9, 3, hud.score));
        drawCentred(p, W / 2, 26, hud.score);
        p.setPen(QColor("#ffe2b3"));
        p.setFont(monoFont(24, 3, QFont::Black));
        drawCentred(p, W / 2, 52, hud.points);
        if (m_flight->multiplier > 1) {
            p.setPen(rgb(m_palette.accent));
            p.setFont(monoFont(14, 1, QFont::Bold));
            p.drawText(QPointF(W / 2 + QFontMetricsF(monoFont(24, 3, QFont::Black)).horizontalAdvance(hud.points) / 2 + 8, 50),
                       QString::fromUtf8("×") + locale.toString(m_flight->multiplier));
        }
    }
    if (!hud.best.isEmpty()) {
        p.setPen(QColor("#d3bdd5"));
        p.setFont(hudFont(10, 1.6, hud.best));
        drawCentred(p, W / 2, hud.points.isEmpty() ? 36 : 70, hud.best);
    }

    // The hull, while you fly: green, then amber, then red, blinking after a hit.
    if (!hud.hull.isEmpty()) {
        const double hull = m_flight->hull / Flight::FullHull;
        const QColor color = hull > 0.5 ? QColor("#9be8a5") : hull > 0.25 ? QColor("#ffd166") : QColor("#ff6b6b");
        p.setPen(QColor("#e2c5cf"));
        p.setFont(hudFont(9, 2, hud.hull));
        p.drawText(QPointF(30, b + 389), hud.hull);
        p.fillRect(QRectF(30, b + 398, 94, 4), QColor("#2e2544"));
        if (m_flight->hurt <= 0 || std::fmod(m_flight->hurt, 0.2) >= 0.1)
            p.fillRect(QRectF(30, b + 398, 94 * hull, 4), color);
    }

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

    // A big chrome message across the middle when the hull gives out.
    if (!hud.alert.isEmpty() && std::fmod(time, 0.6) < 0.42) {
        QPainterPath text;
        const QFont font = hudFont(34, 5, hud.alert, QFont::Black);
        text.addText(0, 0, font, hud.alert);
        const QRectF box = text.boundingRect();
        text.translate(W / 2 - box.center().x(), m_height * 0.36 - box.center().y());
        p.setOpacity(0.85);
        p.fillPath(text.translated(2, 4), QColor("#211638"));
        p.setOpacity(1);
        p.fillPath(text, chrome(text.boundingRect()));
    }

    if (!m_look.banner)
        return;
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

int Scene::paint(QPainter &painter, const Flight &flight, const QSizeF &size, const Hud &hud, const Look &look) {
    begin(flight, size, look);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(size.width() / m_width, size.width() / m_width);
    painter.setClipRect(QRectF(0, 0, m_width, m_height));
    prepare(painter.worldTransform().m11());
    paintSky(painter);
    paintGround(painter);
    buildScenery();
    buildShip();
    buildParticles();
    paintWorld(painter);
    paintOverlay(painter, hud);
    painter.restore();
    return int(m_faces.size());
}
