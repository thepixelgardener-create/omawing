#include "backend.h"

#include <QLocale>
#include <algorithm>
#include <cmath>

Backend::Backend(QObject *parent) : QObject(parent) {
    connect(&m_synth, &Synth::changed, this, [this] {
        // A failed device turns the sound off, so the button tells the truth.
        if (!m_synth.error().isEmpty())
            m_soundWanted = false;
        emit changed();
    });
}

Scene::Hud Backend::hud() const {
    const QLocale locale;
    const double boost = m_flight.boostAmount();
    const QString status = m_paused ? tr("PAUSED")
                         : m_flight.demo ? tr("AUTOPILOT / ENJOY THE RIDE")
                         : boost > 0.6 ? tr("BOOST ENGAGED")
                         : tr("YOU HAVE THE CONTROLS");
    const int second = int(std::floor(std::fmod(m_flight.time, Flight::ActLength)));
    const QString clock = locale.toString(second).rightJustified(2, locale.zeroDigit().at(0)) + " / " + locale.toString(24);
    const QString mode = (m_flight.demo ? tr("DEMO SEQUENCE") : tr("FREE FLIGHT")) + QString::fromUtf8(" · ") + clock;
    QString altitude = locale.toString(m_flight.ship.y, 'f', 1);
    if (m_flight.ship.y < 10)
        altitude.prepend(locale.zeroDigit());
    return {status, mode, tr("THRUST"), tr("ALT %1").arg(altitude), tr("A LOVE LETTER TO THE DEMOSCENE")};
}

void Backend::tick(double seconds) {
    const double dt = std::clamp(seconds, 0.0, 0.05);
    if (!m_paused) {
        m_flight.step(dt);
        emit frameChanged();
    }
    ++m_frames;
    m_elapsed += seconds;
    if (m_elapsed > 0.75) {
        m_fps = int(std::lround(m_frames / m_elapsed));
        m_frames = 0;
        m_elapsed = 0;
        emit fpsChanged();
    }
}

void Backend::setPaused(bool paused) {
    m_paused = paused;
    m_flight.clearInput();
    syncSound();
    emit changed();
    emit frameChanged();
}

void Backend::setDemo(bool demo) {
    m_flight.demo = demo;
    m_flight.clearInput();
    emit changed();
    emit frameChanged();
}

void Backend::nextScene() {
    m_flight.nextScene();
    emit frameChanged();
}

// Restarts the flight and the sequence, keeping pause and sound as they are.
void Backend::restart() {
    m_flight.reset();
    emit changed();
    emit frameChanged();
}

void Backend::toggleSound() {
    m_soundWanted = !m_soundWanted;
    syncSound();
    emit changed();
}

void Backend::syncSound() { m_synth.setRunning(m_soundWanted && !m_paused && !m_hidden); }

void Backend::setHidden(bool hidden) {
    m_hidden = hidden;
    clearInput();
    syncSound();
}

int Backend::flightKey(int key) {
    switch (key) {
    case Qt::Key_A: case Qt::Key_Left: return Flight::Left;
    case Qt::Key_D: case Qt::Key_Right: return Flight::Right;
    case Qt::Key_W: case Qt::Key_Up: return Flight::Climb;
    case Qt::Key_S: case Qt::Key_Down: return Flight::Descend;
    case Qt::Key_Shift: return Flight::Boost;
    default: return 0;
    }
}

bool Backend::keyDown(int key) {
    const int flag = flightKey(key);
    if (!flag)
        return false;
    if (m_paused)
        return true;
    // Any flight key takes over from the autopilot.
    if (m_flight.demo)
        setDemo(false);
    m_flight.press(Flight::Key(flag));
    return true;
}

bool Backend::keyUp(int key) {
    const int flag = flightKey(key);
    if (flag)
        m_flight.release(Flight::Key(flag));
    return flag;
}

void Backend::stickStart() {
    if (m_paused)
        return;
    setDemo(false);
    m_flight.setStick(true);
}

void Backend::stickMove(double x, double y) {
    if (!m_paused && !m_flight.demo)
        m_flight.setStick(true, x, y);
}

void Backend::stickEnd() { m_flight.setStick(false); }

void Backend::clearInput() { m_flight.clearInput(); }
