#pragma once

// The light squares of the checkerboard, grid lines in some acts, and the
// ship's shadow.

#include "view.h"

class QPainter;

namespace Ground {
void paint(QPainter &p, const View &view);
}
