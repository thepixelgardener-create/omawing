// Times whole frames painted into a multisampled OpenGL framebuffer, the
// way the app paints, for each act and for a few of the heaviest moments.
// bin/benchmark builds and runs it; pass --cpu to paint into an image
// instead, the path Qt falls back to without OpenGL.
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLPaintDevice>
#include <QPainter>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

#include "scene.h"

namespace {
struct Fixture {
    const char *name;
    // Sets up the flight (and the look) for the frames timed.
    std::function<void(Flight &, Scene::Look &)> setUp;
};

void report(const char *name, const QSize &size, std::vector<double> times) {
    if (times.empty())
        return;
    double sum = 0;
    for (double t : times)
        sum += t;
    std::sort(times.begin(), times.end());
    std::printf("%-22s %dx%d: mean %.3f ms, median %.3f ms, p95 %.3f ms\n", name, size.width(), size.height(),
                sum / times.size(), times[times.size() / 2], times[times.size() * 95 / 100]);
}
}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    const bool cpu = argc > 1 && std::strcmp(argv[1], "--cpu") == 0;

    QOpenGLContext context;
    QOffscreenSurface surface;
    if (!cpu) {
        QSurfaceFormat format;
        format.setSamples(4);
        context.setFormat(format);
        if (!context.create()) {
            std::fprintf(stderr, "render_benchmark: no OpenGL context (try --cpu)\n");
            return 2;
        }
        surface.setFormat(context.format());
        surface.create();
        if (!context.makeCurrent(&surface)) {
            std::fprintf(stderr, "render_benchmark: could not make the OpenGL context current\n");
            return 3;
        }
    }

    const Scene::Hud hud{"YOU HAVE THE CONTROLS", "FREE FLIGHT · 02 / 24", "THRUST", "ALT 04.2", "Flying into the future",
                         "HULL", "SCORE", "001350", "BEST 001350"};
    std::vector<Fixture> fixtures;
    for (int act = 0; act < Flight::Acts; ++act)
        fixtures.push_back({View::actAt(act).name, [act](Flight &f, Scene::Look &) { f.scene(act); }});
    // The most floor on screen: as high as the ship goes.
    fixtures.push_back({"high altitude", [](Flight &f, Scene::Look &) {
        f.scene(0);
        f.demo = false;
        f.ship.y = Flight::MaxAltitude;
        f.camera.y = Flight::MaxAltitude + 5;
    }});
    // A full sky of particles over the mirror floor, which doubles the scenery.
    fixtures.push_back({"burst on the mirage", [](Flight &f, Scene::Look &) {
        f.scene(4);
        f.emitParticles(Particle::Glitter, {f.ship.x, f.ship.y, f.ship.z + 12}, {0, 0, f.ship.speed}, 8, Flight::MaxParticles);
    }});
    fixtures.push_back({"wreck", [](Flight &f, Scene::Look &) {
        f.scene(2);
        f.demo = false;
        f.game.wrecked = 0;
        f.game.hull = 0;
        f.emitParticles(Particle::Spark, {f.ship.x, f.ship.y, f.ship.z}, {0, 4, 0}, 13, 300);
    }});
    fixtures.push_back({"strong beat", [](Flight &f, Scene::Look &look) {
        f.scene(1);
        look.beat = 1;
        look.level = 1;
    }});

    for (const QSize size : {QSize(1920, 1080), QSize(3840, 2160)}) {
        std::unique_ptr<QOpenGLFramebufferObject> fbo;
        std::unique_ptr<QOpenGLPaintDevice> device;
        QImage image;
        if (cpu) {
            image = QImage(size, QImage::Format_ARGB32_Premultiplied);
        } else {
            QOpenGLFramebufferObjectFormat fbFormat;
            fbFormat.setSamples(4);
            fbFormat.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
            fbo = std::make_unique<QOpenGLFramebufferObject>(size, fbFormat);
            if (!fbo->isValid() || !fbo->bind()) {
                std::fprintf(stderr, "render_benchmark: could not set up a %dx%d framebuffer\n", size.width(), size.height());
                return 4;
            }
            device = std::make_unique<QOpenGLPaintDevice>(size);
        }
        QPaintDevice *target = cpu ? static_cast<QPaintDevice *>(&image) : device.get();
        std::vector<double> all;
        for (const Fixture &fixture : fixtures) {
            Scene scene;
            Flight flight;
            Scene::Look look;
            fixture.setUp(flight, look);
            std::vector<double> times;
            for (int i = 0; i < 45; ++i) {
                // Flying on would end a wreck; its frames hold still instead.
                if (flight.game.wrecked < 0)
                    flight.advance(1.0 / 60);
                QElapsedTimer timer;
                timer.start();
                {
                    QPainter p(target);
                    scene.paint(p, flight, size, hud, look);
                }
                if (!cpu)
                    context.functions()->glFinish();
                // The first frames prepare sprites and caches.
                if (i >= 5)
                    times.push_back(timer.nsecsElapsed() / 1e6);
            }
            report(fixture.name, size, times);
            all.insert(all.end(), times.begin(), times.end());
        }
        report("all", size, all);
    }
}
