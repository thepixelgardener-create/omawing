#include "flight.h"

#include <algorithm>
#include <cmath>

namespace {
double mix(double a, double b, double t) { return a + (b - a) * t; }
double clampTo(double x, double a, double b) { return std::max(a, std::min(b, x)); }
// The shortest signed angle from a to b, so turning never goes the long way round.
double turn(double from, double to) { return std::atan2(std::sin(to - from), std::cos(to - from)); }
}

int Flight::act() const { return int(std::floor(time / ActLength)) % 3; }

double Flight::boostAmount() const { return clampTo((ship.speed - CruiseSpeed) / 14, 0, 1); }

void Flight::step(double dt) {
    time += dt;
    double steer = 0, climb = 0;
    bool boost = false;
    if (demo) {
        const double wantedYaw = std::sin(time * 0.24) * 0.28 + std::sin(time * 0.51) * 0.07;
        steer = clampTo(turn(ship.yaw, wantedYaw) * 2.2, -0.7, 0.7);
        climb = clampTo((3.7 + 1.2 * std::sin(time * 0.43) - ship.y) * 0.7, -0.7, 0.7);
        const double inAct = std::fmod(time, ActLength);
        boost = act() == 2 && inAct > 6 && inAct < 17;
    } else {
        steer = double(holding(Right)) - double(holding(Left));
        climb = double(holding(Climb)) - double(holding(Descend));
        if (m_stick) {
            steer = m_stickX;
            climb = -m_stickY;
        }
        boost = holding(Boost);
    }

    // Exponential smoothing makes the motion independent of the frame rate.
    const double ease = 1 - std::exp(-dt * 5);
    ship.yaw += steer * dt * 0.85;
    ship.bank = mix(ship.bank, -steer * 0.58, ease);
    ship.vertical = mix(ship.vertical, climb * 4.2, ease);
    ship.pitch = mix(ship.pitch, -climb * 0.16, ease);
    ship.speed = mix(ship.speed, boost ? BoostSpeed : CruiseSpeed, 1 - std::exp(-dt * 2.4));
    ship.x += std::sin(ship.yaw) * ship.speed * dt;
    ship.z += std::cos(ship.yaw) * ship.speed * dt;
    ship.y = clampTo(ship.y + ship.vertical * dt, MinAltitude, MaxAltitude);

    // The camera trails the ship and looks at a point ahead of it, so it
    // leans into turns before the ship does.
    const double follow = 1 - std::exp(-dt * 4), yawEase = 1 - std::exp(-dt * 3);
    const double distance = 11.5 + boostAmount() * 2;
    camera.x = mix(camera.x, ship.x - std::sin(ship.yaw) * distance, follow);
    camera.z = mix(camera.z, ship.z - std::cos(ship.yaw) * distance, follow);
    camera.y = mix(camera.y, ship.y + 4.3, follow);
    const double tx = ship.x + std::sin(ship.yaw) * 24, ty = ship.y + 0.4, tz = ship.z + std::cos(ship.yaw) * 24;
    camera.yaw += turn(camera.yaw, std::atan2(tx - camera.x, tz - camera.z)) * yawEase;
    camera.pitch = mix(camera.pitch, std::atan2(ty - camera.y, std::hypot(tx - camera.x, tz - camera.z)), yawEase);
}

void Flight::advance(double seconds) {
    for (double left = seconds; left > 1e-8;) {
        const double dt = std::min(left, 1.0 / 60);
        step(dt);
        left -= dt;
    }
}

void Flight::reset() {
    ship = Ship();
    camera = ChaseCamera();
    time = 0;
    demo = true;
    clearInput();
}

// Lands two seconds into the next act, after its colours have blended in.
void Flight::nextScene() { time = (std::floor(time / ActLength) + 1) * ActLength + 2; }

void Flight::scene(int index) {
    time = std::floor(time / (ActLength * 3)) * ActLength * 3 + std::clamp(index, 0, 2) * ActLength + 2;
}

void Flight::setStick(bool active, double x, double y) {
    m_stick = active;
    m_stickX = active ? clampTo(x, -1, 1) : 0;
    m_stickY = active ? clampTo(y, -1, 1) : 0;
}

void Flight::clearInput() {
    m_held = 0;
    setStick(false);
}
