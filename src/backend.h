#pragma once

#include <QHash>
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QVariantList>

#include "flight.h"
#include "scene.h"
#include "synth.h"

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
    Q_PROPERTY(QVariantList scores READ scores NOTIFY scoresChanged)
    Q_PROPERTY(int pendingScore READ pendingScore NOTIFY scoresChanged)
    Q_PROPERTY(QString playerName READ playerName NOTIFY scoresChanged)
    Q_PROPERTY(int nameLength READ nameLength CONSTANT)

public:
    static constexpr int BoardSize = 10, NameLength = 12;

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
    QString trackTitle() const { return m_synth.trackTitle(); }
    QString soundError() const { return m_synth.error(); }
    QString settingsError() const { return m_settingsError; }
    int fps() const { return m_fps; }
    // The simulated time of the last frame, in seconds.
    double lastStep() const { return m_lastStep; }
    // The board, best first: maps with name, score, rings, and date.
    QVariantList scores() const { return m_scores; }
    // A finished run that made the board and is waiting for a name, or 0.
    int pendingScore() const { return m_pending.score; }
    QString playerName() const { return m_playerName; }
    int nameLength() const { return NameLength; }
    int best() const;

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
    Q_INVOKABLE void nextTrack() { m_synth.nextTrack(); }
    Q_INVOKABLE void toggleBanner();
    // Puts the waiting score on the board; an empty name uses the last one.
    Q_INVOKABLE void saveScore(const QString &name);

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
    void scoresChanged();
    void volumeChanged();

private:
    struct Run {
        int score = 0, rings = 0;
    };
    void syncSound();
    void applyVolume();
    void saveVolume();
    // Ends the run in progress, holding its score for a name if it made the board.
    void endRun();
    bool qualifies(int score) const;
    bool record(const Run &run, const QString &name);
    void loadSettings();
    // Writes preferences and reports whether they reached the disk.
    bool store(std::initializer_list<std::pair<const char *, QVariant>> values);
    bool saved(QSettings &settings);
    static int flightKey(int key);

    Flight m_flight;
    Synth m_synth;
    QString m_settingsFile, m_settingsError;
    Scene::Look m_look;
    QVariantList m_scores;
    Run m_pending;
    QString m_playerName;
    bool m_paused = false, m_musicWanted = false, m_effectsWanted = false, m_hidden = false, m_banner = true;
    int m_frames = 0, m_fps = 0;
    double m_elapsed = 0, m_lastStep = 0, m_drift = 0, m_refreshRate = 0;
    double m_musicVolume = 0.8, m_effectsVolume = 0.8;
    QTimer m_saveVolume;
};
