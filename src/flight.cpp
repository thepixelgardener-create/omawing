#include "flight.h"

#include <algorithm>
#include <cmath>

using namespace geo;

namespace {
// The shortest signed angle from a to b, so turning never goes the long way round.
double turn(double from, double to) { return std::atan2(std::sin(to - from), std::cos(to - from)); }
// The acts where the autopilot opens the throttle.
bool boostAct(int act) { return act == 2 || act == 7; }
}

namespace World {
std::vector<Obstacle> scenery(double x, double z, int reach, double time) {
    std::vector<Obstacle> list;
    const int cx = int(std::floor(x / Cell)), cz = int(std::floor(z / Cell));
    for (int iz = cz - reach; iz <= cz + reach; ++iz)
        for (int ix = cx - reach; ix <= cx + reach; ++ix) {
            const double ox = ix * Cell + 12 + (hash(ix, iz) - 0.5) * 8, oz = iz * Cell + 14;
            // The course down the middle stays clear.
            if (std::abs(ox) < 8)
                continue;
            if ((ix + iz) % 3 == 0)
                list.push_back({Obstacle::Pyramid, ox, 0, oz, 0, ix, iz});
            else
                list.push_back({Obstacle::Crystal, ox, 2 + std::sin(time * 1.4 + ix + iz) * 0.5, oz, time * 0.4 + ix, ix, iz});
        }
    return list;
}

Ring ring(int index) { return {index, 5 * std::sin(index * 1.1), RingHeight, index * RingSpacing + 24}; }

Obstacle ball(double z, double time) {
    return {Obstacle::Ball, 14, 4.3 + std::abs(std::sin(time * 1.25)) * 3.5, std::floor((z + 8) / 110) * 110 + 36,
            time * 0.5};
}
}

int Flight::act() const { return int(std::floor(time / ActLength)) % Acts; }

double Flight::boostAmount() const { return clampTo((ship.speed - CruiseSpeed) / 14, 0, 1); }

std::array<Vec, 2> Flight::nozzles() const {
    const Vec rotation{ship.yaw, ship.pitch, ship.bank}, at{ship.x, ship.y, ship.z};
    return {translated(rotate({-0.65, -0.05, -1.6}, rotation), at), translated(rotate({0.65, -0.05, -1.6}, rotation), at)};
}

void Flight::step(double dt) {
    events = 0;
    if (!std::isfinite(dt) || dt <= 0)
        return;
    if (dt > 0.05) {
        advance(dt);
        return;
    }
    time += dt;
    const double px = ship.x, py = ship.y, pz = ship.z;
    double steering = 0, climb = 0;
    bool boost = false;
    if (game.wrecking()) {
        // A wreck has no say over where it goes.
    } else if (demo) {
        const double wantedYaw = std::sin(time * 0.24) * 0.28 + std::sin(time * 0.51) * 0.07;
        steering = clampTo(turn(ship.yaw, wantedYaw) * 2.2, -0.7, 0.7);
        climb = clampTo((3.7 + 1.2 * std::sin(time * 0.43) - ship.y) * 0.7, -0.7, 0.7);
        const double inAct = std::fmod(time, ActLength);
        boost = boostAct(act()) && inAct > 6 && inAct < 17;
    } else {
        steering = double(holding(Right)) - double(holding(Left));
        climb = double(holding(Climb)) - double(holding(Descend));
        if (m_stick) {
            steering = m_stickX;
            climb = -m_stickY;
        }
        boost = holding(Boost);
    }
    if (boost && !m_boosting) events |= BoostStarted;
    m_boosting = boost;
    steer(dt, steering, climb, boost);
    follow(dt);

    // Exhaust, spread along the path the nozzles took this step so the
    // trail stays smooth at any speed.
    if (!game.wrecking()) {
        const Vec velocity{(ship.x - px) / dt, (ship.y - py) / dt, (ship.z - pz) / dt};
        const Vec back = rotate({0, 0, -1}, {ship.yaw, ship.pitch, ship.bank});
        const auto from = nozzles();
        m_exhaust += dt * (55 + 75 * boostAmount());
        for (; m_exhaust >= 1; m_exhaust -= 1) {
            const double lag = random() * dt;
            for (const Vec &nozzle : from)
                emitParticles(Particle::Exhaust, translated(nozzle, scaled(velocity, -lag)),
                     translated(scaled(velocity, 0.1), scaled(back, 3 + 5 * boostAmount())), 0.5, 1);
        }
    }
    if (!demo)
        game.play(*this, dt, {px, py, pz});
    game.age(dt);
    animateParticles(dt);
}

void Flight::steer(double dt, double steering, double climb, bool boost) {
    // Exponential smoothing makes the motion independent of the frame rate.
    const double ease = 1 - std::exp(-dt * 5);
    ship.yaw += steering * dt * 0.85;
    if (game.wrecking()) {
        // A wreck rolls over and sinks.
        ship.bank += dt * 6.5;
        ship.vertical = mix(ship.vertical, -5, ease);
        ship.pitch = mix(ship.pitch, 0.3, ease);
    } else {
        ship.bank = mix(ship.bank, -steering * 0.58, ease);
        ship.vertical = mix(ship.vertical, climb * 4.2, ease);
        ship.pitch = mix(ship.pitch, -climb * 0.16, ease);
    }
    ship.speed = mix(ship.speed, game.wrecking() ? 8 : boost ? BoostSpeed : CruiseSpeed, 1 - std::exp(-dt * 2.4));
    ship.x += std::sin(ship.yaw) * ship.speed * dt;
    ship.z += std::cos(ship.yaw) * ship.speed * dt;
    ship.y = clampTo(ship.y + ship.vertical * dt, MinAltitude, MaxAltitude);

    // Climbing loads both wings up; turning lifts one tip and drops the
    // other. Springs carry them past and back, so they swing.
    ship.sweep = mix(ship.sweep, boostAmount(), 1 - std::exp(-dt * 3));
    const double flutter = boostAmount() * 0.03 * std::sin(time * 47);
    auto spring = [dt](double &flex, double &rate, double target) {
        rate += ((target - flex) * 70 - rate * 5) * dt;
        flex += rate * dt;
    };
    spring(ship.flexLeft, ship.flexLeftRate, climb * 0.22 + steering * 0.18 + flutter);
    spring(ship.flexRight, ship.flexRightRate, climb * 0.22 - steering * 0.18 - flutter);
}

void Flight::follow(double dt) {
    // The camera trails the ship and looks at a point ahead of it, so it
    // leans into turns before the ship does.
    const double ox = camera.x, oz = camera.z;
    const double follow = 1 - std::exp(-dt * 4), yawEase = 1 - std::exp(-dt * 3);
    const double distance = 17.5 + boostAmount() * 2.5;
    camera.x = mix(camera.x, ship.x - std::sin(ship.yaw) * distance, follow);
    camera.z = mix(camera.z, ship.z - std::cos(ship.yaw) * distance, follow);
    camera.y = mix(camera.y, ship.y + 5.0, follow);
    const double tx = ship.x + std::sin(ship.yaw) * 56, ty = ship.y + 1.0, tz = ship.z + std::cos(ship.yaw) * 56;
    camera.yaw += turn(camera.yaw, std::atan2(tx - camera.x, tz - camera.z)) * yawEase;
    camera.pitch = mix(camera.pitch, std::atan2(ty - camera.y, std::hypot(tx - camera.x, tz - camera.z)), yawEase);
    camera.drift += (camera.x - ox) * std::cos(camera.yaw) - (camera.z - oz) * std::sin(camera.yaw);
    camera.shake = std::max(0.0, camera.shake - dt * 1.8);
}

void Flight::emitParticles(Particle::Kind kind, const Vec &at, const Vec &velocity, double spread, int count) {
    for (int i = 0; i < count; ++i) {
        // A random direction, weighted towards the middle.
        const double a = random() * 2 * M_PI, b = std::acos(2 * random() - 1), s = spread * random();
        Particle p;
        p.kind = kind;
        p.position = at;
        p.velocity = translated(velocity, {std::sin(b) * std::cos(a) * s, std::cos(b) * s, std::sin(b) * std::sin(a) * s});
        switch (kind) {
        case Particle::Exhaust: p.life = 0.3 + 0.12 * random(); p.size = 0.24; break;
        case Particle::Spark: p.life = 0.45 + 0.5 * random(); p.size = 0.1; break;
        case Particle::Glitter: p.life = 0.7 + 0.5 * random(); p.size = 0.16; break;
        case Particle::Smoke: p.life = 0.9 + 0.6 * random(); p.size = 0.55; break;
        case Particle::Flash: p.life = 0.4; p.size = 2.6; break;
        }
        particles.push_back(p);
    }
    if (particles.size() > size_t(MaxParticles))
        particles.erase(particles.begin(), particles.begin() + (particles.size() - MaxParticles));
}

void Flight::animateParticles(double dt) {
    for (Particle &p : particles) {
        p.age += dt;
        switch (p.kind) {
        case Particle::Exhaust: p.velocity = scaled(p.velocity, std::exp(-dt * 2)); break;
        case Particle::Spark: p.velocity[1] -= 16 * dt; break;
        case Particle::Glitter: p.velocity = scaled(p.velocity, std::exp(-dt * 2.5)); p.velocity[1] -= 1.5 * dt; break;
        case Particle::Smoke: p.velocity = scaled(p.velocity, std::exp(-dt * 1.2)); p.velocity[1] += 1.2 * dt; break;
        case Particle::Flash: break;
        }
        p.position = translated(p.position, scaled(p.velocity, dt));
        // Sparks bounce off the ground.
        if (p.position[1] < 0.05 && p.velocity[1] < 0) {
            p.position[1] = 0.05;
            p.velocity[1] *= -0.4;
        }
    }
    std::erase_if(particles, [](const Particle &p) { return p.age >= p.life; });
}

// A small linear congruential generator.
double Flight::random() {
    m_seed = m_seed * 1664525u + 1013904223u;
    return (m_seed >> 8) / double(1u << 24);
}

void Flight::advance(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0)
        return;
    // Events from every step are kept, so a ring passed early still sounds.
    unsigned happened = 0;
    for (double left = seconds; left > 1e-8;) {
        const double dt = std::min(left, 1.0 / 60);
        step(dt);
        happened |= events;
        left -= dt;
    }
    events = happened;
}

void Flight::reset() {
    ship = Ship();
    camera = ChaseCamera();
    time = 0;
    demo = true;
    particles.clear();
    m_exhaust = 0;
    m_seed = 1;
    m_boosting = false;
    newRun();
    clearInput();
}

void Flight::newRun() {
    events = 0;
    Ship fresh;
    fresh.x = ship.x;
    fresh.z = ship.z;
    fresh.yaw = ship.yaw;
    ship = fresh;
    game.reset();
}

// Lands two seconds into the next act, after its colours have blended in.
void Flight::nextScene() { time = (std::floor(time / ActLength) + 1) * ActLength + 2; }

void Flight::scene(int index) {
    time = std::floor(time / (ActLength * Acts)) * ActLength * Acts + std::clamp(index, 0, Acts - 1) * ActLength + 2;
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
