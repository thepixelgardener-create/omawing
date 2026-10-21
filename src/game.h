#pragma once

// The arcade rules, played while you have the controls: rings score and
// chain a multiplier, scenery and ring rims cost hull, and the run ends when
// the hull gives out. The flight hands every step it doesn't fly on
// autopilot to play(). Pure simulation, like the flight.

#include <set>
#include <vector>

#include "geometry.h"

class Flight;
struct Ring;

// Points scored at a ring, floating up where they were won.
struct Popup {
    Vec position;
    int points = 0, multiplier = 1;
    double age = 0;
};

class Game {
public:
    static constexpr double FullHull = 100, WreckTime = 2.5;
    static constexpr int MaxMultiplier = 8;

    double hull = FullHull;
    int score = 0, multiplier = 1, ringsFlown = 0;
    double hurt = 0;     // Seconds left of the flicker after a hit, when nothing else can hurt.
    double wrecked = -1; // Seconds since the hull gave out, or -1.
    std::set<int> collected;
    std::vector<Popup> popups;

    bool wrecking() const { return wrecked >= 0; }
    // The run is over once the wreck has had time to fall; the app then
    // records the score and starts a new one.
    bool over() const { return wrecked >= WreckTime; }

    // A fresh hull and score.
    void reset() { *this = Game(); }
    // One step of the rules, after the flight moved the ship from `from`.
    void play(Flight &flight, double dt, const Vec &from);
    // Points float up and fade, in the demo too.
    void age(double dt);

private:
    void collectRings(Flight &flight, double dt, const Vec &from);
    void reward(Flight &flight, const Ring &ring, const Vec &velocity);
    void collide(Flight &flight);
    void damage(Flight &flight, double amount);
};
