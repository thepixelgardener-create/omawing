#include "ground.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <algorithm>
#include <cmath>

#include "shipshape.h"

using namespace geo;
using namespace draw;

void Ground::paint(QPainter &p, const View &view) {
    // The checkerboard runs out as far as its tiles still show on screen,
    // hundreds of units, and fades into the floor's average colour as they
    // shrink, so the plane reaches the horizon unbroken.
    const double step = View::Tile, reach = view.floorReach();
    const int extent = int(std::ceil(reach / step)) + 2;
    const int gx = int(std::floor(view.camera.x / step)), gz = int(std::floor(view.camera.z / step));
    const bool gridLines = view.style().grid;
    const int side = extent * 2 + 1;
    std::vector<Vec> grid(size_t(side) * side);
    for (int j = -extent; j <= extent; ++j)
        for (int i = -extent; i <= extent; ++i)
            grid[size_t(j + extent) * side + (i + extent)] = view.toCamera({double(gx + i) * step, 0, double(gz + j) * step});
    auto corner = [&](int i, int j) -> const Vec & { return grid[size_t(j) * side + i]; };
    // Tiles are grouped by depth, in steps that grow with distance, and
    // drawn with the convex-polygon fast path: a few dozen colours, no
    // stencil passes.
    constexpr int Bands = 40, LineBands = 8;
    const double nearest = 4;
    auto band = [&](double depth) {
        return std::clamp(int(std::log(std::max(nearest, depth) / nearest) / std::log(reach / nearest) * Bands), 0, Bands - 1);
    };
    std::array<std::vector<QPolygonF>, Bands> tiles;
    std::array<QPainterPath, LineBands> lines;
    const double sideSlope = view.width / (2 * View::Focal) + 0.2;
    for (int j = 0; j < side - 1; ++j)
        for (int i = 0; i < side - 1; ++i) {
            const Vec &a = corner(i, j), &b = corner(i + 1, j), &c = corner(i + 1, j + 1), &d = corner(i, j + 1);
            const double depth = (a[2] + b[2] + c[2] + d[2]) / 4;
            if (depth < -step || depth > reach)
                continue;
            // Cheap side culling before the full clip.
            const double edge = std::max(depth, 1.0) * sideSlope + step;
            if (std::min({a[0], b[0], c[0], d[0]}) > edge || std::max({a[0], b[0], c[0], d[0]}) < -edge)
                continue;
            const int parity = (((gx + gz + i + j) % 2) + 2) % 2;
            const double contrast = view.contrast(depth);
            if (!parity && !(gridLines && contrast > 0.02))
                continue;
            if (parity && contrast < 0.01 && !gridLines)
                continue;
            const auto clipped = view.clip({a, b, c, d});
            if (clipped.size() < 3)
                continue;
            const QPolygonF polygon = view.polygonOf(clipped);
            if (parity && contrast >= 0.01)
                tiles[band(depth)].push_back(polygon);
            if (gridLines && contrast > 0.02) {
                // Lines fade out with the squares, band by band.
                QPainterPath &line = lines[std::min(LineBands - 1, int((1 - contrast) * LineBands))];
                line.addPolygon(polygon);
                line.closeSubpath();
            }
        }
    p.setPen(Qt::NoPen);
    for (int i = 0; i < Bands; ++i) {
        if (tiles[i].empty())
            continue;
        const double depth = nearest * std::pow(reach / nearest, (i + 0.5) / Bands);
        p.setBrush(rgb(view.floorLight(depth)));
        for (const QPolygonF &polygon : tiles[i]) p.drawConvexPolygon(polygon);
    }
    const QPen linePen(rgb(blend(view.palette.accent, view.palette.tileA, 0.4)), 0.55);
    for (int i = 0; i < LineBands; ++i) {
        if (lines[i].isEmpty())
            continue;
        p.setOpacity(0.25 * (1 - (i + 0.5) / LineBands));
        p.strokePath(lines[i], linePen);
    }
    p.setOpacity(1);

    if (view.valley > 0) {
        // Small emissive strips mark the corridor. Their soft pools of light
        // sit on the same projected ground plane as the checkerboard.
        const int start = int(std::floor((view.camera.z - View::Far) / 12));
        for (int i = start; i < start + int(View::Far * 2 / 12) + 2; ++i) {
            for (double side : {-1.0, 1.0}) {
                const double x = side * 7.5, z = i * 12.0;
                const Vec at = view.toCamera({x, 0.08, z});
                if (at[2] < 2 || at[2] > View::Far) continue;
                const double alpha = view.valley * view.fadeIn(at[2]);
                const QPointF screen = view.project(at);
                const double radius = std::min(22.0, View::Focal * 0.65 / at[2]);
                QRadialGradient glow(screen, radius);
                glow.setColorAt(0, withAlpha("#ff7fdd", 0.55 * alpha));
                glow.setColorAt(1, withAlpha("#ff49c9", 0));
                p.setPen(Qt::NoPen); p.setBrush(glow);
                p.drawEllipse(screen, radius, radius);
                const std::vector<Vec> corners{
                    view.toCamera({x-0.10,0.08,z-0.65}), view.toCamera({x+0.10,0.08,z-0.65}),
                    view.toCamera({x+0.10,0.08,z+0.65}), view.toCamera({x-0.10,0.08,z+0.65})};
                const auto clipped = view.clip(corners);
                if (clipped.size() >= 3) {
                    p.setBrush(withAlpha("#fff1fc", alpha));
                    p.drawConvexPolygon(view.polygonOf(clipped));
                }
            }
        }
    }

    // The ship's shadow: its hull and wings flattened straight down onto the
    // ground, so it banks, flexes, and sweeps with the ship. Every piece is
    // wound the same way and filled as one path, so overlaps don't darken.
    const auto &ship = view.flight->ship;
    const Vec at{ship.x, ship.y, ship.z}, rotation{ship.yaw, ship.pitch, ship.bank};
    std::vector<std::vector<Vec>> pieces;
    for (const auto &indices : ShipShape::HullFaces) {
        std::vector<Vec> piece;
        for (int index : indices)
            piece.push_back(ShipShape::HullVertices[index]);
        pieces.push_back(std::move(piece));
    }
    for (const double side : {-1.0, 1.0})
        for (const auto &panel : ShipShape::wingPanels(ship, side))
            pieces.push_back(panel);
    for (const double side : {-1.0, 1.0})
        for (const auto &panel : ShipShape::finPanels(side))
            pieces.push_back(panel);
    QPainterPath shadow;
    shadow.setFillRule(Qt::WindingFill);
    for (const auto &piece : pieces) {
        std::vector<Vec> flat;
        for (const Vec &v : piece) {
            Vec world = translated(rotate(v, rotation), at);
            world[1] = 0.03;
            flat.push_back(view.toCamera(world));
        }
        const auto clipped = view.clip(flat);
        if (clipped.size() < 3)
            continue;
        QPolygonF polygon = view.polygonOf(clipped);
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
