#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

// The top ten, best first, saved in the app's settings file. A finished run
// that makes the board waits as the pending score until it gets a name. As a
// list model, QML shows it with roles name, score, rings, and date.
class ScoreBoard : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int pendingScore READ pendingScore NOTIFY changed)
    Q_PROPERTY(QString playerName READ playerName NOTIFY changed)
    Q_PROPERTY(int nameLength READ nameLength CONSTANT)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    static constexpr int Size = 10, NameLength = 12;

    struct Entry {
        QString name;
        int score = 0, rings = 0;
        QString date;   // ISO 8601
    };
    enum Role { NameRole = Qt::UserRole + 1, ScoreRole, RingsRole, DateRole };

    explicit ScoreBoard(const QString &settingsFile, QObject *parent = nullptr);
    // Quitting with a score still waiting saves it under the last name.
    ~ScoreBoard() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    const QList<Entry> &entries() const { return m_entries; }
    int count() const { return int(m_entries.size()); }
    int best() const { return m_entries.isEmpty() ? 0 : m_entries.first().score; }
    int pendingScore() const { return m_pending.score; }
    QString playerName() const { return m_playerName; }
    int nameLength() const { return NameLength; }
    // Why the last save failed, or empty.
    QString error() const { return m_error; }

    bool qualifies(int score) const;
    // A finished run: held for a name if it made the board. An older score
    // still waiting is saved first, under the last name.
    void submit(int score, int rings);
    // Puts the pending score on the board; an empty name uses the last one.
    // On a failed save the score stays pending, so it can be tried again.
    Q_INVOKABLE void save(const QString &name);

signals:
    void changed();
    // A run made the board and is waiting for a name.
    void pending();

private:
    void load();
    bool write(const QList<Entry> &entries);

    QString m_settingsFile, m_playerName, m_error;
    QList<Entry> m_entries;
    Entry m_pending;
};
