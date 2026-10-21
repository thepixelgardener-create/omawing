#include "audio.h"

#include <QAudioBuffer>
#include <QAudioBufferOutput>
#include <QAudioDevice>
#include <QAudioOutput>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QSoundEffect>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
const std::array<const char *, Audio::TrackCount> tracks = {
    "Pyramids_Of_Glass", "Across_the_Vector_Plane", "Velvet_Horizon"
};
const std::array<const char *, Audio::EffectCount> effects = {"boost", "ring1", "impact", "wreck"};
}

void BeatTracker::reset() {
    m_cues.clear();
    m_low = m_average = m_hopLow = m_hopAll = 0;
    m_hopFrames = 0;
    m_hopStart = -1;
    m_lastBeat = -1000000000;
    m_level = 0;
}

// The bass is the signal through a one-pole low-pass at 120 Hz. Every hop,
// its energy is compared with the recent average: a sharp rise, well above
// it, is a beat, with at least 280 ms between beats.
void BeatTracker::feed(const float *mono, int frames, int sampleRate, qint64 startMicroseconds) {
    if (sampleRate <= 0)
        return;
    const double a = 1 - std::exp(-2 * M_PI * 120 / sampleRate);
    for (int i = 0; i < frames; ++i) {
        if (m_hopFrames == 0)
            m_hopStart = startMicroseconds + qint64(i) * 1000000 / sampleRate;
        const double x = mono[i];
        m_low += a * (x - m_low);
        m_hopLow += m_low * m_low;
        m_hopAll += x * x;
        if (++m_hopFrames < Hop)
            continue;
        const double low = m_hopLow / Hop, level = std::sqrt(m_hopAll / Hop);
        const bool beat = low > 1e-4 && low > m_average * 1.5 && m_hopStart - m_lastBeat > 280000;
        if (beat)
            m_lastBeat = m_hopStart;
        m_average += (low - m_average) * 0.04;
        m_cues.push_back({m_hopStart, float(std::min(1.0, level * 3)), beat});
        m_hopFrames = 0;
        m_hopLow = m_hopAll = 0;
    }
    // Decoding can run far ahead; a whole track is a few thousand cues.
    while (m_cues.size() > 20000)
        m_cues.pop_front();
}

BeatTracker::Pulse BeatTracker::take(qint64 positionMicroseconds) {
    Pulse pulse;
    while (!m_cues.empty() && m_cues.front().at <= positionMicroseconds) {
        m_level = m_cues.front().level;
        pulse.beat |= m_cues.front().beat;
        m_cues.pop_front();
    }
    pulse.level = m_level;
    return pulse;
}

Audio::Audio(QObject *parent) : QObject(parent) {}
Audio::~Audio() = default;

// The soundtrack is read from files rather than compiled in: Qt's FFmpeg
// backend copies a compiled-in track to a temporary file to play it, and
// leaves the 4 MB copy behind. Installed, the tracks are in
// /usr/share/omawing/music; in a build tree, in src/audio.
QUrl Audio::trackSource(int track) {
    const QString file = QString::fromLatin1(tracks.at(track)) + ".mp3";
    const QString app = QCoreApplication::applicationDirPath();
    for (const QString &directory : {qEnvironmentVariable("OMAWING_MUSIC_DIR"), app + "/../share/omawing/music",
                                     app + "/../src/audio"}) {
        if (directory.isEmpty())
            continue;
        const QFileInfo info(QDir(directory).filePath(file));
        if (info.isFile())
            return QUrl::fromLocalFile(info.canonicalFilePath());
    }
    return {};
}

QUrl Audio::ringSource(int chain) {
    return QUrl(QString("qrc:/audio/ring%1.wav").arg(std::clamp(chain, 1, RingSteps)));
}

QUrl Audio::effectSource(Effect effect) {
    if (effect == Ring)
        return ringSource(1);
    return QUrl("qrc:/audio/" + QString::fromLatin1(effects.at(effect)) + ".wav");
}

QString Audio::trackTitle() const {
    static const std::array<const char *, TrackCount> titles = {"Pyramids Of Glass", "Across the Vector Plane", "Velvet Horizon"};
    return QString::fromLatin1(titles[m_track]);
}

void Audio::stopEffects() {
    for (auto &effect : m_effects) if (effect) effect->stop();
    for (auto &chime : m_chimes) if (chime) chime->stop();
}

void Audio::fail(const QString &message) {
    m_running = false;
    m_error = message;
    if (m_player) m_player->pause();
    stopEffects();
    emit changed();
}

// The soundtrack and the effects open the first time each is switched on:
// effects alone never load the 4 MB soundtrack.
void Audio::openPlayer() {
    m_output = std::make_unique<QAudioOutput>(this);
    m_output->setVolume(m_musicVolume);
    m_player = std::make_unique<QMediaPlayer>(this);
    m_player->setAudioOutput(m_output.get());
    connect(m_player.get(), &QMediaPlayer::errorOccurred, this, [this] {
        fail(tr("Could not play the soundtrack.") + "\n" + m_player->errorString());
    });
    connect(m_player.get(), &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia && m_running && m_music)
            nextTrack();
    });
    // A copy of what plays, as mono floats, for the beat tracker.
    QAudioFormat format;
    format.setSampleFormat(QAudioFormat::Float);
    format.setChannelCount(1);
    format.setSampleRate(44100);
    m_tap = std::make_unique<QAudioBufferOutput>(format, this);
    connect(m_tap.get(), &QAudioBufferOutput::audioBufferReceived, this, &Audio::listen);
    m_player->setAudioBufferOutput(m_tap.get());
    selectTrack();
}

void Audio::openEffects() {
    if (m_effects[0])
        return;
    for (int i = 0; i < EffectCount; ++i) {
        m_effects[i] = std::make_unique<QSoundEffect>(this);
        auto *effect = m_effects[i].get();
        effect->setVolume(m_effectVolume * (i == Boost ? 0.38 : i == Ring ? 0.55 : 0.65));
        connect(effect, &QSoundEffect::statusChanged, this, [this, effect] {
            if (effect->status() == QSoundEffect::Error)
                fail(tr("The audio output can't play this sound."));
        });
        effect->setSource(effectSource(Effect(i)));
    }
    for (int i = 0; i < RingSteps - 1; ++i) {
        m_chimes[i] = std::make_unique<QSoundEffect>(this);
        m_chimes[i]->setVolume(m_effectVolume * 0.55);
        m_chimes[i]->setSource(ringSource(i + 2));
    }
}

// Mixes any format down to mono floats, in case the tap couldn't convert.
void Audio::listen(const QAudioBuffer &buffer) {
    const QAudioFormat format = buffer.format();
    const int channels = std::max(1, format.channelCount()), frames = int(buffer.frameCount());
    std::vector<float> mono(frames);
    for (int i = 0; i < frames; ++i) {
        float sum = 0;
        for (int c = 0; c < channels; ++c) {
            const int at = i * channels + c;
            switch (format.sampleFormat()) {
            case QAudioFormat::Float: sum += buffer.constData<float>()[at]; break;
            case QAudioFormat::Int16: sum += buffer.constData<qint16>()[at] / 32768.0f; break;
            case QAudioFormat::Int32: sum += buffer.constData<qint32>()[at] / 2147483648.0f; break;
            case QAudioFormat::UInt8: sum += (buffer.constData<quint8>()[at] - 128) / 128.0f; break;
            default: return;
            }
        }
        mono[i] = sum / channels;
    }
    m_beats.feed(mono.data(), frames, format.sampleRate(), buffer.startTime());
}

BeatTracker::Pulse Audio::pulse() {
    if (!m_player || !m_running || !m_music || m_player->playbackState() != QMediaPlayer::PlayingState)
        return {};
    return m_beats.take(m_player->position() * 1000);
}

void Audio::playRing(int chain) {
    if (chain <= 1)
        return play(Ring);
    QSoundEffect *chime = m_chimes[std::min(chain, RingSteps) - 2].get();
    if (!m_running || !m_effectsOn || !chime || chime->status() != QSoundEffect::Ready)
        return;
    chime->stop();
    chime->play();
}

void Audio::selectTrack() {
    if (!m_player) return;
    const QUrl source = trackSource(m_track);
    if (source.isEmpty())
        return fail(tr("Could not play the soundtrack.") + "\n" + tr("The music files are missing."));
    m_beats.reset();
    m_player->setSource(source);
    if (m_running && m_music) m_player->play();
}

void Audio::setRunning(bool music, bool effects) {
    if (music == m_music && effects == m_effectsOn && m_running == (music || effects) && m_error.isEmpty())
        return;
    m_music = music;
    m_effectsOn = effects;
    if (!effects)
        stopEffects();
    // Pausing keeps the track's position for later.
    if (!music && m_player)
        m_player->pause();
    if (!music && !effects) {
        m_running = false;
        emit changed();
        return;
    }
    // After a failure, start again from scratch: the device or a file may
    // have been repaired since.
    if (!m_error.isEmpty()) {
        m_player.reset();
        for (auto &effect : m_effects) effect.reset();
        for (auto &chime : m_chimes) chime.reset();
        m_output.reset();
        m_error.clear();
    }
    if (QMediaDevices::defaultAudioOutput().isNull())
        return fail(tr("No audio output device was found."));
    m_running = true;
    if (effects)
        openEffects();
    if (music) {
        if (!m_player) openPlayer();
        else if (m_player->error() != QMediaPlayer::NoError) selectTrack();
        else if (m_player->mediaStatus() == QMediaPlayer::EndOfMedia) nextTrack();
        else m_player->play();
    }
    emit changed();
}

void Audio::nextTrack() {
    m_track = (m_track + 1) % TrackCount;
    selectTrack();
    emit changed();
}

void Audio::play(Effect effect) {
    if (!m_running || !m_effectsOn || !m_effects[effect] || m_effects[effect]->status() != QSoundEffect::Ready) return;
    // Retrigger rather than stacking copies of the same impact without limit.
    m_effects[effect]->stop();
    m_effects[effect]->play();
}

void Audio::setVolume(float music, float effects) {
    m_musicVolume = std::clamp(music, 0.0f, 1.0f);
    m_effectVolume = std::clamp(effects, 0.0f, 1.0f);
    if (m_output) m_output->setVolume(m_musicVolume);
    for (int i = 0; i < EffectCount; ++i)
        if (m_effects[i]) m_effects[i]->setVolume(m_effectVolume * (i == Boost ? 0.38 : i == Ring ? 0.55 : 0.65));
    for (auto &chime : m_chimes)
        if (chime) chime->setVolume(m_effectVolume * 0.55);
}
