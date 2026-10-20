#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <array>
#include <memory>

class QAudioOutput;
class QMediaPlayer;
class QSoundEffect;

// Streams the bundled MP3 playlist and mixes short, preloaded gameplay cues.
// The player opens when music is first switched on, and the effects when
// they are; neither opens before.
class Audio : public QObject {
    Q_OBJECT
public:
    enum Effect { Boost, Ring, Impact, Wreck, EffectCount };
    static constexpr int TrackCount = 3;
    explicit Audio(QObject *parent = nullptr);
    ~Audio() override;

    bool running() const { return m_running; }
    QString error() const { return m_error; }
    int track() const { return m_track; }
    QString trackTitle() const;
    static QUrl trackSource(int track);
    static QUrl effectSource(Effect effect);

    // Music and effects switch on and off separately. Pausing the music keeps
    // its position; cues never replay on resume.
    void setRunning(bool music, bool effects);
    void setRunning(bool running) { setRunning(running, running); }
    bool musicPlaying() const { return m_running && m_music; }
    bool effectsPlaying() const { return m_running && m_effectsOn; }
    void nextTrack();
    void setVolume(float music, float effects);
    void play(Effect effect);

signals:
    void changed();

private:
    void fail(const QString &message);
    void openPlayer();
    void openEffects();
    void selectTrack();
    std::unique_ptr<QMediaPlayer> m_player;
    std::unique_ptr<QAudioOutput> m_output;
    std::array<std::unique_ptr<QSoundEffect>, EffectCount> m_effects;
    float m_musicVolume = 0.48f, m_effectVolume = 1.0f;
    int m_track = 0;
    bool m_running = false, m_music = false, m_effectsOn = false;
    QString m_error;
};
