#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

#include "backend.h"
#include "flight.h"
#include "scene.h"
#include "synth.h"
#include "theme.h"
#include "translator.h"

class OmawingTests : public QObject {
    Q_OBJECT

private slots:
    void demoPlaysAllThreeActsAndLoops()
    {
        Flight flight;
        QSet<int> acts;
        // 73 seconds: one second past the 72-second loop, clear of the boundary.
        for (int i = 0; i < 146; ++i) {
            flight.advance(0.5);
            acts.insert(flight.act());
            QVERIFY(std::isfinite(flight.ship.x) && std::isfinite(flight.camera.yaw));
            QVERIFY(flight.ship.y >= Flight::MinAltitude && flight.ship.y <= Flight::MaxAltitude);
        }
        QCOMPARE(acts.size(), 3);
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
            for (int i = 0; i < 146; ++i) {
                flight.advance(0.5);
                QPainter painter(&image);
                scene.paint(painter, flight, size, {});
                painter.end();
                int shipFaces = 0;
                for (const auto &face : scene.faces()) {
                    shipFaces += face.ship;
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
        Backend backend;
        QVERIFY(backend.demo());
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
        Backend backend;
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
        Backend backend;
        backend.keyDown(Qt::Key_Shift);
        backend.flight().advance(2);
        QVERIFY(backend.flight().ship.speed > 25);
    }

    void dragSteersLikeAStick()
    {
        Backend backend;
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
        Backend backend;
        backend.keyDown(Qt::Key_A);
        backend.flight().advance(0.1);
        backend.clearInput();
        const double yaw = backend.flight().ship.yaw;
        backend.flight().advance(1);
        QCOMPARE(backend.flight().ship.yaw, yaw);
    }

    void pauseFreezesTheFlight()
    {
        Backend backend;
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
        Backend backend;
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

    void hudReportsTheMode()
    {
        Backend backend;
        QCOMPARE(backend.hud().status, QString("AUTOPILOT / ENJOY THE RIDE"));
        backend.setPaused(true);
        QCOMPARE(backend.hud().status, QString("PAUSED"));
        backend.setPaused(false);
        backend.setDemo(false);
        QCOMPARE(backend.hud().status, QString("YOU HAVE THE CONTROLS"));
        QVERIFY(backend.hud().mode.startsWith("FREE FLIGHT"));
        QVERIFY(backend.hud().altitude.contains("03"));
    }

    void tunePlaysOnTheBeat()
    {
        Tune tune;
        std::vector<float> samples(Tune::SampleRate);
        tune.render(samples.data(), int(samples.size()));
        // Eight sixteenth notes in the first second, starting 30 ms in.
        QCOMPARE(tune.notesPlayed(), 8);
        double before = 0, after = 0;
        for (int i = 0; i < 1200; ++i) before = std::max(before, double(std::abs(samples[i])));
        for (int i = 1500; i < 6000; ++i) after = std::max(after, double(std::abs(samples[i])));
        QVERIFY(before < 1e-6);
        QVERIFY(after > 0.05);
        for (float s : samples)
            QVERIFY(s >= -1 && s <= 1);
        tune.restart();
        QCOMPARE(tune.notesPlayed(), 0);
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
