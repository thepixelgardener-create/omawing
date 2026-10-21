#pragma once

// The ship, its chase camera, the world's scenery, particles, and the timed
// demo sequence; the arcade rules are in Game. Pure simulation: no Qt types,
// so tests can fly it without a window.

#include <vector>

#include "game.h"
#include "geometry.h"

struct Ship {
    double x = 0, y = 3.2, z = 0, yaw = 0, bank = 0, pitch = 0, speed = 13, vertical = 0;
    // The wing tips hang on springs: they lag behind turns and climbs, swing
    // past, and settle. Sweep folds the wings back while boosting.
    double flexLeft = 0, flexRight = 0, flexLeftRate = 0, flexRightRate = 0, sweep = 0;
};

struct ChaseCamera {
    double x = 0, y = 8.2, z = -17.5, yaw = 0, pitch = -0.055;
    // How far the camera has slid sideways, for the parallax background, and
    // how hard it is shaking after a hit.
    double drift = 0, shake = 0;
};

struct Particle {
    // A flash lights a ring as you fly through it.
    enum Kind { Exhaust, Spark, Glitter, Smoke, Flash };
    Vec position, velocity;
    double age = 0, life = 1, size = 0.3;
    Kind kind = Exhaust;
};

struct Obstacle {
    enum Kind { Pyramid, Crystal, Ball };
    Kind kind;
    double x, y, z, spin = 0;
    int cellX = 0, cellZ = 0;
};

struct Ring {
    int index;
    double x, y, z;
};

// Where the scenery stands. The renderer draws these and the flight collides
// with them, so what you see is what you hit.
namespace World {
constexpr double Cell = 24, RingSpacing = 32, RingRadius = 5, RingHeight = 5.5;
constexpr double PyramidHalf = 5, PyramidHeight = 8, BallRadius = 3.2;
// The pyramids and crystals in the cells within `reach` of (x, z).
std::vector<Obstacle> scenery(double x, double z, int reach, double time);
Ring ring(int index);
// One beach ball bounces beside the course every 110 units.
Obstacle ball(double z, double time);
}

class Flight {
public:
    // Each act lasts 24 seconds, and the acts loop.
    static constexpr int Acts = 9;
    static constexpr double ActLength = 24;
    static constexpr double MinAltitude = 1.4, MaxAltitude = 16;
    static constexpr double CruiseSpeed = 13, BoostSpeed = 27;
    static constexpr int MaxParticles = 700;

    enum Key { Left = 1, Right = 2, Climb = 4, Descend = 8, Boost = 16 };

    enum Event { RingPassed = 1, Impact = 2, Wreck = 4, BoostStarted = 8 };
    // The cues of the last step (or advance), for the sound effects.
    unsigned events = 0;

    Ship ship;
    ChaseCamera camera;
    double time = 0;
    bool demo = true;

    // The run in progress, played while you have the controls.
    Game game;
    std::vector<Particle> particles;

    int act() const;
    double boostAmount() const;

    void step(double dt);
    // Simulates any length of time in 1/60-second steps, so a jump forward
    // flies the same path as watching it.
    void advance(double seconds);
    void reset();
    // A fresh ship and score where the old one was, keeping the clock.
    void newRun();
    void nextScene();
    void scene(int index);

    void press(Key key) { m_held |= key; }
    void release(Key key) { m_held &= ~key; }
    // Dragging steers like a joystick: x and y run from -1 to 1 away from
    // where the drag started.
    void setStick(bool active, double x = 0, double y = 0);
    void clearInput();
    bool holding(Key key) const { return m_held & key; }

    // Where the two engines' nozzles are, in the world.
    std::array<Vec, 2> nozzles() const;

    // Effects: `count` particles from `at`, moving at `velocity` plus up to
    // `spread` in a random direction.
    void emitParticles(Particle::Kind kind, const Vec &at, const Vec &velocity, double spread, int count);
    // A number in [0, 1) from a fixed seed, so a flight replays exactly.
    double random();

private:
    void steer(double dt, double steer, double climb, bool boost);
    void follow(double dt);
    void animateParticles(double dt);

    int m_held = 0;
    bool m_stick = false, m_boosting = false;
    double m_stickX = 0, m_stickY = 0;
    double m_exhaust = 0;
    unsigned m_seed = 1;
};
