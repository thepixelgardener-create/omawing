#pragma once

#include <QIODevice>
#include <QObject>
#include <QString>
#include <memory>
#include <vector>

class QAudioSink;

// The chip-style loop, as plain samples. It schedules each note at an exact
// sample, so the rhythm holds however the audio system asks for data.
class Tune {
public:
    static constexpr int SampleRate = 48000;
    static constexpr double Step = 0.125;   // One sixteenth note, in seconds.

    // Fills `frames` mono samples in -1..1 and moves the song forward.
    void render(float *out, int frames);
    void restart();
    int notesPlayed() const { return m_note; }

private:
    enum Wave { Square, Triangle, Sine };
    struct Voice {
        double frequency, start, duration, volume;
        Wave wave;
    };
    void schedule(double at);

    std::vector<Voice> m_voices;
    long long m_sample = 0;
    int m_note = 0;
    double m_nextNote = 0.03;
};

// Pulls samples from a Tune for the audio sink.
class TuneDevice : public QIODevice {
public:
    explicit TuneDevice(Tune &tune, QObject *parent = nullptr);
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return 1 << 16; }

protected:
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *, qint64) override { return -1; }

private:
    Tune &m_tune;
    std::vector<float> m_buffer;
};

// Plays the Tune through the default audio output. Sound stays off until
// the user turns it on, and a missing device is reported, not ignored.
class Synth : public QObject {
    Q_OBJECT

public:
    explicit Synth(QObject *parent = nullptr);
    ~Synth() override;

    bool running() const { return m_running; }
    QString error() const { return m_error; }
    // Starts or suspends output; the first start opens the device.
    void setRunning(bool running);

signals:
    void changed();

private:
    void fail(const QString &message);

    Tune m_tune;
    std::unique_ptr<TuneDevice> m_device;
    std::unique_ptr<QAudioSink> m_sink;
    bool m_running = false;
    QString m_error;
};
