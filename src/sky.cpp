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

void Sky::paint(QPainter &p, const View &view) {
    const double time = view.flight->time, W = view.width;
    const double horizon = view.height / 2 + View::Focal * std::tan(view.camera.pitch), split = clampTo(horizon, 0, view.height);

    QLinearGradient sky(0, 0, 0, std::max(1.0, split));
    sky.setColorAt(0, rgb(view.palette.top));
    sky.setColorAt(0.66, rgb(view.palette.mid));
    sky.setColorAt(1, rgb(view.palette.horizon));
    p.fillRect(QRectF(0, 0, W, std::max(1.0, split)), sky);

    // Copper bars: the Amiga's copper changed colours line by line.
    for (int i = 0; i < 24; ++i) {
        const double y = horizon - 170 + i * 7 + std::sin(time * 0.7 + i * 0.18) * 6;
        p.setOpacity(0.12 + i / 90.0);
        p.fillRect(QRectF(0, y, W, 6), rgb(blend(view.palette.mid, view.palette.horizon, i / 24.0)));
    }
    p.setOpacity(1);

    // Stars sit at infinity: only the camera's rotation moves them.
    const QColor starColor("#fcecc6");
    for (int i = 0; i < 130; ++i) {
        const double az = hash(i, 2) * 2 * M_PI, elevation = 0.05 + hash(i, 3) * 1.2, phase = hash(i, 4);
        const Vec direction{std::sin(az) * std::cos(elevation), std::sin(elevation), std::cos(az) * std::cos(elevation)};
        const Vec c{dot(direction, view.right), dot(direction, view.up), dot(direction, view.forward)};
        if (c[2] <= 0.05)
            continue;
        const QPointF at = view.project(c);
        if (at.x() < 0 || at.x() > W || at.y() < 88 || at.y() > horizon)
            continue;
        const double s = 0.3 + phase * (0.6 + 0.3 * std::sin(time + phase * 10));
        p.fillRect(QRectF(at.x() - 0.8, at.y() - 0.8, s + 1.6, 1.6), starColor);
    }

    const View::Act &act = view.style();
    const Vec sunDirection{std::sin(0.44) * std::cos(0.1), std::sin(0.1), std::cos(0.44) * std::cos(0.1)};
    const Vec sun{dot(sunDirection, view.right), dot(sunDirection, view.up), dot(sunDirection, view.forward)};
    if (sun[2] > 0.15) {
        const Vec sunColor = blend(view.palette.accent, hex("#ffd098"), 0.55);
        p.save();
        p.translate(view.project(sun));
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

    paintBackdrop(p, view, horizon);

    // The camera never rolls, so ground depth depends only on the screen row:
    // the dark squares, fogged with distance, are one vertical gradient, and
    // only the light squares are drawn as polygons over it.
    if (split < view.height) {
        QLinearGradient ground(0, split, 0, view.height);
        ground.setColorAt(0, rgb(view.palette.floor));
        for (double d = View::Far; d >= 3; d *= 0.8) {
            const Vec c = view.toCamera({view.camera.x + std::sin(view.camera.yaw) * d, 0, view.camera.z + std::cos(view.camera.yaw) * d});
            if (c[2] <= View::Near)
                continue;
            const double y = view.project(c).y();
            if (y > split && y < view.height)
                ground.setColorAt((y - split) / (view.height - split), rgb(blend(view.palette.tileB, view.palette.floor, view.fog(c[2]))));
        }
        ground.setColorAt(1, rgb(view.palette.tileB));
        p.fillRect(QRectF(0, split, W, view.height - split), ground);
    }
}

// Clouds and three ranges of hills, each sliding at its own speed: the
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

    struct Ridge {
        double turn, slide, sink, spacing, low, high;
        bool rounded;
    };
    static const Ridge ridges[] = {
        {1.0, 0.4, 0.15, 22, 26, 86, false},   // Far mountains, nearly at infinity.
        {1.8, 2.2, 0.7, 34, 14, 52, false},
        {3.0, 6, 1.8, 54, 6, 28, true},        // Rolling hills close by.
    };
    const Vec colors[] = {blend(blend(view.palette.floor, view.palette.mid, 0.5), view.palette.horizon, 0.45),
                          blend(view.palette.floor, view.palette.mid, 0.5), blend(view.palette.floor, view.palette.tileB, 0.5)};
    for (int layer = 0; layer < 3; ++layer) {
        const Ridge &r = ridges[layer];
        const double period = 2 * M_PI * View::Focal * r.turn;
        const int count = std::max(8, int(std::lround(period / r.spacing)));
        const double step = period / count;
        const double scroll = view.camera.yaw * View::Focal * r.turn + view.camera.drift * r.slide;
        // Flying higher lets the nearer hills sink below the horizon.
        const double base = horizon + std::max(0.0, view.camera.y - 2) * r.sink;
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
