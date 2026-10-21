#include "game.h"

#include <algorithm>
#include <cmath>

#include "flight.h"

using namespace geo;

void Game::play(Flight &flight, double dt, const Vec &from) {
    hurt = std::max(0.0, hurt - dt);
    if (wrecking()) {
        wrecked += dt;
        const Ship &ship = flight.ship;
        if (flight.random() < dt * 40)
            flight.emitParticles(Particle::Smoke, {ship.x, ship.y + 0.3, ship.z}, {0, 1.5, 0}, 0.8, 1);
        return;
    }
    collectRings(flight, dt, from);
    if (hurt <= 0)
        collide(flight);
}

// Rings whose plane the ship crossed this step, in either direction.
void Game::collectRings(Flight &flight, double dt, const Vec &from) {
    const Ship &ship = flight.ship;
    const double px = from[0], py = from[1], pz = from[2];
    const double lo = std::min(pz, ship.z), hi = std::max(pz, ship.z);
    for (int i = int(std::floor((lo - 24) / World::RingSpacing)); i <= int(std::ceil((hi - 24) / World::RingSpacing)); ++i) {
        const Ring r = World::ring(i);
        if ((pz < r.z) == (ship.z < r.z) || collected.count(i))
            continue;
        const double t = (r.z - pz) / (ship.z - pz);
        const double off = std::hypot(mix(px, ship.x, t) - r.x, mix(py, ship.y, t) - r.y);
        if (off < World::RingRadius - 0.75) {
            const int points = 100 * multiplier * (flight.boostAmount() > 0.6 ? 2 : 1);
            score += points;
            flight.events |= Flight::RingPassed;
            popups.push_back({{r.x, r.y + 1.5, r.z}, points, multiplier, 0});
            multiplier = std::min(MaxMultiplier, multiplier + 1);
            ++ringsFlown;
            collected.insert(i);
            hull = std::min(FullHull, hull + 3);
            reward(flight, r, {(ship.x - px) / dt, (ship.y - py) / dt, (ship.z - pz) / dt});
        } else if (off < World::RingRadius + 0.75) {
            if (hurt <= 0)
                damage(flight, 12);
            multiplier = 1;
        } else {
            // Passing a ring by breaks the chain.
            multiplier = 1;
        }
    }
}

// The ring lights up and bursts: a flash in its middle, a ring of glitter
// thrown outwards in its plane, and a stream of sparks that keeps pace with
// the ship for a moment before falling behind.
void Game::reward(Flight &flight, const Ring &r, const Vec &velocity) {
    flight.emitParticles(Particle::Flash, {r.x, r.y, r.z}, scaled(velocity, 0.3), 0, 1);
    for (int k = 0; k < 48; ++k) {
        const double a = k * 2 * M_PI / 48;
        flight.emitParticles(Particle::Glitter,
                             {r.x + std::cos(a) * World::RingRadius, r.y + std::sin(a) * World::RingRadius, r.z},
                             translated({std::cos(a) * 7, std::sin(a) * 7, 0}, scaled(velocity, 0.3)), 0.8, 1);
    }
    const Ship &ship = flight.ship;
    flight.emitParticles(Particle::Glitter, {ship.x, ship.y, ship.z}, scaled(velocity, 0.85), 2.5, 18);
}

void Game::collide(Flight &flight) {
    const Ship &ship = flight.ship;
    auto hits = [&](const Obstacle &o) {
        const double dx = ship.x - o.x, dy = ship.y - o.y, dz = ship.z - o.z;
        switch (o.kind) {
        case Obstacle::Pyramid: {
            const double edge = std::max(0.0, std::max(std::abs(dx), std::abs(dz)) - 0.7);
            return edge < World::PyramidHalf && dy < World::PyramidHeight * (1 - edge / World::PyramidHalf) + 0.3;
        }
        case Obstacle::Crystal:
            return std::hypot(dx, dz) < 2.1 && dy > -1.6 && dy < 4.6;
        default:
            return std::hypot(dx, dy, dz) < World::BallRadius + 0.9;
        }
    };
    auto obstacles = World::scenery(ship.x, ship.z, 1, flight.time);
    obstacles.push_back(World::ball(ship.z, flight.time));
    for (const Obstacle &o : obstacles)
        if (hits(o)) {
            damage(flight, o.kind == Obstacle::Pyramid ? 34 : o.kind == Obstacle::Ball ? 25 : 20);
            return;
        }
}

void Game::damage(Flight &flight, double amount) {
    Ship &ship = flight.ship;
    const Vec at{ship.x, ship.y, ship.z};
    flight.events |= Flight::Impact;
    hull = std::max(0.0, hull - amount);
    hurt = 1.4;
    multiplier = 1;
    flight.camera.shake = 1;
    ship.speed *= 0.55;
    // Knocked upwards, with the wings ringing.
    ship.vertical = 6;
    ship.flexLeftRate += 4;
    ship.flexRightRate -= 4;
    flight.emitParticles(Particle::Spark, at, {0, 3, 0}, 9, 36);
    if (hull <= 0) {
        wrecked = 0;
        flight.events |= Flight::Wreck;
        flight.emitParticles(Particle::Spark, at, {0, 4, 0}, 13, 60);
    }
}

void Game::age(double dt) {
    for (Popup &popup : popups) {
        popup.age += dt;
        popup.position[1] += dt * 2;
    }
    std::erase_if(popups, [](const Popup &popup) { return popup.age >= 1.4; });
}
