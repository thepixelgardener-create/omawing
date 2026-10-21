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
    // The checkerboard reaches past the end of the fog, so its edge is never
    // seen and rows don't pop in as the camera crosses a tile.
    const int step = 6, extent = int(std::ceil(View::Far / step)) + 2;
    const int gx = int(std::floor(view.camera.x / step)), gz = int(std::floor(view.camera.z / step));
    const bool gridLines = view.style().grid;
    std::vector<std::vector<Vec>> grid(extent * 2 + 1, std::vector<Vec>(extent * 2 + 1));
    for (int j = -extent; j <= extent; ++j)
        for (int i = -extent; i <= extent; ++i)
            grid[j + extent][i + extent] = view.toCamera({double(gx + i) * step, 0, double(gz + j) * step});
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
            const double haze = view.fog(std::max(0.0, depth));
            if (depth < -5 || haze >= 0.995 || (!parity && !gridLines))
                continue;
            const auto clipped = view.clip(points);
            if (clipped.size() < 3)
                continue;
            const QPolygonF polygon = view.polygonOf(clipped);
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
        p.setBrush(rgb(blend(view.palette.tileA, view.palette.floor, haze)));
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
