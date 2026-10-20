#pragma once

#include <QObject>

#include "flight.h"
#include "scene.h"
#include "synth.h"

// The app's state for QML: the flight, pause, the demo autopilot, and sound.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool paused READ paused NOTIFY changed)
    Q_PROPERTY(bool demo READ demo NOTIFY changed)
    Q_PROPERTY(bool sound READ sound NOTIFY changed)
    Q_PROPERTY(QString soundError READ soundError NOTIFY changed)
    Q_PROPERTY(int fps READ fps NOTIFY fpsChanged)

public:
    explicit Backend(QObject *parent = nullptr);

    bool paused() const { return m_paused; }
    bool demo() const { return m_flight.demo; }
    bool sound() const { return m_soundWanted; }
    QString soundError() const { return m_synth.error(); }
    int fps() const { return m_fps; }

    const Flight &flight() const { return m_flight; }
    Flight &flight() { return m_flight; }
    Scene::Hud hud() const;

    // Called every frame with the seconds since the last one.
    Q_INVOKABLE void tick(double seconds);
    Q_INVOKABLE void togglePause() { setPaused(!m_paused); }
    Q_INVOKABLE void setPaused(bool paused);
    Q_INVOKABLE void toggleDemo() { setDemo(!m_flight.demo); }
    Q_INVOKABLE void setDemo(bool demo);
    Q_INVOKABLE void nextScene();
    Q_INVOKABLE void restart();
    Q_INVOKABLE void toggleSound();

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

private:
    void syncSound();
    static int flightKey(int key);

    Flight m_flight;
    Synth m_synth;
    bool m_paused = false, m_soundWanted = false, m_hidden = false;
    int m_frames = 0, m_fps = 0;
    double m_elapsed = 0;
};
