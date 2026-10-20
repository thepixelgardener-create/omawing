#include "audio.h"

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
const std::array<const char *, Audio::TrackCount> tracks = {
    "Pyramids_Of_Glass", "Across_the_Vector_Plane", "Velvet_Horizon"
};
const std::array<const char *, Audio::EffectCount> effects = {"boost", "ring", "impact", "wreck"};
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

QUrl Audio::effectSource(Effect effect) {
    return QUrl("qrc:/audio/" + QString::fromLatin1(effects.at(effect)) + ".wav");
}

QString Audio::trackTitle() const {
    static const std::array<const char *, TrackCount> titles = {"Pyramids Of Glass", "Across the Vector Plane", "Velvet Horizon"};
    return QString::fromLatin1(titles[m_track]);
}

void Audio::fail(const QString &message) {
    m_running = false;
    m_error = message;
    if (m_player) m_player->pause();
    for (auto &effect : m_effects) if (effect) effect->stop();
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
}

void Audio::selectTrack() {
    if (!m_player) return;
    const QUrl source = trackSource(m_track);
    if (source.isEmpty())
        return fail(tr("Could not play the soundtrack.") + "\n" + tr("The music files are missing."));
    m_player->setSource(source);
    if (m_running && m_music) m_player->play();
}

void Audio::setRunning(bool music, bool effects) {
    if (music == m_music && effects == m_effectsOn && m_running == (music || effects) && m_error.isEmpty())
        return;
    m_music = music;
    m_effectsOn = effects;
    if (!effects)
        for (auto &effect : m_effects) if (effect) effect->stop();
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
}
