#include "synth.h"

#include <QAudioDevice>
#include <QAudioOutput>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QSoundEffect>
#include <algorithm>

namespace {
const std::array<const char *, Synth::TrackCount> tracks = {
    "Pyramids_Of_Glass", "Across_the_Vector_Plane", "Velvet_Horizon"
};
const std::array<const char *, Synth::EffectCount> effects = {"boost", "ring", "impact", "wreck"};
}

Synth::Synth(QObject *parent) : QObject(parent) {}
Synth::~Synth() = default;

// The soundtrack is read from files rather than compiled in: Qt's FFmpeg
// backend copies a compiled-in track to a temporary file to play it, and
// leaves the 4 MB copy behind. Installed, the tracks are in
// /usr/share/omawing/music; in a build tree, in src/audio.
QUrl Synth::trackSource(int track) {
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

QUrl Synth::effectSource(Effect effect) {
    return QUrl("qrc:/audio/" + QString::fromLatin1(effects.at(effect)) + ".wav");
}

QString Synth::trackTitle() const {
    static const std::array<const char *, TrackCount> titles = {"Pyramids Of Glass", "Across the Vector Plane", "Velvet Horizon"};
    return QString::fromLatin1(titles[m_track]);
}

void Synth::fail(const QString &message) {
    m_running = false;
    m_error = message;
    if (m_player) m_player->pause();
    for (auto &effect : m_effects) if (effect) effect->stop();
    emit changed();
}

void Synth::openAudio() {
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
    selectTrack();
}

void Synth::selectTrack() {
    if (!m_player) return;
    const QUrl source = trackSource(m_track);
    if (source.isEmpty())
        return fail(tr("Could not play the soundtrack.") + "\n" + tr("The music files are missing."));
    m_player->setSource(source);
    if (m_running && m_music) m_player->play();
}

void Synth::setRunning(bool music, bool effects) {
    const bool running = music || effects;
    if (running == m_running && music == m_music && effects == m_effectsOn && m_error.isEmpty()) return;
    m_music = music;
    m_effectsOn = effects;
    if (!effects)
        for (auto &effect : m_effects) if (effect) effect->stop();
    if (running && m_running && m_error.isEmpty()) {
        // Already open: only the music needs starting or pausing.
        if (!m_player) {
        } else if (!music) m_player->pause();
        else if (m_player->mediaStatus() == QMediaPlayer::EndOfMedia) nextTrack();
        else m_player->play();
        emit changed();
        return;
    }
    if (!running) {
        m_running = false;
        if (m_player) m_player->pause();
        for (auto &effect : m_effects) if (effect) effect->stop();
        emit changed();
        return;
    }
    if (!m_error.isEmpty()) {
        m_player.reset();
        for (auto &effect : m_effects) effect.reset();
        m_output.reset();
    }
    m_error.clear();
    if (QMediaDevices::defaultAudioOutput().isNull())
        return fail(tr("No audio output device was found."));
    m_running = true;
    if (!m_player) openAudio();
    // A failed source can be retried after the device or file is repaired.
    else if (m_player->error() != QMediaPlayer::NoError) selectTrack();
    else if (!m_music) m_player->pause();
    else if (m_player->mediaStatus() == QMediaPlayer::EndOfMedia) nextTrack();
    else m_player->play();
    emit changed();
}

void Synth::nextTrack() {
    m_track = (m_track + 1) % TrackCount;
    selectTrack();
    emit changed();
}

void Synth::play(Effect effect) {
    if (!m_running || !m_effectsOn || !m_effects[effect] || m_effects[effect]->status() != QSoundEffect::Ready) return;
    // Retrigger rather than stacking copies of the same impact without limit.
    m_effects[effect]->stop();
    m_effects[effect]->play();
}

void Synth::setVolume(float music, float effects) {
    m_musicVolume = std::clamp(music, 0.0f, 1.0f);
    m_effectVolume = std::clamp(effects, 0.0f, 1.0f);
    if (m_output) m_output->setVolume(m_musicVolume);
    for (int i = 0; i < EffectCount; ++i)
        if (m_effects[i]) m_effects[i]->setVolume(m_effectVolume * (i == Boost ? 0.38 : i == Ring ? 0.55 : 0.65));
}
