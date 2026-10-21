#include <QDir>
#include <QAudioDecoder>
#include <QAudioBuffer>
#include <QAudioDevice>
#include <QAudioOutput>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QSoundEffect>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QSettings>
#include <QtTest>
#include <cmath>
#include <limits>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include "flightview.h"

#include "backend.h"
#include "flight.h"
#include "scene.h"
#include "audio.h"
#include "theme.h"
#include "translator.h"

class OmawingTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        qputenv("OMAWING_MUSIC_DIR", OMAWING_TEST_MUSIC);
        QQuickStyle::setStyle("Material");
        qmlRegisterType<FlightView>("Omawing", 1, 0, "FlightView");
    }
    void cleanup() {
        QLocale::setDefault(QLocale::c());
        QGuiApplication::setLayoutDirection(Qt::LeftToRight);
    }
    void demoPlaysAllNineActsAndLoops()
    {
        Flight flight;
        QSet<int> acts;
        // One second beyond the complete nine-act sequence.
        for (int i = 0; i < int((Flight::Acts * Flight::ActLength + 1) * 2); ++i) {
            flight.advance(0.5);
            acts.insert(flight.act());
            QVERIFY(std::isfinite(flight.ship.x) && std::isfinite(flight.camera.yaw));
            QVERIFY(flight.ship.y >= Flight::MinAltitude && flight.ship.y <= Flight::MaxAltitude);
        }
        QCOMPARE(acts.size(), Flight::Acts);
        QCOMPARE(flight.act(), 0);
    }

    void everyFrameStaysInsideTheView()
    {
        // Paints a frame every half second of the whole sequence, at three
        // window shapes, and checks each projected point.
        for (const QSize size : {QSize(960, 540), QSize(700, 900), QSize(1600, 600)}) {
            Flight flight;
            Scene scene;
            QImage image(size, QImage::Format_ARGB32_Premultiplied);
            for (int i = 0; i < int((Flight::Acts * Flight::ActLength + 1) * 2); ++i) {
                flight.advance(0.5);
                QPainter painter(&image);
                scene.paint(painter, flight, size, {});
                painter.end();
                int shipFaces = 0;
                for (const auto &face : scene.faces()) {
                    shipFaces += face.ship;
                    // The renderer's fast path requires convex projected faces.
                    double winding = 0;
                    for (int v = 0; v < face.polygon.size(); ++v) {
                        const QPointF a = face.polygon[(v+1)%face.polygon.size()] - face.polygon[v];
                        const QPointF b = face.polygon[(v+2)%face.polygon.size()] - face.polygon[(v+1)%face.polygon.size()];
                        const double cross = a.x()*b.y() - a.y()*b.x();
                        if (std::abs(cross) < 1e-5) continue;
                        QVERIFY(winding == 0 || winding * cross >= 0);
                        winding = cross;
                    }
                    for (const QPointF &p : face.polygon) {
                        QVERIFY(std::isfinite(p.x()) && std::isfinite(p.y()));
                        QVERIFY(p.x() >= -0.01 && p.x() <= scene.width() + 0.01);
                        QVERIFY(p.y() >= -0.01 && p.y() <= scene.height() + 0.01);
                    }
                }
                // The chase camera never loses the ship.
                QVERIFY2(shipFaces >= 4, qPrintable(QString("only %1 ship faces at %2 s").arg(shipFaces).arg(flight.time)));
            }
        }
    }

    void steeringTakesOverAndBanks()
    {
        QTemporaryDir settings;
        Backend backend(nullptr, settings.filePath("game.ini"));
        QVERIFY(backend.demo());
        backend.flight().ship.y = 14;
        QVERIFY(backend.keyDown(Qt::Key_D));
        QVERIFY(!backend.demo());
        backend.flight().advance(3);
        QVERIFY(backend.flight().ship.yaw > 1);
        QVERIFY(backend.flight().ship.bank < -0.2);
        backend.flight().advance(6);
        QVERIFY(backend.flight().ship.yaw > 2 * M_PI);
        QVERIFY(backend.keyUp(Qt::Key_D));
        QVERIFY(!backend.keyDown(Qt::Key_Q));
    }

    void altitudeStaysBetweenGroundAndCeiling()
    {
        QTemporaryDir settings;
        Backend backend(nullptr, settings.filePath("game.ini"));
        // Test the altitude limiter independently of ring impacts.
        backend.flight().game.hurt = 1000;
        backend.keyDown(Qt::Key_W);
        backend.flight().advance(15);
        QCOMPARE(backend.flight().ship.y, Flight::MaxAltitude);
        backend.keyUp(Qt::Key_W);
        backend.keyDown(Qt::Key_Down);
        backend.flight().advance(15);
        QCOMPARE(backend.flight().ship.y, Flight::MinAltitude);
    }

    void shiftBoosts()
    {
        QTemporaryDir settings;
        Backend backend(nullptr, settings.filePath("game.ini"));
        backend.keyDown(Qt::Key_Shift);
        backend.flight().advance(2);
        QVERIFY(backend.flight().ship.speed > 25);
    }

    void dragSteersLikeAStick()
    {
        QTemporaryDir settings;
        Backend backend(nullptr, settings.filePath("game.ini"));
        backend.stickStart();
        QVERIFY(!backend.demo());
        backend.stickMove(1, -1);
        backend.flight().advance(2);
        QVERIFY(backend.flight().ship.yaw > 1);
        QVERIFY(backend.flight().ship.y > 3.2);
        backend.stickEnd();
        const double yaw = backend.flight().ship.yaw;
        backend.flight().advance(1);
        QCOMPARE(backend.flight().ship.yaw, yaw);
    }

    void losingFocusReleasesKeys()
    {
        QTemporaryDir settings;
        Backend backend(nullptr, settings.filePath("game.ini"));
        backend.keyDown(Qt::Key_A);
        backend.flight().advance(0.1);
        backend.clearInput();
        const double yaw = backend.flight().ship.yaw;
        backend.flight().advance(1);
        QCOMPARE(backend.flight().ship.yaw, yaw);
    }

    void pauseFreezesTheFlight()
    {
        QTemporaryDir settings;
        Backend backend(nullptr, settings.filePath("game.ini"));
        backend.tick(0.02);
        backend.setPaused(true);
        const double time = backend.flight().time;
        QSignalSpy frames(&backend, &Backend::frameChanged);
        backend.tick(0.02);
        QCOMPARE(backend.flight().time, time);
        QCOMPARE(frames.count(), 0);
        // Keys don't take over from the autopilot while paused.
        QVERIFY(backend.keyDown(Qt::Key_D));
        QVERIFY(backend.demo());
        backend.setPaused(false);
        backend.tick(0.02);
        QVERIFY(backend.flight().time > time);
    }

    void nextSceneAndRestart()
    {
        QTemporaryDir settings;
        Backend backend(nullptr, settings.filePath("game.ini"));
        backend.nextScene();
        QCOMPARE(backend.flight().act(), 1);
        QCOMPARE(backend.flight().time, Flight::ActLength + 2);
        backend.flight().scene(2);
        QCOMPARE(backend.flight().act(), 2);
        backend.setDemo(false);
        backend.restart();
        QCOMPARE(backend.flight().time, 0.0);
        QCOMPARE(backend.flight().ship.x, 0.0);
        QVERIFY(backend.demo());
    }

    void musicAndEffectsToggleSeparately()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("omawing.conf");
        {
            Backend backend(nullptr, file);
            QVERIFY(!backend.music() && !backend.effects());
            backend.toggleEffects();
            QVERIFY(backend.effects() || !backend.soundError().isEmpty());
            QVERIFY(!backend.music());
            // Effects alone never open the soundtrack.
            QVERIFY(!backend.audio().findChild<QMediaPlayer *>());
            backend.toggleEffects();
            backend.toggleMusic();
            QVERIFY(!backend.effects());
            // Let the track finish opening: closing the player halfway makes
            // FFmpeg report the file as broken.
            if (const auto player = backend.audio().findChild<QMediaPlayer *>())
                QTRY_VERIFY_WITH_TIMEOUT(player->mediaStatus() != QMediaPlayer::LoadingMedia, 10000);
        }
        // The choice is remembered for next time.
        QSettings settings(file, QSettings::IniFormat);
        QCOMPARE(settings.value("effects").toBool(), false);
    }

    void volumeIsClampedAndRemembered()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("omawing.conf");
        {
            Backend backend(nullptr, file);
            QCOMPARE(backend.musicVolume(), 0.8);
            backend.setMusicVolume(0.3);
            backend.setEffectsVolume(4);
            QCOMPARE(backend.effectsVolume(), 1.0);
        }
        Backend again(nullptr, file);
        QCOMPARE(again.musicVolume(), 0.3);
        QCOMPARE(again.effectsVolume(), 1.0);
    }

    void longStepsKeepEveryEvent()
    {
        Flight flight;
        flight.demo = false;
        flight.press(Flight::Boost);
        flight.step(0.2);   // Split into small steps; boosting starts in the first.
        QVERIFY(flight.events & Flight::BoostStarted);
    }

    void scoreBoardKeepsTiesInOrderAndServesQml()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("board.ini");
        // A hand-edited file: an overlong name and an empty run.
        write(file, "[scores]\n1\\name=AVERYLONGPILOTNAME\n1\\score=500\n2\\name=Zero\n2\\score=0\nsize=2\n");
        ScoreBoard board(file);
        QCOMPARE(board.count(), 1);
        QCOMPARE(board.entries().first().name, QString("AVERYLONGPIL"));
        board.offer(500, 3);
        board.save("Second");
        QCOMPARE(board.entries()[1].name, QString("Second"));   // A tie stays behind the earlier score.
        const QModelIndex top = board.index(0);
        QCOMPARE(board.data(top, ScoreBoard::ScoreRole).toInt(), 500);
        QCOMPARE(board.roleNames().value(ScoreBoard::NameRole), QByteArray("name"));
        board.offer(0, 0);
        QCOMPARE(board.pendingScore(), 0);
    }

    void beatTrackerHearsKicksOnTime()
    {
        // Ten seconds of a kick drum at 120 bpm over a quiet high hum.
        const int rate = 44100, seconds = 10;
        std::vector<float> samples(rate * seconds);
        for (int i = 0; i < int(samples.size()); ++i) {
            const double t = double(i) / rate, sinceKick = std::fmod(t, 0.5);
            samples[i] = float(0.05 * std::sin(2 * M_PI * 2000 * t) +
                               0.8 * std::sin(2 * M_PI * 55 * sinceKick) * std::exp(-sinceKick * 12));
        }
        BeatTracker tracker;
        // Fed in uneven chunks, as decoders deliver them.
        for (int at = 0; at < int(samples.size());) {
            const int frames = std::min(int(samples.size()) - at, 1500 + at % 700);
            tracker.feed(samples.data() + at, frames, rate, qint64(at) * 1000000 / rate);
            at += frames;
        }
        QList<double> beats;
        float loudest = 0;
        for (int ms = 0; ms <= seconds * 1000; ms += 10) {
            const auto pulse = tracker.take(qint64(ms) * 1000);
            loudest = std::max(loudest, pulse.level);
            if (pulse.beat) beats << ms / 1000.0;
        }
        // Once the average has settled, one beat per kick, close to it.
        QVERIFY2(beats.size() >= 17 && beats.size() <= 20, qPrintable(QString::number(beats.size())));
        for (double at : beats) {
            const double offset = std::fmod(at, 0.5);
            QVERIFY2(offset < 0.06 || offset > 0.48, qPrintable(QString::number(at)));
        }
        QVERIFY(loudest > 0.3);
        // Nothing is handed out before its time.
        BeatTracker early;
        early.feed(samples.data(), rate, rate, 0);
        QVERIFY(!early.take(-1).beat);
    }

    void ringsFlashAndBurst()
    {
        Flight flight;
        flight.advance(1);
        auto count = [&](Particle::Kind kind) {
            return std::count_if(flight.particles.begin(), flight.particles.end(), [&](const Particle &p) { return p.kind == kind; });
        };
        flight.demo = false;
        const Ring r = World::ring(0);
        flight.ship.x = r.x; flight.ship.y = r.y; flight.ship.z = r.z - 0.1;
        flight.step(1.0 / 60);
        QCOMPARE(flight.game.ringsFlown, 1);
        QCOMPARE(count(Particle::Flash), 1);
        QVERIFY(count(Particle::Glitter) >= 60);
    }

    void switchingThemeShowsItsScene()
    {
        QTemporaryDir dir;
        Backend backend(nullptr, dir.filePath("omawing.conf"));
        backend.setTheme({{"background", "#1a1b26"}, {"accent", "#7aa2f7"}}, "Tokyo Night");
        // The first reading at startup leaves the sequence where it is.
        QCOMPARE(backend.flight().act(), 0);
        backend.setTheme({{"background", "#1a1b26"}, {"accent", "#7aa2f7"}}, "Tokyo Night");
        QCOMPARE(backend.flight().act(), 0);
        backend.setTheme({{"background", "#05182e"}, {"accent", "#faa968"}}, "Retro 82");
        QCOMPARE(backend.flight().act(), Scene::ThemeAct);
        QCOMPARE(backend.look().theme[6], QColor("#faa968"));
    }

    void hudReportsTheMode()
    {
        QTemporaryDir settings;
        Backend backend(nullptr, settings.filePath("game.ini"));
        QCOMPARE(backend.hud().status, QString("AUTOPILOT / ENJOY THE RIDE"));
        backend.setPaused(true);
        QCOMPARE(backend.hud().status, QString("PAUSED"));
        backend.setPaused(false);
        backend.setDemo(false);
        QCOMPARE(backend.hud().status, QString("YOU HAVE THE CONTROLS"));
        QVERIFY(backend.hud().mode.startsWith("FREE FLIGHT"));
        QVERIFY(backend.hud().altitude.contains("03"));
    }

    void ringCrossingScoresOnceAndBoostDoubles() {
        Flight f;
        f.demo = false;
        const Ring r = World::ring(0);
        f.ship.x = r.x; f.ship.y = r.y; f.ship.z = r.z - 0.1;
        f.step(1.0 / 60);
        QCOMPARE(f.game.score, 100);
        QCOMPARE(f.game.ringsFlown, 1);
        QCOMPARE(f.game.multiplier, 2);
        f.ship.yaw = M_PI;
        f.advance(0.1);
        QCOMPARE(f.game.score, 100);
        const Ring next = World::ring(1);
        f.ship.x = next.x; f.ship.y = next.y; f.ship.z = next.z - 0.5;
        f.ship.yaw = 0; f.ship.speed = Flight::BoostSpeed;
        f.press(Flight::Boost);
        f.step(0.05);
        QCOMPARE(f.game.score, 500);
        QCOMPARE(f.game.multiplier, 3);
        QVERIFY(f.game.popups.size() == 2);
    }

    void missedRingAndRimBreakTheChain() {
        Flight f;
        f.demo = false;
        const Ring r = World::ring(0);
        f.game.multiplier = 4;
        f.ship.x = r.x + 7; f.ship.y = r.y; f.ship.z = r.z - 0.1;
        f.step(1.0 / 60);
        QCOMPARE(f.game.score, 0);
        QCOMPARE(f.game.multiplier, 1);
        QCOMPARE(f.game.hull, Game::FullHull);
        f.ship.x = r.x + World::RingRadius; f.ship.z = r.z - 0.1;
        f.step(1.0 / 60);
        QCOMPARE(f.game.hull, Game::FullHull - 12);
    }

    void collisionsDamageWithInvulnerabilityAndEndTheRun() {
        QTemporaryDir settings;
        Backend backend(nullptr, settings.filePath("game.ini"));
        backend.setDemo(false);
        auto &f = backend.flight();
        const auto obstacles = World::scenery(0, 0, 1, 0);
        const auto o = *std::find_if(obstacles.begin(), obstacles.end(), [](const Obstacle &o) { return o.kind == Obstacle::Pyramid; });
        auto collide = [&] { f.ship.x = o.x; f.ship.y = 2; f.ship.z = o.z; backend.tick(0.016); };
        collide();
        QCOMPARE(f.game.hull, 66.0);
        QVERIFY(f.camera.shake > 0);
        collide();
        QCOMPARE(f.game.hull, 66.0);
        f.game.hurt = 0; collide();
        QCOMPARE(f.game.hull, 32.0);
        f.game.score = 700; f.game.ringsFlown = 3;
        f.game.hurt = 0; collide();
        QCOMPARE(f.game.hull, 0.0);
        QVERIFY(f.game.wrecked >= 0);
        for (int i = 0; i < 55; ++i) backend.tick(0.05);
        QVERIFY(backend.demo());
        QCOMPARE(backend.scoreBoard().pendingScore(), 700);
        QCOMPARE(f.game.hull, Game::FullHull);
    }

    void wingsAndParticlesRespondWithoutInvalidNumbers() {
        Flight f;
        f.demo = false; f.ship.y = 14;
        f.press(Flight::Right); f.press(Flight::Boost);
        f.advance(1);
        QVERIFY(f.ship.flexLeft > 0.1 && f.ship.flexRight < -0.1);
        QVERIFY(f.ship.sweep > 0.4);
        QVERIFY(f.particles.size() > 30 && f.particles.size() <= Flight::MaxParticles);
        f.step(0); f.step(-1); f.step(std::numeric_limits<double>::quiet_NaN());
        for (const auto &p : f.particles)
            for (double v : p.position) QVERIFY(std::isfinite(v));
        Scene scene;
        QImage image(960, 540, QImage::Format_ARGB32_Premultiplied);
        QPainter painter(&image);
        scene.paint(painter, f, image.size(), {});
        int soft = 0;
        for (const auto &face : scene.faces())
            if (face.sprite >= 0) { ++soft; QVERIFY(face.opacity > 0 && face.opacity < 1); }
        QVERIFY(soft > 0);
    }

    void scoresRankPersistAndBannerStaysOff() {
        QTemporaryDir settings;
        const QString path = settings.filePath("game.ini");
        {
            Backend b(nullptr, path);
            b.toggleBanner();
            for (int i = 1; i <= 12; ++i) {
                b.setDemo(false); b.flight().game.score = i * 100; b.flight().game.ringsFlown = i;
                b.setDemo(true); b.scoreBoard().save("  Pilot  ");
            }
            QCOMPARE(b.scoreBoard().count(), ScoreBoard::Size);
            QCOMPARE(b.scoreBoard().best(), 1200);
            QCOMPARE(b.scoreBoard().entries().last().score, 300);
            b.setDemo(false); b.flight().game.score = 2000; // Quitting mid-run saves too.
        }
        Backend loaded(nullptr, path);
        QCOMPARE(loaded.scoreBoard().best(), 2000);
        QCOMPARE(loaded.scoreBoard().playerName(), QString("Pilot"));
        QVERIFY(!loaded.banner() && !loaded.look().banner);
        QCOMPARE(loaded.scoreBoard().count(), ScoreBoard::Size);
    }

    void failedScoreSaveRetainsPendingRun() {
        QTemporaryDir settings;
        const QString blocked = settings.filePath("blocked");
        write(blocked, "file, not a directory");
        Backend b(nullptr, blocked + "/game.ini");
        b.setDemo(false); b.flight().game.score = 100; b.setDemo(true);
        b.scoreBoard().save("Pilot");
        QVERIFY(!b.settingsError().isEmpty());
        QCOMPARE(b.scoreBoard().pendingScore(), 100);
        QVERIFY((b.scoreBoard().count() == 0));
    }

    void hiddenWindowAndInvalidTicksDoNotAdvance() {
        QTemporaryDir settings;
        Backend b(nullptr, settings.filePath("game.ini"));
        b.tick(0); b.tick(-1); b.tick(std::numeric_limits<double>::quiet_NaN());
        QCOMPARE(b.flight().time, 0.0);
        b.setHidden(true); b.tick(1);
        QCOMPARE(b.flight().time, 0.0);
        b.setHidden(false); b.tick(0.02);
        QCOMPARE(b.flight().time, 0.02);
    }

    void themePaletteChangesEvenIfAccentDoesNot() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        const QString path = dir.filePath("theme/colors.toml");
        write(path, "accent = \"#112233\"\nbackground = \"#000011\"\n");
        Theme theme(dir.path());
        QSignalSpy changed(&theme, &Theme::changed);
        write(path, "accent = \"#112233\"\nbackground = \"#221100\"\n");
        QTRY_VERIFY(changed.count() > 0);
        QCOMPARE(theme.colors().value("background"), QString("#221100"));
    }

    void realWindowShortcutsScoreEntryAndResponsiveLayout() {
        QTemporaryDir dir;
        Backend backend(nullptr, dir.filePath("game.ini"));
        Theme theme(dir.path());
        Translator translator(dir.filePath("language"));
        qApp->installTranslator(&translator);
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings << error.toString();
        });
        connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QTest::qWait(100);
        QTest::keyClick(window, Qt::Key_B);
        QVERIFY(!backend.banner());
        QTest::keyClick(window, Qt::Key_P);
        QCOMPARE(backend.trackTitle(), QString("Across the Vector Plane"));
        backend.setDemo(false);
        backend.flight().game.score = 1200;
        backend.flight().game.ringsFlown = 6;
        backend.setDemo(true);
        auto board = window->findChild<QObject *>("scoreBoard");
        QVERIFY(board);
        QTRY_VERIFY(board->property("opened").toBool());
        QVERIFY(backend.paused());
        auto player = window->findChild<QQuickItem *>("playerName");
        QVERIFY(player);
        player->setProperty("text", "");
        player->forceActiveFocus();
        // These would quit, change acts, reset or steer outside a text field.
        for (auto key : {Qt::Key_Q, Qt::Key_N, Qt::Key_R, Qt::Key_W}) QTest::keyClick(window, key);
        QCOMPARE(player->property("text").toString(), QString("qnrw"));
        QVERIFY(backend.demo());
        QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(backend.scoreBoard().pendingScore(), 0);
        QCOMPARE(backend.scoreBoard().best(), 1200);
        auto capture = [&](const QString &name) {
            QTest::qWait(250);
            const QImage shot = window->grabWindow();
            QVERIFY(!shot.isNull());
            const QString out = qEnvironmentVariable("OMAWING_SCREENSHOT_DIR");
            if (!out.isEmpty()) { QDir().mkpath(out); QVERIFY(shot.save(out + "/" + name + ".png")); }
        };
        std::function<QQuickItem *(QQuickItem *)> findRow = [&](QQuickItem *item) -> QQuickItem * {
            if (item->objectName() == "scoreRow") return item;
            for (auto child : item->childItems()) if (auto row = findRow(child)) return row;
            return nullptr;
        };
        QTRY_VERIFY(findRow(window->contentItem()));
        QTRY_VERIFY(findRow(window->contentItem())->width() > 400);
        QTRY_VERIFY(findRow(window->contentItem())->height() >= 44);
        capture("scores");
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!board->property("visible").toBool());
        QVERIFY(!backend.paused());
        backend.setPaused(true);
        backend.flight().advance(5);
        backend.nextScene();
        capture("flight");
        backend.setDemo(false);
        backend.flight().ship.x = 0;
        backend.flight().ship.y = 4;
        backend.flight().ship.yaw = 0;
        backend.flight().game.score = 1350;
        backend.flight().game.hull = 76;
        backend.flight().game.multiplier = 3;
        backend.toggleBanner();
        backend.setPaused(false);
        window->setProperty("pointerAwake", false);
        // The toolbar takes a moment to fade away.
        QTRY_VERIFY(!window->findChild<QObject *>("toolbar")->property("visible").toBool());
        capture("gameplay");
        backend.setPaused(true);
        // V opens the volume panel with the music slider focused; the arrow
        // keys move it and Esc closes the panel.
        auto panel = window->findChild<QObject *>("volumePanel");
        QVERIFY(panel);
        const double music = backend.musicVolume();
        QTest::keyClick(window, Qt::Key_V);
        QTRY_VERIFY(panel->property("opened").toBool());
        QTest::keyClick(window, Qt::Key_Left);
        QVERIFY(backend.musicVolume() < music);
        capture("volume");
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!panel->property("visible").toBool());
        // U switches the toolbar (and the titles) off, even while the pointer is moving, and
        // fades it away; a second U brings it back.
        auto bar = window->findChild<QObject *>("toolbar");
        QVERIFY(bar);
        window->setProperty("pointerAwake", true);
        QTRY_VERIFY(bar->property("shown").toBool());
        QTest::keyClick(window, Qt::Key_U);
        QVERIFY(!backend.interfaceShown());
        QVERIFY(!bar->property("shown").toBool());
        QTRY_VERIFY(!bar->property("visible").toBool());
        // The titles at the top fade out with it, even while paused.
        QTRY_COMPARE(backend.look().titles, 0.0);
        QTest::keyClick(window, Qt::Key_U);
        QVERIFY(backend.interfaceShown());
        QTRY_COMPARE(bar->property("opacity").toDouble(), 1.0);
        QTRY_COMPARE(backend.look().titles, 1.0);
        window->setProperty("pointerAwake", false);
        window->resize(640, 400);
        for (const QString &language : {QString("de"), QString("ar")}) {
            write(dir.filePath("language"), language.toUtf8());
            QTRY_COMPARE(translator.language(), language);
            QMetaObject::invokeMethod(window, "showScores");
            QTRY_VERIFY(board->property("opened").toBool());
            capture("scores-" + language);
            QVERIFY(board->property("width").toDouble() <= window->width());
            QVERIFY(board->property("height").toDouble() <= window->height());
            QTest::keyClick(window, Qt::Key_Escape);
            QTRY_VERIFY(!board->property("visible").toBool());
            window->setProperty("helpVisible", true);
            capture("help-" + language);
            window->setProperty("helpVisible", false);
        }
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        qApp->removeTranslator(&translator);
    }

    void bundledMusicAndEffectsDecode() {
        QList<QUrl> sources;
        // Tracks play from files: a compiled-in track would be copied to a
        // temporary file that is never removed.
        for (int i = 0; i < Audio::TrackCount; ++i) {
            QVERIFY(Audio::trackSource(i).isLocalFile());
            sources << Audio::trackSource(i);
        }
        for (int i = 0; i < Audio::EffectCount; ++i) sources << Audio::effectSource(Audio::Effect(i));
        for (int chain = 2; chain <= Audio::RingSteps; ++chain) sources << Audio::ringSource(chain);
        for (const auto &source : sources) {
            QAudioDecoder decoder;
            decoder.setSource(source);
            decoder.start();
            QTRY_VERIFY_WITH_TIMEOUT(decoder.bufferAvailable() || decoder.error() != QAudioDecoder::NoError, 10000);
            QVERIFY2(decoder.error() == QAudioDecoder::NoError, qPrintable(source.toString() + ": " + decoder.errorString()));
            const QAudioBuffer buffer = decoder.read();
            QVERIFY(buffer.isValid());
            QVERIFY(buffer.frameCount() > 0);
            QCOMPARE(buffer.format().sampleRate(), 44100);
            decoder.stop();
        }
    }

    void soundtrackPausesSkipsAndLoopsWithoutPlayingOldCues() {
        if (QMediaDevices::defaultAudioOutput().isNull()) QSKIP("No audio device; decoding is tested independently.");
        Audio sound;
        sound.setVolume(0, 0); // Exercise the real player without making test noise.
        sound.setRunning(true);
        auto player = sound.findChild<QMediaPlayer *>();
        QVERIFY(player);
        QTRY_VERIFY_WITH_TIMEOUT(player->duration() > 180000 || !sound.error().isEmpty(), 10000);
        QVERIFY2(sound.error().isEmpty(), qPrintable(sound.error()));
        QCOMPARE(sound.trackTitle(), QString("Pyramids Of Glass"));
        QTRY_VERIFY(player->position() > 100);
        sound.setRunning(false);
        QCOMPARE(player->playbackState(), QMediaPlayer::PausedState);
        const auto pausedAt = player->position();
        QTest::qWait(100);
        QCOMPARE(player->position(), pausedAt);
        sound.setRunning(true);
        QTRY_VERIFY(player->position() > pausedAt);
        sound.nextTrack();
        QCOMPARE(sound.track(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(player->isSeekable() && player->duration() > 170000 && player->duration() < 180000, 10000);
        player->setPosition(player->duration() - 200);
        QTRY_COMPARE_WITH_TIMEOUT(sound.track(), 2, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(player->isSeekable() && player->duration() > 174000 && player->duration() < 175500, 10000);
        player->setPosition(player->duration() - 200);
        QTRY_COMPARE_WITH_TIMEOUT(sound.track(), 0, 10000);
        const auto effects = sound.findChildren<QSoundEffect *>();
        QCOMPARE(effects.size(), Audio::EffectCount + Audio::RingSteps - 1);
        for (auto effect : effects) QTRY_COMPARE(effect->status(), QSoundEffect::Ready);
        sound.play(Audio::Ring);
        auto ring = *std::find_if(effects.begin(), effects.end(), [](QSoundEffect *e) { return e->source() == Audio::effectSource(Audio::Ring); });
        QTRY_VERIFY(ring->isPlaying());
        sound.setRunning(false);
        QVERIFY(!ring->isPlaying());
        sound.play(Audio::Ring);
        QVERIFY(!ring->isPlaying());
    }

    void cameraFramesTheShipBelowAnOpenHorizon() {
        Flight flight;
        flight.demo = false;
        flight.ship.speed = 0;
        flight.advance(3);
        const double distance = std::hypot(flight.ship.x - flight.camera.x, flight.ship.z - flight.camera.z);
        QVERIFY(distance > 17 && distance < 22);
        QVERIFY(flight.camera.pitch > -0.08 && flight.camera.pitch < 0);
        Scene scene;
        QImage image(960,540,QImage::Format_ARGB32_Premultiplied);
        QPainter painter(&image);
        scene.paint(painter,flight,image.size(),{});
        QRectF ship;
        for (const auto &f : scene.faces()) if (f.ship) ship |= f.polygon.boundingRect();
        QVERIFY(ship.center().y() > 340 && ship.center().y() < 440);
        QVERIFY(ship.width() < 230);
    }

    void gameplayCuesOccurOncePerAction() {
        Flight f;
        f.demo = false;
        f.press(Flight::Boost);
        f.step(0.016);
        QVERIFY(f.events & Flight::BoostStarted);
        f.step(0.016);
        QVERIFY(!(f.events & Flight::BoostStarted));
        f.release(Flight::Boost); f.step(0.016);
        f.press(Flight::Boost); f.step(0.016);
        QVERIFY(f.events & Flight::BoostStarted);
        f.ship.x = 0; f.ship.y = World::RingHeight; f.ship.z = 23.9;
        f.step(0.016);
        QVERIFY(f.events & Flight::RingPassed);
        f.step(0.016);
        QVERIFY(!(f.events & Flight::RingPassed));
        const Ring ring = World::ring(1);
        f.ship.x = ring.x + World::RingRadius; f.ship.y = ring.y; f.ship.z = ring.z - 0.1;
        f.game.hull = 10;
        f.step(0.016);
        QVERIFY(f.events & Flight::Impact);
        QVERIFY(f.events & Flight::Wreck);
        f.step(0.016);
        QCOMPARE(f.events, 0u);
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        QFile colors(dir.path() + "/theme/colors.toml");
        QVERIFY(colors.open(QIODevice::WriteOnly));
        colors.write("accent = \"#112233\"\n");
        colors.close();

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        auto write = [](const QString &path, const QByteArray &data) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        QVERIFY(write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n"));
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(write(response, "{\"int\":0}"));

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        QVERIFY(write(response, "{\"int\":6}"));
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"}})");
        write(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }

private:
    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
};

QTEST_MAIN(OmawingTests)
#include "omawing_tests.moc"
