#pragma once

// The ship's hull and wings in its own frame, shared by the ship the world
// draws and the shadow the ground draws under it.

#include <array>
#include <vector>

#include "flight.h"

namespace ShipShape {
inline const std::vector<Vec> HullVertices{{0, 0, 2.8}, {-0.65, 0, -1.6}, {0.65, 0, -1.6}, {0, 0.65, -0.4}, {0, -0.4, -0.6}};
inline const std::vector<std::vector<int>> HullFaces{{0, 1, 3}, {0, 3, 2}, {1, 2, 3}, {0, 4, 1}, {0, 2, 4}, {1, 4, 2}};

// Each wing is an inner panel and an outer one, on the left (side -1) or
// right (+1). Flex lifts the tip most and the middle a little, so the wing
// bends rather than hinges; sweep folds the tips back.
inline std::array<std::vector<Vec>, 2> wingPanels(const Ship &ship, double side) {
    const double flex = side < 0 ? ship.flexLeft : ship.flexRight;
    const Vec rootFront{0.5 * side, 0, 1}, rootRear{0.55 * side, 0.05, -1.2};
    const Vec tip{(3.3 - 0.55 * ship.sweep) * side, -0.12 + flex * 2.6, -1.9 - 1.1 * ship.sweep};
    Vec midFront = geo::blend(rootFront, tip, 0.5), midRear = geo::blend(rootRear, tip, 0.5);
    midFront[1] = geo::mix(rootFront[1], -0.12, 0.5) + flex * 0.75;
    midRear[1] = geo::mix(rootRear[1], -0.12, 0.5) + flex * 0.75;
    return {std::vector<Vec>{rootFront, midFront, midRear, rootRear}, std::vector<Vec>{midFront, tip, midRear}};
}
}
