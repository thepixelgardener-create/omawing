#pragma once

// The ship, its chase camera, and the timed demo sequence. Pure simulation:
// no Qt types, so tests can fly it without a window.

struct Ship {
    double x = 0, y = 3.2, z = 0, yaw = 0, bank = 0, pitch = 0, speed = 13, vertical = 0;
};

struct ChaseCamera {
    double x = 0, y = 7.5, z = -11.5, yaw = 0, pitch = -0.11;
};

class Flight {
public:
    // Each act lasts 24 seconds; the three acts loop every 72.
    static constexpr double ActLength = 24;
    static constexpr double MinAltitude = 1.4, MaxAltitude = 16;
    static constexpr double CruiseSpeed = 13, BoostSpeed = 27;

    enum Key { Left = 1, Right = 2, Climb = 4, Descend = 8, Boost = 16 };

    Ship ship;
    ChaseCamera camera;
    double time = 0;
    bool demo = true;

    int act() const;
    double boostAmount() const;

    void step(double dt);
    // Simulates any length of time in 1/60-second steps, so a jump forward
    // flies the same path as watching it.
    void advance(double seconds);
    void reset();
    void nextScene();
    void scene(int index);

    void press(Key key) { m_held |= key; }
    void release(Key key) { m_held &= ~key; }
    // Dragging steers like a joystick: x and y run from -1 to 1 away from
    // where the drag started.
    void setStick(bool active, double x = 0, double y = 0);
    void clearInput();
    bool holding(Key key) const { return m_held & key; }

private:
    int m_held = 0;
    bool m_stick = false;
    double m_stickX = 0, m_stickY = 0;
};
