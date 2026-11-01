#include "scoreboard.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QSettings>
#include <algorithm>

ScoreBoard::ScoreBoard(const QString &settingsFile, QObject *parent)
    : QAbstractListModel(parent), m_settingsFile(settingsFile) {
    load();
}

ScoreBoard::~ScoreBoard() {
    if (m_pending.score > 0)
        save({});
}

void ScoreBoard::load() {
    QSettings settings(m_settingsFile, QSettings::IniFormat);
    m_playerName = settings.value("name", qEnvironmentVariable("USER")).toString().left(NameLength);
    m_entries = read(settings);
}

QList<ScoreBoard::Entry> ScoreBoard::read(QSettings &settings) {
    QList<Entry> entries;
    const int count = settings.beginReadArray("scores");
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        // An edited file can't stretch the board: names are cut to what the
        // name field allows, and empty runs are dropped.
        const Entry entry{settings.value("name").toString().left(NameLength), settings.value("score").toInt(),
                          settings.value("rings").toInt(), settings.value("date").toString()};
        if (entry.score > 0)
            entries.append(entry);
    }
    settings.endArray();
    std::stable_sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.score > b.score; });
    if (entries.size() > Size)
        entries.resize(Size);
    return entries;
}

bool ScoreBoard::qualifies(int score) const {
    return score > 0 && (m_entries.size() < Size || score > m_entries.last().score);
}

void ScoreBoard::offer(int score, int rings) {
    // An older score still waiting goes on the board first, so the new one
    // is measured against the board as it now stands.
    if (m_pending.score > 0 && !save({})) {
        // It couldn't be saved. Only one score can wait for a name, so the
        // better of the two stays pending and is tried again later.
        if (score > m_pending.score) {
            m_pending = {{}, score, rings, {}};
            emit changed();
        }
        return;
    }
    if (!qualifies(score))
        return;
    m_pending = {{}, score, rings, {}};
    emit changed();
    emit pending();
}

bool ScoreBoard::save(const QString &name) {
    if (m_pending.score <= 0)
        return true;
    const QString trimmed = name.trimmed().left(NameLength);
    if (!trimmed.isEmpty())
        m_playerName = trimmed;
    Entry entry = m_pending;
    entry.name = m_playerName;
    entry.date = QDateTime::currentDateTime().toString(Qt::ISODate);

    // Another copy of the game may have saved a score since this one read
    // the board: the file is read again and merged, under a lock, so neither
    // erases the other's runs. If the lock can't be had, the save goes ahead.
    // (QSettings takes "<file>.lock" itself while it writes, so this one has
    // a name of its own.)
    QDir().mkpath(QFileInfo(m_settingsFile).absolutePath());
    QLockFile lock(m_settingsFile + ".scores.lock");
    lock.tryLock(1000);
    QSettings settings(m_settingsFile, QSettings::IniFormat);
    auto entries = settings.status() == QSettings::NoError ? read(settings) : m_entries;

    // Ties keep the earlier score ahead.
    entries.insert(std::find_if(entries.begin(), entries.end(), [&](const Entry &e) { return e.score < entry.score; }),
                   entry);
    if (entries.size() > Size)
        entries.resize(Size);
    if (!write(settings, entries)) {
        emit changed();
        return false;
    }
    beginResetModel();
    m_entries = entries;
    endResetModel();
    m_pending = {};
    emit changed();
    return true;
}

bool ScoreBoard::write(QSettings &settings, const QList<Entry> &entries) {
    settings.setValue("name", m_playerName);
    settings.remove("scores");
    settings.beginWriteArray("scores", int(entries.size()));
    for (int i = 0; i < entries.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("name", entries[i].name);
        settings.setValue("score", entries[i].score);
        settings.setValue("rings", entries[i].rings);
        settings.setValue("date", entries[i].date);
    }
    settings.endArray();
    settings.sync();
    m_error = settings.status() == QSettings::NoError ? QString() : tr("Could not save settings. Check folder permissions.");
    return m_error.isEmpty();
}

int ScoreBoard::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : count(); }

QVariant ScoreBoard::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const Entry &entry = m_entries[index.row()];
    switch (role) {
    case NameRole: return entry.name;
    case ScoreRole: return entry.score;
    case RingsRole: return entry.rings;
    case DateRole: return entry.date;
    default: return {};
    }
}

QHash<int, QByteArray> ScoreBoard::roleNames() const {
    return {{NameRole, "name"}, {ScoreRole, "score"}, {RingsRole, "rings"}, {DateRole, "date"}};
}
