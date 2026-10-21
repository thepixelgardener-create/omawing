#include "view.h"

#include <algorithm>
#include <cmath>

using namespace geo;

namespace draw {
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

const Vec &light() {
    static const Vec direction = normalized({-0.5, 0.85, -0.4});
    return direction;
}
} // namespace draw

using namespace draw;

const View::Act &View::actAt(int act) {
    static const std::array<Act, Flight::Acts> list{{
        {"COPPER COAST", {"#100d33", "#62276f", "#e99489", "#5e3b67", "#e9afab", "#38284f", "#ffd093", "#80e3df"},
         false, false, false, false, false, false, false},
        {"VECTOR GARDEN", {"#091b32", "#163f5f", "#67b7b2", "#1a3e53", "#5e9f9c", "#122f49", "#9bf4ca", "#e3b5f5"},
         true, false, false, false, false, false, true},
        {"STAR RUN", {"#070c22", "#1d2056", "#7b528b", "#282144", "#705878", "#151b35", "#ffa1bc", "#91d5fa"},
         true, true, true, true, true, true, false},
        // View::ThemeAct: these colours stand in until the Omarchy theme is read.
        {"OMARCHY", {"#0e0f17", "#3b4a7a", "#a3b8f0", "#2f3858", "#8fa4e0", "#14151f", "#7aa2f7", "#ad8ee6"},
         false, false, false, true, false, false, false},
        {"NEON MIRAGE", {"#12002b", "#4b0f6b", "#ff3fa4", "#171538", "#242044", "#191831", "#2ee6ff", "#ff5fd2"},
         false, false, false, true, false, false, false},
        {"AMBER TERMINAL", {"#0b0600", "#2a1700", "#a35a00", "#1c0f00", "#6b3c00", "#140a00", "#ffb000", "#ffd27a"},
         true, true, true, true, true, false, false},
        {"ICE PALACE", {"#0a2a4a", "#3f7fb5", "#d9f3ff", "#7fa8c9", "#e8f6ff", "#a7c7e0", "#ffffff", "#5fd0ff"},
         false, false, false, false, false, false, true},
        {"LAVA FIELDS", {"#1a0505", "#5a0f0a", "#ff6a1a", "#2a0a06", "#c0300f", "#1e0704", "#ffd04a", "#ff8a3a"},
         false, true, false, false, false, true, false},
        {"C64 DREAMS", {"#352879", "#6c5eb5", "#70a4b2", "#352879", "#6c5eb5", "#2a1f66", "#b8c76f", "#9ad284"},
         false, false, false, true, false, false, false},
    }};
    return list[std::clamp(act, 0, Flight::Acts - 1)];
}

View::Palette View::paletteOf(int act) const {
    std::array<Vec, 8> c;
    for (int i = 0; i < 8; ++i)
        c[i] = act == ThemeAct && look.theme[i].isValid() ? vecOf(look.theme[i]) : hex(actAt(act).colors[i]);
    return {c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7]};
}

void View::begin(const Flight &frame, const QSizeF &size, const SceneLook &wanted) {
    flight = &frame;
    look = wanted;
    const double aspect = size.height() > 0 ? size.width() / size.height() : 16.0 / 9;
    width = std::max(MinWidth, BaseHeight * aspect);
    height = width / aspect;
    act = frame.act();

    // Colours blend over the first two seconds of each act.
    const Palette previous = paletteOf((act + Flight::Acts - 1) % Flight::Acts), next = paletteOf(act);
    const double t = frame.time < 2 ? 1 : smooth(clampTo(std::fmod(frame.time, Flight::ActLength) / 2, 0, 1));
    palette = {blend(previous.top, next.top, t), blend(previous.mid, next.mid, t),
               blend(previous.horizon, next.horizon, t), blend(previous.floor, next.floor, t),
               blend(previous.tileA, next.tileA, t), blend(previous.tileB, next.tileB, t),
               blend(previous.accent, next.accent, t), blend(previous.crystal, next.crystal, t)};

    valley = mix((act + Flight::Acts - 1) % Flight::Acts == 4 ? 1.0 : 0.0, act == 4 ? 1.0 : 0.0, t);

    // A hit shakes the camera, hard at first and dying away.
    camera = frame.camera;
    camera.pitch += valley * 0.085;
    if (camera.shake > 0) {
        const double amount = camera.shake * camera.shake, time = frame.time;
        camera.x += amount * 0.45 * std::sin(time * 53);
        camera.y += amount * 0.35 * std::sin(time * 61 + 1);
        camera.yaw += amount * 0.02 * std::sin(time * 47 + 2);
        camera.pitch += amount * 0.02 * std::sin(time * 59 + 3);
    }
    forward = {std::sin(camera.yaw) * std::cos(camera.pitch), std::sin(camera.pitch),
               std::cos(camera.yaw) * std::cos(camera.pitch)};
    right = {std::cos(camera.yaw), 0, -std::sin(camera.yaw)};
    up = cross(forward, right);
}

Vec View::toCamera(const Vec &v) const {
    const Vec d{v[0] - camera.x, v[1] - camera.y, v[2] - camera.z};
    return {dot(d, right), dot(d, up), dot(d, forward)};
}

QPointF View::project(const Vec &p) const {
    return {width / 2 + Focal * p[0] / p[2], height / 2 - Focal * p[1] / p[2]};
}

// Clips against the near plane and the four sides of the view, so nothing
// projects behind the camera or to coordinates far outside the window.
std::vector<Vec> View::clip(std::vector<Vec> polygon) const {
    const double sx = width / (2 * Focal), sy = height / (2 * Focal);
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

QPolygonF View::polygonOf(const std::vector<Vec> &points) const {
    QPolygonF polygon;
    polygon.reserve(int(points.size()));
    for (const Vec &p : points)
        polygon << project(p);
    return polygon;
}

double View::fog(double depth) const { return 0.34 * smooth(clampTo((depth - 25) / 1400, 0, 1)); }

double View::fadeIn(double depth) const { return smooth(clampTo((Far - depth) / 18, 0, 1)); }

// A ground tile of size Tile at depth d, seen from height h, is about
// Focal * h * Tile / d^2 canvas units tall on screen.
// Detail falls off steadily with distance: full contrast while a tile is
// at least FullSize tall, none once it is down to GoneSize, and a smooth
// slope between them on a logarithmic scale, so there is no edge.
namespace { constexpr double FullSize = 24, GoneSize = 1.5; }

double View::contrast(double depth) const {
    const double h = std::max(1.0, camera.y), d = std::max(1.0, depth);
    const double size = Focal * h * Tile / (d * d);
    return smooth(clampTo(std::log(size / GoneSize) / std::log(FullSize / GoneSize), 0, 1));
}

double View::floorReach() const { return std::min(700.0, std::sqrt(Focal * std::max(1.0, camera.y) * Tile / GoneSize)); }

// The floor brightens a little towards the horizon, more than objects do:
// the plane's own sense of distance, without any mist.
static double floorAir(const View &view, double depth) {
    return std::max(view.fog(depth), 0.42 * smooth(clampTo((depth - 60) / 2400, 0, 1)));
}

Vec View::floorLight(double depth) const {
    const Vec average = blend(palette.tileA, palette.tileB, 0.5);
    return blend(blend(average, palette.tileA, contrast(depth)), atmosphere(), floorAir(*this, depth));
}

Vec View::floorDark(double depth) const {
    const Vec average = blend(palette.tileA, palette.tileB, 0.5);
    return blend(blend(average, palette.tileB, contrast(depth)), atmosphere(), floorAir(*this, depth));
}

Vec View::atmosphere() const {
    // Keep a trace of each act's sky, but scattered blue light dominates
    // in the distance. This also blends smoothly during scene transitions.
    return blend(blend(palette.horizon, draw::hex("#8ab9e2"), 0.68), draw::hex("#9868a5"), valley);
}
