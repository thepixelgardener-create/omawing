#pragma once

// Small vector helpers shared by the simulation and the renderer.

#include <algorithm>
#include <array>
#include <cmath>

using Vec = std::array<double, 3>;

inline double mix(double a, double b, double t) { return a + (b - a) * t; }
inline double clampTo(double x, double a, double b) { return std::max(a, std::min(b, x)); }
inline double smooth(double t) { return t * t * (3 - 2 * t); }
inline double dot(const Vec &a, const Vec &b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline Vec sub(const Vec &a, const Vec &b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
inline Vec translated(const Vec &v, const Vec &by) { return {v[0] + by[0], v[1] + by[1], v[2] + by[2]}; }
inline Vec scaled(const Vec &v, double s) { return {v[0] * s, v[1] * s, v[2] * s}; }
inline Vec cross(const Vec &a, const Vec &b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
inline Vec normalized(const Vec &v) {
    const double n = std::hypot(v[0], v[1], v[2]);
    return n ? Vec{v[0] / n, v[1] / n, v[2] / n} : v;
}
inline Vec blend(const Vec &a, const Vec &b, double t) { return {mix(a[0], b[0], t), mix(a[1], b[1], t), mix(a[2], b[2], t)}; }
// Bank about z, then pitch about x, then yaw about y.
inline Vec rotate(const Vec &v, const Vec &r) {
    const double yaw = r[0], pitch = r[1], bank = r[2];
    const double x = v[0] * std::cos(bank) - v[1] * std::sin(bank), y = v[0] * std::sin(bank) + v[1] * std::cos(bank);
    const double yy = y * std::cos(pitch) - v[2] * std::sin(pitch), z = y * std::sin(pitch) + v[2] * std::cos(pitch);
    return {x * std::cos(yaw) + z * std::sin(yaw), yy, -x * std::sin(yaw) + z * std::cos(yaw)};
}
// A stable pseudo-random number in [0, 1) for a grid cell, so scenery stays
// put as the camera moves.
inline double hash(double x, double z) {
    const double n = std::sin(x * 127.1 + z * 311.7) * 43758.5453;
    return n - std::floor(n);
}
