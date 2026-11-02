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

namespace {
// The hull's faces as point lists, for the shadow; they never change.
const std::vector<std::vector<Vec>> &hullPieces() {
    static const auto pieces = [] {
        std::vector<std::vector<Vec>> list;
        for (const auto &indices : ShipShape::HullFaces) {
            std::vector<Vec> piece;
            for (int index : indices)
                piece.push_back(ShipShape::HullVertices[index]);
            list.push_back(std::move(piece));
        }
        return list;
    }();
    return pieces;
}
}

void Ground::paint(QPainter &p, const View &view) {
    // The checkerboard runs out as far as its tiles still show on screen,
    // hundreds of units, and fades into the floor's average colour as they
    // shrink, so the plane reaches the horizon unbroken.
    const double step = View::Tile, reach = view.floorReach();
    const int extent = int(std::ceil(reach / step)) + 2;
    const int gx = int(std::floor(view.camera.x / step)), gz = int(std::floor(view.camera.z / step));
    const bool gridLines = view.style().grid;
    const int side = extent * 2 + 1;
    // The corners form a regular lattice, so in camera space each one is the
    // first plus whole steps along two fixed vectors: three dot products for
    // the whole grid instead of one transform per corner.
    m_grid.resize(size_t(side) * side);
    const Vec origin = view.toCamera({double(gx - extent) * step, 0, double(gz - extent) * step});
    const Vec across = sub(view.toCamera({step, 0, 0}), view.toCamera({0, 0, 0}));
    const Vec along = sub(view.toCamera({0, 0, step}), view.toCamera({0, 0, 0}));
    for (int j = 0; j < side; ++j) {
        Vec row = translated(origin, scaled(along, j));
        for (int i = 0; i < side; ++i, row = translated(row, across))
            m_grid[size_t(j) * side + i] = row;
    }
    auto corner = [&](int i, int j) -> const Vec & { return m_grid[size_t(j) * side + i]; };
    // Tiles are grouped by depth, in steps that grow with distance, and
    // drawn with the convex-polygon fast path: a few dozen colours, no
    // stencil passes.
    const double nearest = 4, bandScale = Bands / std::log(reach / nearest);
    auto band = [&](double depth) {
        return std::clamp(int(std::log(std::max(nearest, depth) / nearest) * bandScale), 0, Bands - 1);
    };
    for (auto &tiles : m_tiles)
        tiles.clear();
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
            // Only the light squares are drawn, over the dark gradient.
            if ((((gx + gz + i + j) % 2) + 2) % 2 == 0 || view.contrast(depth) < 0.01)
                continue;
            m_points.assign({a, b, c, d});
            view.clipInPlace(m_points, m_scratch);
            if (m_points.size() >= 3)
                m_tiles[band(depth)].push_back(view.polygonOf(m_points));
        }
    p.setPen(Qt::NoPen);
    for (int i = 0; i < Bands; ++i) {
        if (m_tiles[i].empty())
            continue;
        const double depth = nearest * std::pow(reach / nearest, (i + 0.5) / Bands);
        p.setBrush(rgb(view.floorLight(depth)));
        for (const QPolygonF &polygon : m_tiles[i]) p.drawConvexPolygon(polygon);
    }
    if (gridLines)
        paintLines(p, view, side, reach);

    if (view.valley > 0)
        paintCorridor(p, view);
    paintShadow(p, view);
}

// Every line of the grid is straight, on the ground and on screen, so each
// is drawn as a few long segments, one wherever its fade changes, rather than
// edge by edge around every square: far fewer for the stroker to outline.
void Ground::paintLines(QPainter &p, const View &view, int side, double reach) {
    for (auto &lines : m_lines)
        lines.clear();
    auto corner = [&](int i, int j) -> const Vec & { return m_grid[size_t(j) * side + i]; };
    auto add = [&](int band, Vec a, Vec b) {
        if (band >= 0 && view.clipSegment(a, b))
            m_lines[band].push_back(QLineF(view.project(a), view.project(b)));
    };
    for (int across = 0; across < 2; ++across)
        for (int k = 0; k < side; ++k) {
            int runBand = -1;
            Vec runStart{0, 0, 0}, runEnd{0, 0, 0};
            for (int e = 0; e < side - 1; ++e) {
                const Vec &a = across ? corner(e, k) : corner(k, e), &b = across ? corner(e + 1, k) : corner(k, e + 1);
                const double depth = (a[2] + b[2]) / 2;
                // Lines fade out with the squares, band by band.
                int band = -1;
                if (depth >= -View::Tile && depth <= reach) {
                    const double contrast = view.contrast(depth);
                    if (contrast > 0.02)
                        band = std::min(LineBands - 1, int((1 - contrast) * LineBands));
                }
                if (band != runBand) {
                    add(runBand, runStart, runEnd);
                    runBand = band;
                    runStart = a;
                }
                runEnd = b;
            }
            add(runBand, runStart, runEnd);
        }
    QPen pen(rgb(blend(view.palette.accent, view.palette.tileA, 0.4)), 0.55);
    pen.setCapStyle(Qt::FlatCap);
    p.setPen(pen);
    for (int i = 0; i < LineBands; ++i) {
        if (m_lines[i].empty())
            continue;
        p.setOpacity(0.25 * (1 - (i + 0.5) / LineBands));
        p.drawLines(m_lines[i].data(), int(m_lines[i].size()));
    }
    p.setOpacity(1);
    p.setPen(Qt::NoPen);
}

void Ground::paintCorridor(QPainter &p, const View &view) {
    // Small emissive strips mark the corridor. Their soft pools of light
    // sit on the same projected ground plane as the checkerboard.
    static const QColor glowCore("#ff7fdd"), glowEdge = withAlpha("#ff49c9", 0), stripColor("#fff1fc");
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
            glow.setColorAt(0, withAlpha(glowCore, 0.55 * alpha));
            glow.setColorAt(1, glowEdge);
            p.setPen(Qt::NoPen); p.setBrush(glow);
            p.drawEllipse(screen, radius, radius);
            m_points.assign({view.toCamera({x-0.10,0.08,z-0.65}), view.toCamera({x+0.10,0.08,z-0.65}),
                             view.toCamera({x+0.10,0.08,z+0.65}), view.toCamera({x-0.10,0.08,z+0.65})});
            view.clipInPlace(m_points, m_scratch);
            if (m_points.size() >= 3) {
                p.setBrush(withAlpha(stripColor, alpha));
                p.drawConvexPolygon(view.polygonOf(m_points));
            }
        }
    }
}

// The ship's shadow: its hull and wings flattened straight down onto the
// ground, so it banks, flexes, and sweeps with the ship. Every piece is
// wound the same way and filled as one path, so overlaps don't darken.
void Ground::paintShadow(QPainter &p, const View &view) {
    static const QColor shade("#12102a");
    const auto &ship = view.flight->ship;
    const Vec at{ship.x, ship.y, ship.z}, rotation{ship.yaw, ship.pitch, ship.bank};
    QPainterPath shadow;
    shadow.setFillRule(Qt::WindingFill);
    auto add = [&](const std::vector<Vec> &piece) {
        m_points.clear();
        for (const Vec &v : piece) {
            Vec world = translated(rotate(v, rotation), at);
            world[1] = 0.03;
            m_points.push_back(view.toCamera(world));
        }
        view.clipInPlace(m_points, m_scratch);
        if (m_points.size() < 3)
            return;
        QPolygonF polygon = view.polygonOf(m_points);
        double area = 0;
        for (int i = 0; i < polygon.size(); ++i) {
            const QPointF &a = polygon[i], &b = polygon[(i + 1) % polygon.size()];
            area += a.x() * b.y() - b.x() * a.y();
        }
        if (std::abs(area) < 1e-3)
            return;
        if (area < 0)
            std::reverse(polygon.begin(), polygon.end());
        shadow.addPolygon(polygon);
        shadow.closeSubpath();
    };
    for (const auto &piece : hullPieces())
        add(piece);
    for (const double side : {-1.0, 1.0})
        for (const auto &panel : ShipShape::wingPanels(ship, side))
            add(panel);
    for (const double side : {-1.0, 1.0})
        for (const auto &panel : ShipShape::finPanels(side))
            add(panel);
    if (!shadow.isEmpty()) {
        p.setOpacity(clampTo(0.55 - ship.y * 0.025, 0.12, 0.5));
        p.fillPath(shadow, shade);
        p.setOpacity(1);
    }
}
