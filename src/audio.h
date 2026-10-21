#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <array>
#include <deque>
#include <memory>

class QAudioBuffer;
class QAudioBufferOutput;
class QAudioOutput;
class QMediaPlayer;
class QSoundEffect;

// Listens to the soundtrack as it plays: how loud it is, and when the bass
// kicks. Samples come in ahead of time, stamped with where they sit in the
// track; take() hands out what the listener should be hearing by now.
// Plain arithmetic, so tests can feed it samples.
class BeatTracker {
public:
    struct Pulse {
        float level = 0;     // Loudness, roughly 0 to 1.
        bool beat = false;   // A kick drum (or anything bass-heavy) began.
    };

    void feed(const float *mono, int frames, int sampleRate, qint64 startMicroseconds);
    Pulse take(qint64 positionMicroseconds);
    void reset();

private:
    struct Cue {
        qint64 at;
        float level;
        bool beat;
    };
    static constexpr int Hop = 1024;   // About 23 ms at 44.1 kHz.
    std::deque<Cue> m_cues;
    double m_low = 0, m_average = 0, m_hopLow = 0, m_hopAll = 0;
    int m_hopFrames = 0;
    qint64 m_hopStart = -1, m_lastBeat = -1000000000;
    float m_level = 0;
};

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
    // The ring chime climbs a step for each ring in a chain (1 to 8).
    void playRing(int chain);
    static constexpr int RingSteps = 8;
    static QUrl ringSource(int chain);
    // What the music is doing right now, for the picture to move with it.
    // Quiet when the music is off.
    BeatTracker::Pulse pulse();

signals:
    void changed();

private:
    void fail(const QString &message);
    void stopEffects();
    void openPlayer();
    void openEffects();
    void selectTrack();
    void listen(const QAudioBuffer &buffer);
    std::unique_ptr<QMediaPlayer> m_player;
    std::unique_ptr<QAudioOutput> m_output;
    std::unique_ptr<QAudioBufferOutput> m_tap;
    BeatTracker m_beats;
    std::array<std::unique_ptr<QSoundEffect>, EffectCount> m_effects;
    // The higher chimes of a chain; the first is m_effects[Ring].
    std::array<std::unique_ptr<QSoundEffect>, RingSteps - 1> m_chimes;
    float m_musicVolume = 0.48f, m_effectVolume = 1.0f;
    int m_track = 0;
    bool m_running = false, m_music = false, m_effectsOn = false;
    QString m_error;
};
