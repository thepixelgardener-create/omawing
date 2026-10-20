#include "synth.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QCoreApplication>
#include <QMediaDevices>
#include <algorithm>
#include <cmath>

namespace {
double midiToFrequency(int midi) { return 440 * std::pow(2.0, (midi - 69) / 12.0); }
}

// Every sixteenth note plays an arpeggio step; every beat adds a bass note
// and a soft kick. Four chords of four beats each make one bar of the loop.
void Tune::schedule(double at) {
    static const int roots[] = {45, 41, 48, 43}, scale[] = {0, 7, 12, 16, 12, 7, 19, 16};
    const int base = roots[(m_note / 16) % 4];
    m_voices.push_back({midiToFrequency(base + 24 + scale[m_note % 8]), at, 0.14, 0.1, Square});
    if (m_note % 4 == 0) {
        m_voices.push_back({midiToFrequency(base), at, 0.34, 0.45, Triangle});
        m_voices.push_back({65, at, 0.09, 0.6, Sine});
    }
    ++m_note;
}

void Tune::render(float *out, int frames) {
    for (int i = 0; i < frames; ++i, ++m_sample) {
        const double t = double(m_sample) / SampleRate;
        while (m_nextNote <= t) {
            schedule(m_nextNote);
            m_nextNote += Step;
        }
        double sum = 0;
        for (const Voice &v : m_voices) {
            const double local = t - v.start;
            if (local < 0 || local > v.duration)
                continue;
            // An 8 ms attack, then an exponential fall to near silence, the
            // same envelope the SVG edition gave Web Audio.
            const double level = local < 0.008 ? v.volume * local / 0.008
                               : v.volume * std::pow(0.0001 / v.volume, (local - 0.008) / (v.duration - 0.008));
            const double cycle = v.frequency * local, phase = cycle - std::floor(cycle);
            const double wave = v.wave == Square ? (phase < 0.5 ? 1.0 : -1.0)
                              : v.wave == Triangle ? 4 * std::abs(phase - 0.5) - 1
                              : std::sin(2 * M_PI * cycle);
            sum += wave * level;
        }
        out[i] = float(std::clamp(sum * 0.2, -1.0, 1.0));
        if ((m_sample & 1023) == 0)
            std::erase_if(m_voices, [t](const Voice &v) { return t - v.start > v.duration; });
    }
}

void Tune::restart() {
    m_voices.clear();
    m_sample = 0;
    m_note = 0;
    m_nextNote = 0.03;
}

TuneDevice::TuneDevice(Tune &tune, QObject *parent) : QIODevice(parent), m_tune(tune) {}

qint64 TuneDevice::readData(char *data, qint64 maxSize) {
    const int frames = int(maxSize / qint64(sizeof(qint16)));
    m_buffer.resize(frames);
    m_tune.render(m_buffer.data(), frames);
    auto *samples = reinterpret_cast<qint16 *>(data);
    for (int i = 0; i < frames; ++i)
        samples[i] = qint16(std::lround(m_buffer[i] * 32767));
    return qint64(frames) * qint64(sizeof(qint16));
}

Synth::Synth(QObject *parent) : QObject(parent) {}

Synth::~Synth() {
    if (m_sink)
        m_sink->stop();
}

void Synth::fail(const QString &message) {
    m_running = false;
    m_error = message;
    emit changed();
}

void Synth::setRunning(bool running) {
    if (running == m_running && m_error.isEmpty())
        return;
    if (!running) {
        if (m_sink)
            m_sink->suspend();
        m_running = false;
        emit changed();
        return;
    }
    m_error.clear();
    if (!m_sink) {
        const QAudioDevice output = QMediaDevices::defaultAudioOutput();
        if (output.isNull())
            return fail(QCoreApplication::translate("Synth", "No audio output device was found."));
        QAudioFormat format;
        format.setSampleRate(Tune::SampleRate);
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Int16);
        if (!output.isFormatSupported(format))
            return fail(QCoreApplication::translate("Synth", "The audio output can't play this sound."));
        m_sink = std::make_unique<QAudioSink>(output, format);
        // About 100 ms of buffer: short enough that pause feels immediate.
        m_sink->setBufferSize(Tune::SampleRate / 10 * qsizetype(sizeof(qint16)));
        connect(m_sink.get(), &QAudioSink::stateChanged, this, [this] {
            if (m_sink->error() != QtAudio::NoError)
                fail(QCoreApplication::translate("Synth", "Sound stopped: the audio output failed."));
        });
        m_device = std::make_unique<TuneDevice>(m_tune);
        m_device->open(QIODevice::ReadOnly);
        m_sink->start(m_device.get());
    } else {
        m_sink->resume();
    }
    m_running = true;
    emit changed();
}
