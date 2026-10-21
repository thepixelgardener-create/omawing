#include "backend.h"

#include <QDateTime>
#include <QLocale>
#include <QSettings>
#include <QtAudio>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

namespace {
// Six digits, zero-padded in the reader's numerals.
QString digits(const QLocale &locale, int value) {
    return locale.toString(value).remove(locale.groupSeparator()).rightJustified(6, locale.zeroDigit().at(0));
}
}

Backend::Backend(QObject *parent, const QString &settingsFile)
    : QObject(parent),
      m_settingsFile(!settingsFile.isEmpty() ? settingsFile
                     : QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/omawing.conf"),
      m_board(m_settingsFile) {
    connect(&m_board, &ScoreBoard::changed, this, &Backend::changed);
    connect(&m_audio, &Audio::changed, this, [this] {
        // A failed device turns the sound off, so the button tells the truth.
        if (!m_audio.error().isEmpty())
            m_musicWanted = m_effectsWanted = false;
        emit changed();
    });
    // Sliders move in many small steps; the levels are written once they settle.
    m_saveVolume.setSingleShot(true);
    m_saveVolume.setInterval(400);
    connect(&m_saveVolume, &QTimer::timeout, this, &Backend::saveVolume);
    loadSettings();
    applyVolume();
    syncSound();
}

// Quitting mid-run still keeps a score that made the board.
Backend::~Backend() {
    if (m_saveVolume.isActive())
        saveVolume();
    endRun();
}

void Backend::loadSettings() {
    QSettings settings(m_settingsFile, QSettings::IniFormat);
    m_banner = settings.value("banner", true).toBool();
    // "interface" switched both, before titles and toolbar had their own.
    const bool both = settings.value("interface", true).toBool();
    m_titlesOn = settings.value("titles", both).toBool();
    m_toolbar = settings.value("toolbar", both).toBool();
    m_titles = m_titlesOn ? 1 : 0;
    updateLook();
    m_musicWanted = settings.value("music", false).toBool();
    m_effectsWanted = settings.value("effects", false).toBool();
    m_musicVolume = std::clamp(settings.value("musicVolume", 0.8).toDouble(), 0.0, 1.0);
    m_effectsVolume = std::clamp(settings.value("effectsVolume", 0.8).toDouble(), 0.0, 1.0);
}

bool Backend::store(std::initializer_list<std::pair<const char *, QVariant>> values) {
    QSettings settings(m_settingsFile, QSettings::IniFormat);
    for (const auto &[key, value] : values)
        settings.setValue(key, value);
    settings.sync();
    const QString error = settings.status() == QSettings::NoError ? QString()
                        : tr("Could not save settings. Check folder permissions.");
    if (error != m_settingsError) {
        m_settingsError = error;
        emit changed();
    }
    return error.isEmpty();
}

void Backend::zoom(double steps) {
    const double before = m_flight.cameraDistance();
    m_flight.zoom(steps);
    if (before != m_flight.cameraDistance()) m_zoomFor = 3;
}

void Backend::resetZoom() {
    const double before = m_flight.cameraDistance();
    m_flight.resetZoom();
    if (before != m_flight.cameraDistance()) m_zoomFor = 3;
}

void Backend::endRun() {
    if (m_flight.demo && m_flight.game.score == 0)
        return;
    const int score = m_flight.game.score, rings = m_flight.game.ringsFlown;
    m_flight.newRun();
    m_board.offer(score, rings);
}

Scene::Hud Backend::hud() const {
    const QLocale locale;
    const double boost = m_flight.boostAmount();
    const QString status = m_paused ? tr("PAUSED")
                         : m_flight.demo ? tr("AUTOPILOT / ENJOY THE RIDE")
                         : boost > 0.6 ? tr("BOOST ENGAGED")
                         : tr("YOU HAVE THE CONTROLS");
    const int second = int(std::floor(std::fmod(m_flight.time, Flight::ActLength)));
    const QString clock = locale.toString(second).rightJustified(2, locale.zeroDigit().at(0)) + " / " + locale.toString(int(Flight::ActLength));
    const QString mode = (m_flight.demo ? tr("DEMO SEQUENCE") : tr("FREE FLIGHT")) + QString::fromUtf8(" · ") + clock;
    QString altitude = locale.toString(m_flight.ship.y, 'f', 1);
    if (m_flight.ship.y < 10)
        altitude.prepend(locale.zeroDigit());

    Scene::Hud hud{status, mode, tr("THRUST"), tr("ALT %1").arg(altitude), tr("Flying into the future")};
    const bool playing = !m_flight.demo;
    const int top = std::max(m_board.best(), playing ? m_flight.game.score : 0);
    if (playing) {
        hud.hull = tr("HULL");
        hud.score = tr("SCORE");
        hud.points = digits(locale, m_flight.game.score);
    }
    if (top > 0)
        hud.best = tr("BEST %1").arg(digits(locale, top));
    if (m_flight.game.wrecked >= 0)
        hud.alert = tr("HULL BREACHED");
    hud.titlesSwitch = tr("TITLES");
    hud.toolbarSwitch = tr("MENU");
    hud.bannerSwitch = tr("BANNER");
    return hud;
}

void Backend::setTheme(const QHash<QString, QString> &colors, const QString &name) {
    const auto palette = Scene::themeColors(colors);
    // Picking a new Omarchy theme flies straight into its scene, fading in
    // from the scene before it. The first reading at startup doesn't jump.
    const bool switched = m_look.theme[0].isValid() && (palette != m_look.theme || name != m_look.themeName);
    m_look.theme = palette;
    m_look.themeName = name;
    if (switched) {
        m_flight.scene(Scene::ThemeAct);
        m_flight.time -= 2;
    }
    emit frameChanged();
}

void Backend::tick(double seconds) {
    const double refreshRate = m_refreshRate;
    if (!std::isfinite(seconds) || seconds <= 0 || m_hidden)
        return;
    double dt = std::clamp(seconds, 0.0, 0.05);
    if (refreshRate >= 20 && refreshRate <= 500) {
        // Frames reach the screen on its beat, but the timer measuring them
        // wobbles by a few milliseconds. Stepping by whole refreshes moves the
        // world evenly; what the timer measured beyond that is paid back a
        // little each frame, so the clock never drifts.
        const double period = 1 / refreshRate;
        const double snapped = std::min(0.05, std::max(1.0, std::round(dt / period)) * period);
        m_drift = std::clamp(m_drift + dt - snapped, -0.25, 0.25);
        const double payback = std::clamp(m_drift * 0.05, -0.03 * period, 0.03 * period);
        m_drift -= payback;
        dt = snapped + payback;
    }
    m_lastStep = dt;
    // The titles fade with their switch, and the switch keys fade in for a
    // moment after a change, paused or not.
    const double titles = m_titlesOn ? 1 : 0;
    m_switchesFor = std::max(0.0, m_switchesFor - seconds);
    const double switches = m_switchesFor > 0 ? 1 : 0;
    if (m_titles != titles || m_switches != switches) {
        auto ease = [dt](double from, double to) {
            return std::abs(to - from) < 0.01 ? to : from + (to - from) * (1 - std::exp(-dt * 8));
        };
        m_titles = ease(m_titles, titles);
        m_switches = ease(m_switches, switches);
        updateLook();
        if (m_paused)
            emit frameChanged();
    }
    if (!m_paused) {
        m_flight.step(dt);
        const unsigned cues = m_flight.events;
        if (cues & Flight::Wreck) m_audio.play(Audio::Wreck);
        else if (cues & Flight::Impact) m_audio.play(Audio::Impact);
        // Each ring of a chain chimes a step higher.
        if (cues & Flight::RingPassed)
            m_audio.playRing(m_flight.game.popups.empty() ? 1 : m_flight.game.popups.back().multiplier);
        if (cues & Flight::BoostStarted) m_audio.play(Audio::Boost);
        // The picture moves with the music: a kick makes the crystals swell.
        const BeatTracker::Pulse pulse = m_audio.pulse();
        updatePulse(dt, pulse.beat, pulse.level);
        // A wreck that has finished falling ends the run, and the autopilot
        // takes over again.
        if (m_flight.game.over())
            setDemo(true);
        emit frameChanged();
    }
    if (m_paused && m_zoomFor > 0) {
        m_flight.updateCamera(dt);
        emit frameChanged();
    }
    m_zoomFor = std::max(0.0, m_zoomFor - dt);
    ++m_frames;
    m_elapsed += seconds;
    if (m_elapsed > 0.75) {
        m_fps = int(std::lround(m_frames / m_elapsed));
        m_frames = 0;
        m_elapsed = 0;
        emit fpsChanged();
    }
}

// The beat as the picture shows it: a swell, not a flash. Each kick sets a
// target that the crystals rise to over about a tenth of a second and fall
// from slowly, and kicks closer than MinBeatGap apart are let pass, so it
// never pulses more than about twice a second, well under the three flashes
// a second that photosensitivity guidelines warn about.
void Backend::updatePulse(double dt, bool beat, double level) {
    m_sinceBeat += dt;
    if (beat && m_sinceBeat >= MinBeatGap) {
        m_beatTarget = 1;
        m_sinceBeat = 0;
    }
    m_beatTarget *= std::exp(-dt * 3);
    m_beat += (m_beatTarget - m_beat) * (1 - std::exp(-dt * 22));
    m_level += (level - m_level) * (1 - std::exp(-dt * 6));
    m_look.beat = m_beat;
    m_look.level = m_level;
}

void Backend::setPaused(bool paused, bool keepMusic) {
    m_paused = paused;
    m_musicThroughPause = paused && keepMusic;
    m_flight.clearInput();
    syncSound();
    emit changed();
    emit frameChanged();
}

void Backend::setDemo(bool demo) {
    if (demo && !m_flight.demo)
        endRun();
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
    endRun();
    m_flight.reset();
    emit changed();
    emit frameChanged();
}

void Backend::toggleMusic() {
    m_musicWanted = !m_musicWanted;
    store({{"music", m_musicWanted}});
    syncSound();
    emit changed();
}

void Backend::saveVolume() {
    m_saveVolume.stop();
    store({{"musicVolume", m_musicVolume}, {"effectsVolume", m_effectsVolume}});
}

void Backend::setMusicVolume(double volume) {
    volume = std::clamp(volume, 0.0, 1.0);
    if (volume == m_musicVolume)
        return;
    m_musicVolume = volume;
    applyVolume();
    m_saveVolume.start();
    emit volumeChanged();
}

void Backend::setEffectsVolume(double volume) {
    volume = std::clamp(volume, 0.0, 1.0);
    if (volume == m_effectsVolume)
        return;
    m_effectsVolume = volume;
    applyVolume();
    m_saveVolume.start();
    emit volumeChanged();
}

// The sliders follow loudness; the player wants linear gain. The music sits
// a little under the effects, so cues cut through it at equal settings.
void Backend::applyVolume() {
    auto gain = [](double v) {
        return QtAudio::convertVolume(float(v), QtAudio::LogarithmicVolumeScale, QtAudio::LinearVolumeScale);
    };
    m_audio.setVolume(gain(m_musicVolume) * 0.6f, gain(m_effectsVolume));
}

void Backend::toggleEffects() {
    m_effectsWanted = !m_effectsWanted;
    store({{"effects", m_effectsWanted}});
    syncSound();
    emit changed();
}

void Backend::toggleTitles() {
    m_titlesOn = !m_titlesOn;
    store({{"titles", m_titlesOn}});
    showSwitches(3);
    emit changed();
}

void Backend::toggleToolbar() {
    m_toolbar = !m_toolbar;
    store({{"toolbar", m_toolbar}});
    showSwitches(3);
    emit changed();
}

void Backend::showSwitches(double seconds) {
    m_switchesFor = std::max(m_switchesFor, seconds);
    updateLook();
}

void Backend::updateLook() {
    m_look.banner = m_banner;
    m_look.titles = m_titles;
    m_look.switches = m_switches;
    m_look.titlesOn = m_titlesOn;
    m_look.toolbarOn = m_toolbar;
}

void Backend::toggleBanner() {
    m_banner = !m_banner;
    store({{"banner", m_banner}});
    showSwitches(3);
    emit changed();
    emit frameChanged();
}

void Backend::syncSound() {
    const bool audible = !m_paused && !m_hidden;
    m_audio.setRunning(m_musicWanted && !m_hidden && (!m_paused || m_musicThroughPause), m_effectsWanted && audible);
}

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

void Backend::mouseBoost(bool down) {
    if (!down) {
        m_flight.release(Flight::MouseBoost);
        return;
    }
    if (m_paused)
        return;
    if (m_flight.demo)
        setDemo(false);
    m_flight.press(Flight::MouseBoost);
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
