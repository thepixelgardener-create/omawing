#include "sky.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <algorithm>
#include <cmath>

using namespace geo;
using namespace draw;

namespace Sky {
void paintBackdrop(QPainter &p, const View &view, double horizon);
}

namespace {
// A direction in the sky, at infinity: only the camera's turning moves it.
// Returns false when it is behind the camera.
bool skyPoint(const View &view, double azimuth, double elevation, QPointF *at) {
    const Vec direction{std::sin(azimuth) * std::cos(elevation), std::sin(elevation), std::cos(azimuth) * std::cos(elevation)};
    const Vec c{dot(direction, view.right), dot(direction, view.up), dot(direction, view.forward)};
    if (c[2] <= 0.08)
        return false;
    *at = view.project(c);
    return true;
}

// A soft round glow, as a radial gradient that fades to nothing.
void glow(QPainter &p, const QPointF &at, double radius, const QColor &color, double strength) {
    QRadialGradient g(at, radius);
    g.setColorAt(0, withAlpha(color, strength));
    g.setColorAt(0.4, withAlpha(color, strength * 0.45));
    g.setColorAt(1, withAlpha(color, 0));
    p.fillRect(QRectF(at.x() - radius, at.y() - radius, radius * 2, radius * 2), g);
}
}

void Sky::paint(QPainter &p, const View &view) {
    const double time = view.flight->time, W = view.width;
    const double horizon = view.height / 2 + View::Focal * std::tan(view.camera.pitch), split = clampTo(horizon, 0, view.height);
    const View::Act &act = view.style();
    const Vec &top = view.palette.top, &mid = view.palette.mid, &low = view.palette.horizon;

    // The sky deepens upwards from a warm glow at the horizon.
    QLinearGradient sky(0, std::min(0.0, horizon - 520), 0, std::max(1.0, split));
    sky.setColorAt(0, rgb(top));
    sky.setColorAt(0.38, rgb(blend(top, mid, 0.7)));
    sky.setColorAt(0.72, rgb(blend(mid, low, 0.45)));
    sky.setColorAt(1, rgb(blend(low, hex("#fff0dc"), 0.12)));
    p.fillRect(QRectF(0, 0, W, std::max(1.0, split)), sky);

    // The copper bars, the Amiga's touch, as a faint shimmer near the horizon.
    for (int i = 0; i < 24; ++i) {
        const double y = horizon - 170 + i * 7 + std::sin(time * 0.7 + i * 0.18) * 6;
        p.setOpacity(0.04 + i / 260.0);
        p.fillRect(QRectF(0, y, W, 6), rgb(blend(mid, low, i / 24.0)));
    }
    p.setOpacity(1);

    // Stars in the upper sky, fading out as they near the glow.
    const QColor starColor("#fff4e6");
    for (int i = 0; i < 140; ++i) {
        const double az = hash(i, 2) * 2 * M_PI, elevation = 0.12 + hash(i, 3) * 1.1, phase = hash(i, 4);
        QPointF at;
        if (!skyPoint(view, az, elevation, &at) || at.x() < 0 || at.x() > W || at.y() < 0)
            continue;
        const double height = clampTo((horizon - 70 - at.y()) / 160, 0, 1);
        if (height <= 0)
            continue;
        const double twinkle = 0.55 + 0.45 * std::sin(time * (1.2 + phase * 2) + phase * 20);
        p.setOpacity(height * twinkle * (0.5 + 0.5 * phase));
        const double s = 0.8 + phase * 1.1;
        p.fillRect(QRectF(at.x() - s / 2, at.y() - s / 2, s, s), starColor);
    }
    p.setOpacity(1);

    // A warm haze spreads along the horizon around the sun.
    const double sunAzimuth = 0.44, sunElevation = 0.1;
    const Vec sunColor = blend(hex("#fff3dd"), view.palette.accent, 0.18);
    const Vec haloColor = blend(view.palette.accent, low, 0.35);
    const double dim = act.dimSun ? 0.35 : 1;
    QPointF sun;
    const bool sunAhead = skyPoint(view, sunAzimuth, sunElevation, &sun);
    if (sunAhead) {
        p.save();
        p.translate(sun);
        p.scale(3.2, 1);
        glow(p, {0, 0}, 150, rgb(haloColor), 0.42 * dim);
        p.restore();
    }

    // Moons: a big pale crescent high on the left of the sun, and a small
    // full one further round, each with a faint glow.
    struct Moon {
        double azimuth, elevation, radius, phase;   // phase: how far the shadow covers it.
        const char *color;
    };
    static const Moon moons[] = {{-0.26, 0.21, 15, 0.42, "#f6ecff"}, {0.95, 0.36, 6.5, 0, "#fde9f2"}};
    for (const Moon &m : moons) {
        QPointF at;
        if (!skyPoint(view, m.azimuth, m.elevation, &at) || at.y() > horizon - m.radius)
            continue;
        glow(p, at, m.radius * 4.5, QColor(m.color), 0.22);
        QPainterPath disc;
        disc.addEllipse(at, m.radius, m.radius);
        if (m.phase > 0) {
            QPainterPath shadow;
            shadow.addEllipse(at + QPointF(m.radius * m.phase * 1.6, -m.radius * 0.25), m.radius * 0.95, m.radius * 0.95);
            disc = disc.subtracted(shadow);
        }
        p.setOpacity(0.92);
        p.fillPath(disc, QColor(m.color));
        p.setOpacity(1);
    }

    // The sun: small and low, ringed by flat bands of light that grow
    // fainter outwards, like a lens's halo.
    if (sunAhead) {
        const double radius = 15;
        p.setPen(Qt::NoPen);
        for (int ring = 5; ring >= 1; --ring) {
            p.setOpacity((0.07 + 0.035 * (5 - ring)) * dim);
            p.setBrush(rgb(blend(sunColor, haloColor, ring / 6.0)));
            const double r = radius * (1 + ring * 0.95);
            p.drawEllipse(sun, r, r);
        }
        p.setOpacity(dim < 1 ? 0.5 : 1);
        p.setBrush(rgb(sunColor));
        p.drawEllipse(sun, radius, radius);
        p.setOpacity(1);
    }

    paintBackdrop(p, view, horizon);

    // The camera never rolls, so ground depth depends only on the screen row:
    // the dark squares, fogged with distance, are one vertical gradient, and
    // only the light squares are drawn as polygons over it.
    if (split < view.height) {
        QLinearGradient ground(0, split, 0, view.height);
        ground.setColorAt(0, rgb(view.atmosphere()));
        for (double d = View::Far; d >= 3; d *= 0.8) {
            const Vec c = view.toCamera({view.camera.x + std::sin(view.camera.yaw) * d, 0, view.camera.z + std::cos(view.camera.yaw) * d});
            if (c[2] <= View::Near)
                continue;
            const double y = view.project(c).y();
            if (y > split && y < view.height)
                ground.setColorAt((y - split) / (view.height - split), rgb(blend(view.palette.tileB, view.atmosphere(), view.fog(c[2]))));
        }
        ground.setColorAt(1, rgb(view.palette.tileB));
        p.fillRect(QRectF(0, split, W, view.height - split), ground);
    }
}

// Clouds and four ranges of hills, each sliding at its own speed: the
// nearer a layer, the further it moves when the camera turns or drifts
// sideways. Each layer wraps once per full turn.
void Sky::paintBackdrop(QPainter &p, const View &view, double horizon) {
    const double W = view.width, time = view.flight->time;
    const View::Act &act = view.style();

    struct Clouds {
        double turn, slide, wind, low, high, opacity;
        int count;
    };
    static const Clouds clouds[] = {{1.4, 1.5, 5, 70, 130, 0.2, 9}, {2.4, 3.6, 13, 130, 240, 0.28, 8}};
    for (int layer = 0; layer < 2; ++layer) {
        const Clouds &c = clouds[layer];
        const double period = 2 * M_PI * View::Focal * c.turn;
        const double scroll = view.camera.yaw * View::Focal * c.turn + view.camera.drift * c.slide + time * c.wind;
        p.setOpacity(c.opacity * (act.dimSun ? 0.45 : 1));
        const QColor color = rgb(blend(view.palette.horizon, hex("#fff3e0"), 0.25 + layer * 0.2));
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

    // A small flock drifts across the far sky in a loose V, flapping.
    {
        const double azimuth = std::fmod(time * 0.012, 2 * M_PI) - 1.2, elevation = 0.16;
        QPointF lead;
        if (skyPoint(view, azimuth, elevation, &lead) && lead.y() < horizon) {
            p.setPen(QPen(withAlpha(rgb(blend(view.palette.top, view.palette.mid, 0.4)), 0.75), 1.1, Qt::SolidLine, Qt::RoundCap));
            p.setBrush(Qt::NoBrush);
            for (int b = 0; b < 9; ++b) {
                const int rank = (b + 1) / 2, side = b % 2 ? -1 : 1;
                const QPointF at = lead + QPointF(-rank * 11 + hash(b, 90) * 3, side * rank * 5 + hash(b, 91) * 3);
                const double flap = std::sin(time * 7 + b * 1.3) * 1.6, span = 3.2;
                p.drawLine(at, at + QPointF(-span, -1 - flap));
                p.drawLine(at, at + QPointF(span, -1 - flap));
            }
        }
    }

    struct Ridge {
        double turn, slide, sink, spacing, low, high, haze;
        bool rounded;
    };
    static const Ridge ridges[] = {
        {1.0, 0.18, 0.08, 38, 48, 115, 0.86, false}, // Distant pale-blue peaks.
        {1.15, 0.75, 0.3, 55, 32, 82, 0.65, false},
        {1.35, 2.1, 0.8, 72, 16, 52, 0.36, false},
        {1.65, 4.2, 1.4, 94, 8, 29, 0.10, true},   // Dark, broad foothills.
    };
    const Vec rock = blend(view.palette.floor, view.palette.mid, 0.3);
    const Vec air = view.atmosphere();
    for (int layer = 0; layer < 4; ++layer) {
        const Ridge &r = ridges[layer];
        const double period = 2 * M_PI * View::Focal * r.turn;
        const int count = std::max(8, int(std::lround(period / r.spacing)));
        const double step = period / count;
        const double scroll = view.camera.yaw * View::Focal * r.turn + view.camera.drift * r.slide;
        // Flying higher reveals the valleys. Near hills move more quickly
        // across the view, while the remote peaks barely shift sideways.
        const double base = horizon + std::max(0.0, view.camera.y - 2) * r.sink;
        const int first = int(std::floor(scroll / step)) - 1, points = int(std::ceil(W / step)) + 3;
        auto peak = [&](int g) {
            const int index = ((g % count) + count) % count;
            // Broad shoulders break up the repeated triangular silhouette.
            const double shape = 0.65 * hash(index, 60 + layer) + 0.35 * hash(index / 3, 80 + layer);
            return QPointF(g * step - scroll, base - mix(r.low, r.high, shape));
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
        // Haze pools in each valley, separating overlapping silhouettes.
        // Far ridges lose contrast and saturation, not just opacity.
        QLinearGradient mist(0, base - r.high, 0, base);
        mist.setColorAt(0, rgb(blend(rock, air, r.haze)));
        mist.setColorAt(1, rgb(blend(rock, air, std::min(0.97, r.haze + 0.18))));
        p.fillPath(path, mist);
    }
}
