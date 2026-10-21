#pragma once

// The sky: gradient, copper bars, stars, sun, clouds, and four ranges of
// hills sliding at different speeds; then the dark squares of the ground,
// as one gradient down to the bottom of the view.

#include "view.h"

class QPainter;

namespace Sky {
void paint(QPainter &p, const View &view);
}
