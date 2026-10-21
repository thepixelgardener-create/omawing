#pragma once

// Shared geometry keeps the moving ship, its shadow and exhaust aligned.
#include <array>
#include <vector>
#include "flight.h"

namespace ShipShape {
inline constexpr double EngineX = 0.83, EngineY = -0.08, EngineZ = -1.65;
inline const std::vector<Vec> HullVertices{
    {0, 0.05, 3.1}, {-0.65, 0, 0.5}, {0.65, 0, 0.5},
    {-0.58, 0, -1.65}, {0.58, 0, -1.65}, {0, 0.62, 0.35},
    {0, 0.42, -0.85}, {0, -0.35, -1.8}, {0, -0.25, 1.4}};
inline const std::vector<std::vector<int>> HullFaces{
    {0,1,5}, {0,5,2}, {1,3,6}, {1,6,5}, {5,6,2}, {2,6,4},
    {3,7,6}, {6,7,4}, {0,8,1}, {0,2,8}, {1,8,7}, {1,7,3}, {2,4,7}, {2,7,8}};

// Triangles stay planar as the tips flex and sweep under thrust. The
// leading peach strip, dark trailing panels and pink tips share vertices.
inline std::array<std::vector<Vec>, 6> wingPanels(const Ship &ship, double side) {
    const double flex = side < 0 ? ship.flexLeft : ship.flexRight;
    auto point = [&](double x, double y, double z) -> Vec {
        const double span = (x - 0.5) / 3.7;
        return {(x - 0.45 * ship.sweep * span) * side,
                y + flex * 2.6 * span * span, z - ship.sweep * span};
    };
    const Vec root = point(0.5, 0.08, 1.3), shoulder = point(1.45, 0.12, 0.25),
        tip = point(4.2, -0.14, -2.25), edge = point(3.45, -0.08, -1.78),
        ridge = point(1.6, 0.03, -0.48), rear = point(2.3, -0.1, -1.7),
        inner = point(0.64, -0.02, -1.18);
    return {std::vector<Vec>{root, shoulder, ridge}, {shoulder, edge, ridge},
            {edge, tip, ridge}, {ridge, tip, rear}, {ridge, rear, inner}, {root, ridge, inner}};
}

inline std::array<std::vector<Vec>, 2> finPanels(double side) {
    const Vec front{0.93 * side, 0.13, 0.35}, rear{1.03 * side, 0.12, -1.1},
        peak{1.65 * side, 1.55, -0.65}, ridge{1.03 * side, 0.56, -0.38};
    return {std::vector<Vec>{front, peak, ridge}, {ridge, peak, rear}};
}
}
