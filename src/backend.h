#pragma once

#include <QHash>
#include <QObject>
#include <QSettings>
#include <QTimer>

#include "flight.h"
#include "scene.h"
#include "scoreboard.h"
#include "audio.h"

// The app's state for QML: the flight, pause, the demo autopilot, sound, the
// banner, and the high scores.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool paused READ paused NOTIFY changed)
    Q_PROPERTY(bool demo READ demo NOTIFY changed)
    Q_PROPERTY(bool music READ music NOTIFY changed)
    Q_PROPERTY(bool effects READ effects NOTIFY changed)
    Q_PROPERTY(double musicVolume READ musicVolume WRITE setMusicVolume NOTIFY volumeChanged)
    Q_PROPERTY(double effectsVolume READ effectsVolume WRITE setEffectsVolume NOTIFY volumeChanged)
    Q_PROPERTY(QString trackTitle READ trackTitle NOTIFY changed)
    Q_PROPERTY(bool banner READ banner NOTIFY changed)
    Q_PROPERTY(QString soundError READ soundError NOTIFY changed)
    Q_PROPERTY(QString settingsError READ settingsError NOTIFY changed)
    Q_PROPERTY(int fps READ fps NOTIFY fpsChanged)
    Q_PROPERTY(ScoreBoard *scoreBoard READ scoreBoardObject CONSTANT)

public:
    // Scores and preferences live in an INI file, by default
    // ~/.config/omawing/omawing.conf.
    explicit Backend(QObject *parent = nullptr, const QString &settingsFile = {});
    ~Backend() override;

    bool paused() const { return m_paused; }
    bool demo() const { return m_flight.demo; }
    bool music() const { return m_musicWanted; }
    bool effects() const { return m_effectsWanted; }
    // Slider positions from 0 to 1, on a loudness scale rather than a linear one.
    double musicVolume() const { return m_musicVolume; }
    double effectsVolume() const { return m_effectsVolume; }
    void setMusicVolume(double volume);
    void setEffectsVolume(double volume);
    bool banner() const { return m_banner; }
    QString trackTitle() const { return m_audio.trackTitle(); }
    QString soundError() const { return m_audio.error(); }
    // A failed save of preferences or of the score board, or empty.
    QString settingsError() const { return m_settingsError.isEmpty() ? m_board.error() : m_settingsError; }
    int fps() const { return m_fps; }
    // The simulated time of the last frame, in seconds.
    double lastStep() const { return m_lastStep; }
    ScoreBoard &scoreBoard() { return m_board; }
    const ScoreBoard &scoreBoard() const { return m_board; }

    const Flight &flight() const { return m_flight; }
    Flight &flight() { return m_flight; }
    Scene::Hud hud() const;
    Scene::Look look() const { return m_look; }
    void setTheme(const QHash<QString, QString> &colors, const QString &name);

    // Called every frame with the seconds since the last one.
    // With the display's refresh rate, steps snap to whole refreshes, so motion
    // doesn't judder with the timer's jitter.
    Q_INVOKABLE void tick(double seconds);
    void setRefreshRate(double hertz) { m_refreshRate = hertz; }
    Q_INVOKABLE void togglePause() { setPaused(!m_paused); }
    Q_INVOKABLE void setPaused(bool paused);
    Q_INVOKABLE void toggleDemo() { setDemo(!m_flight.demo); }
    Q_INVOKABLE void setDemo(bool demo);
    Q_INVOKABLE void nextScene();
    Q_INVOKABLE void restart();
    Q_INVOKABLE void toggleMusic();
    Q_INVOKABLE void toggleEffects();
    Q_INVOKABLE void nextTrack() { m_audio.nextTrack(); }
    Q_INVOKABLE void toggleBanner();

    // Flight keys take Qt key codes; they return whether the key steers.
    Q_INVOKABLE bool keyDown(int key);
    Q_INVOKABLE bool keyUp(int key);
    Q_INVOKABLE void stickStart();
    Q_INVOKABLE void stickMove(double x, double y);
    Q_INVOKABLE void stickEnd();
    Q_INVOKABLE void clearInput();
    // A minimized window stops the music rather than playing to nobody.
    Q_INVOKABLE void setHidden(bool hidden);

signals:
    void changed();
    void frameChanged();
    void fpsChanged();
    void volumeChanged();

private:
    void syncSound();
    void applyVolume();
    void saveVolume();
    // Ends the run in progress and offers its score to the board.
    void endRun();
    ScoreBoard *scoreBoardObject() { return &m_board; }
    void loadSettings();
    // Writes preferences and reports whether they reached the disk.
    bool store(std::initializer_list<std::pair<const char *, QVariant>> values);
    static int flightKey(int key);

    Flight m_flight;
    Audio m_audio;
    QString m_settingsFile, m_settingsError;
    Scene::Look m_look;
    ScoreBoard m_board;
    bool m_paused = false, m_musicWanted = false, m_effectsWanted = false, m_hidden = false, m_banner = true;
    int m_frames = 0, m_fps = 0;
    double m_elapsed = 0, m_lastStep = 0, m_drift = 0, m_refreshRate = 0;
    double m_musicVolume = 0.8, m_effectsVolume = 0.8;
    QTimer m_saveVolume;
};
