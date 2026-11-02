#pragma once

// The light squares of the checkerboard, grid lines in some acts, and the
// ship's shadow.

#include <QLineF>
#include <QPolygonF>
#include <array>
#include <vector>

#include "view.h"

class QPainter;

class Ground {
public:
    void paint(QPainter &p, const View &view);

private:
    // Tiles are grouped by depth into Bands, each drawn in one colour, and
    // grid lines into LineBands, each at one opacity.
    static constexpr int Bands = 40, LineBands = 8;
    void paintShadow(QPainter &p, const View &view);
    void paintCorridor(QPainter &p, const View &view);
    void paintLines(QPainter &p, const View &view, int side, double reach);

    // Kept from frame to frame, so their storage is reused.
    std::vector<Vec> m_grid, m_points, m_scratch;
    std::array<std::vector<QPolygonF>, Bands> m_tiles;
    std::array<std::vector<QLineF>, LineBands> m_lines;
};
