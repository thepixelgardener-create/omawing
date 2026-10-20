// Theme watching follows Omacut (MIT, David Heinemeier Hansson).
#include "theme.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <cmath>

Theme::Theme(const QString &directory, QObject *parent, const QString &hyprctl)
    : QObject(parent),
      m_directory(directory.isEmpty() ? QDir::homePath() + "/.local/state/omarchy/current" : directory) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);

    // Corners follow Hyprland's effective rounding, which themes and personal
    // overrides can both set, so ask hyprctl rather than reading its config.
    // Without Hyprland the corners stay sharp, like Omarchy's default.
    m_hyprctl = !hyprctl.isEmpty() ? hyprctl
              : directory.isEmpty() && !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")
                  ? QStandardPaths::findExecutable("hyprctl") : QString();
    m_roundingTimeout.setSingleShot(true);
    m_roundingTimeout.setInterval(500);
    connect(&m_roundingTimeout, &QTimer::timeout, &m_roundingProcess, &QProcess::kill);
    connect(&m_roundingProcess, &QProcess::errorOccurred, this, [this] { m_roundingTimeout.stop(); });
    connect(&m_roundingProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_roundingTimeout.stop();
        const auto value = QJsonDocument::fromJson(m_roundingProcess.readAllStandardOutput()).object().value("int");
        if (code != 0 || status != QProcess::NormalExit || !value.isDouble() || value.toInt(-1) < 0)
            return;
        if (value.toInt() != m_radius) {
            m_radius = value.toInt();
            emit changed();
        }
    });
    m_roundingPoll.setInterval(1000);
    connect(&m_roundingPoll, &QTimer::timeout, this, &Theme::refreshRounding);
    if (!m_hyprctl.isEmpty()) {
        // Polling is the fallback, for when the event socket isn't there.
        m_roundingPoll.start();
        if (directory.isEmpty())
            listenToHyprland();
    }
    reload();
}

void Theme::listenToHyprland() {
    const QString path = qEnvironmentVariable("XDG_RUNTIME_DIR") + "/hypr/" +
                         qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE") + "/.socket2.sock";
    connect(&m_events, &QLocalSocket::connected, this, [this] { m_roundingPoll.stop(); });
    connect(&m_events, &QLocalSocket::disconnected, this, [this] { m_roundingPoll.start(); });
    connect(&m_events, &QLocalSocket::errorOccurred, this, [this] { m_roundingPoll.start(); });
    connect(&m_events, &QLocalSocket::readyRead, this, [this] {
        m_eventBuffer += m_events.readAll();
        bool reloaded = false;
        for (qsizetype end; (end = m_eventBuffer.indexOf('\n')) >= 0; m_eventBuffer.remove(0, end + 1))
            reloaded |= m_eventBuffer.startsWith("configreloaded>>");
        // A line never ends only if something is wrong; don't let it grow.
        if (m_eventBuffer.size() > 64 * 1024)
            m_eventBuffer.clear();
        if (reloaded)
            refreshRounding();
    });
    m_events.connectToServer(path);
}

Theme::~Theme() {
    // Closing the socket on the way out mustn't restart the poll.
    disconnect(&m_events, nullptr, this, nullptr);
    if (m_roundingProcess.state() != QProcess::NotRunning) {
        m_roundingProcess.kill();
        m_roundingProcess.waitForFinished(1000);
    }
}

void Theme::refreshRounding() {
    if (m_hyprctl.isEmpty() || m_roundingProcess.state() != QProcess::NotRunning)
        return;
    m_roundingProcess.start(m_hyprctl, {"-j", "getoption", "decoration:rounding"});
    m_roundingTimeout.start();
}

QHash<QString, QString> Theme::readColors(const QString &path) {
    QHash<QString, QString> colors;
    QFile file(path);
    // A theme file is a few hundred bytes; anything far bigger isn't one.
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024)
        return colors;
    static const QRegularExpression expression(R"re(^\s*([a-zA-Z_]+)\s*=\s*["'](#[0-9a-fA-F]{6})["'])re",
                                               QRegularExpression::MultilineOption);
    auto matches = expression.globalMatch(QString::fromUtf8(file.readAll()));
    while (matches.hasNext()) {
        const auto match = matches.next();
        colors.insert(match.captured(1), match.captured(2));
    }
    return colors;
}

// Black or white, whichever reads on the accent (WCAG relative luminance).
QString Theme::accentForeground() const {
    const QColor c(m_accent);
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    const double luminance = .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
    return luminance > .179 ? "black" : "white";
}

void Theme::reload() {
    refreshRounding();
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);

    // A theme switch replaces symlinks and files, so watch the parents too and
    // re-arm after every change. Watching the nearest existing ancestor means
    // installing Omarchy later works without restarting the app.
    QString ancestor = m_directory;
    while (!QFileInfo::exists(ancestor) && ancestor != "/")
        ancestor = QFileInfo(ancestor).absolutePath();
    QStringList candidates{ancestor, QFileInfo(ancestor).absolutePath(), m_directory,
                           m_directory + "/theme", m_directory + "/theme/colors.toml", m_directory + "/theme.name"};
    candidates.removeDuplicates();
    for (const auto &path : candidates)
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);

    const auto colors = readColors(m_directory + "/theme/colors.toml");
    const QString accent = colors.value("accent", "#FFD60A");
    // The name is a scene title, so only its first line, and not too long.
    QFile label(m_directory + "/theme.name");
    QString name;
    if (label.open(QIODevice::ReadOnly))
        name = QString::fromUtf8(label.readLine(256)).trimmed().left(32);
    if (name.isEmpty())
        name = QFileInfo(QFileInfo(m_directory + "/theme").symLinkTarget()).fileName();
    if (accent != m_accent || colors != m_colors || name != m_name) {
        m_accent = accent;
        m_colors = colors;
        m_name = name;
        emit changed();
    }
}
