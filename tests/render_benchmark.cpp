// Times whole frames painted into a multisampled OpenGL framebuffer, the
// way the app paints, for each act and for a few of the heaviest moments,
// and counts the painting work each one asks for. bin/benchmark builds and
// runs it.
//
//   --cpu            paint into an image instead, the path Qt falls back to
//                    without OpenGL (and the only one on a machine without a GPU)
//   --parts          also time each part of the frame: sky, ground, building
//                    the world, painting it, and the overlay
//   --frames N       frames timed per scene, after a few to warm up (60)
//   --size WxH       the size to paint at; may be repeated (1920x1080 and 3840x2160)
//   --record FILE    write the painting work per scene to FILE
//   --check FILE     compare the painting work with FILE, and fail if any scene
//                    asks for more than a little over it
//
// Timings depend on the machine and what else it is doing; the work counted
// doesn't, so a check against a recorded baseline is reliable anywhere, CI
// included. It is counted over two seconds of frames after the warm-up: the
// draw calls, the points in them, and changes of painter state of the
// busiest frame, and over all of them, the geometry and images that are new
// (that the GPU must be sent again) and the HUD strings laid out afresh.
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLPaintDevice>
#include <QPaintEngine>
#include <QPainter>
#include <QPainterPath>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <climits>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <memory>
#include <vector>

#include "scene.h"

namespace {
// What a frame is painted at: its size in logical pixels and the display's
// scale, so the pixels painted are size * ratio.
struct Shape {
    QSizeF size;
    double ratio = 1;
};

struct Fixture {
    const char *name;
    // Sets up the flight (and the look) for the frames timed.
    std::function<void(Flight &, Scene::Look &)> setUp;
    // Changes things before each frame, given its number from 0.
    std::function<void(Flight &, Scene::Look &, int)> update = {};
    // The HUD of each frame; unset, the same every frame.
    std::function<Scene::Hud(const Flight &, int)> hud = {};
    // The shape of each frame, given the size of the run; unset, that size.
    std::function<Shape(const QSize &, int)> shape = {};
};

const Scene::Hud fixedHud{"YOU HAVE THE CONTROLS", "FREE FLIGHT · 02 / 24", "THRUST", "ALT 04.2",
                          "Flying into the future", "HULL", "SCORE", "001350", "BEST 001350"};

// The words of the HUD, in the game's English and in its Arabic catalog.
struct HudWords {
    const char *language, *status, *mode, *thrust, *altitude, *subtitle, *hull, *score, *best, *titles, *menu,
        *banner;
};
const HudWords english{"en", "YOU HAVE THE CONTROLS", "FREE FLIGHT", "THRUST", "ALT %1", "Flying into the future",
                       "HULL", "SCORE", "BEST %1", "TITLES", "MENU", "BANNER"};
const HudWords arabic{"ar", "التحكم بين يديك", "طيران حر", "الدفع", "الارتفاع %1", "نحلّق نحو المستقبل", "الهيكل",
                      "النقاط", "الأفضل %1", "العناوين", "القائمة", "الشريط"};

QString padded(const QLocale &locale, int value, int width) {
    const QString text = locale.toString(value);
    return text.size() < width ? QString(locale.zeroDigit()).repeated(width - text.size()) + text : text;
}

// The HUD as the game words it each frame (Backend::hud): the clock,
// altitude, and score change as you fly. Here the score climbs every frame,
// faster than any pilot, so each frame brings new strings.
Scene::Hud liveHud(const Flight &f, int frame, const HudWords &words) {
    // The language's own digits, the same on every machine (the game asks
    // Omarchy which locale to format in).
    const QLocale locale(QString::fromLatin1(words.language));
    const int second = int(std::floor(std::fmod(f.time, Flight::ActLength)));
    const QString clock = padded(locale, second, 2) + " / " + locale.toString(int(Flight::ActLength));
    QString altitude = locale.toString(f.ship.y, 'f', 1);
    if (f.ship.y < 10)
        altitude.prepend(locale.zeroDigit());
    const int score = 1000 + frame * 10;
    Scene::Hud hud{QString::fromUtf8(words.status), QString::fromUtf8(words.mode) + QString::fromUtf8(" · ") + clock,
                   QString::fromUtf8(words.thrust), QString::fromUtf8(words.altitude).arg(altitude),
                   QString::fromUtf8(words.subtitle)};
    hud.hull = QString::fromUtf8(words.hull);
    hud.score = QString::fromUtf8(words.score);
    hud.points = padded(locale, score, 6);
    hud.best = QString::fromUtf8(words.best).arg(padded(locale, score, 6));
    hud.titlesSwitch = QString::fromUtf8(words.titles);
    hud.toolbarSwitch = QString::fromUtf8(words.menu);
    hud.bannerSwitch = QString::fromUtf8(words.banner);
    return hud;
}

std::vector<Fixture> fixtures() {
    std::vector<Fixture> list;
    for (int act = 0; act < Flight::Acts; ++act)
        list.push_back({View::actAt(act).name, [act](Flight &f, Scene::Look &) { f.scene(act); }});
    // The most floor on screen: as high as the ship goes.
    list.push_back({"high altitude", [](Flight &f, Scene::Look &) {
        f.scene(0);
        f.demo = false;
        f.ship.y = Flight::MaxAltitude;
        f.camera.y = Flight::MaxAltitude + 5;
    }});
    // A full sky of particles over the mirror floor, which doubles the scenery.
    list.push_back({"burst on the mirage", [](Flight &f, Scene::Look &) {
        f.scene(4);
        f.emitParticles(Particle::Glitter, {f.ship.x, f.ship.y, f.ship.z + 12}, {0, 0, f.ship.speed}, 8, Flight::MaxParticles);
    }});
    list.push_back({"wreck", [](Flight &f, Scene::Look &) {
        f.scene(2);
        f.demo = false;
        f.game.wrecked = 0;
        f.game.hull = 0;
        f.emitParticles(Particle::Spark, {f.ship.x, f.ship.y, f.ship.z}, {0, 4, 0}, 13, 300);
    }});
    list.push_back({"strong beat", [](Flight &f, Scene::Look &look) {
        f.scene(1);
        look.beat = 1;
        look.level = 1;
    }});
    list.push_back({"sustained particles", [](Flight &f, Scene::Look &) { f.scene(4); },
        [](Flight &f, Scene::Look &, int) {
            f.particles.clear();
            f.emitParticles(Particle::Glitter, {f.ship.x, f.ship.y, f.ship.z + 12},
                            {0, 0, f.ship.speed}, 8, Flight::MaxParticles);
        }});
    // The two seconds over which the colours, and the mirror floor and the
    // valley, blend in from the act before.
    list.push_back({"mirage transition", [](Flight &f, Scene::Look &) { f.scene(4); },
        [](Flight &f, Scene::Look &, int frame) {
            f.time = 4 * Flight::ActLength + (frame % 120) / 60.0;
        }});
    // Flying yourself, with the switch keys showing.
    auto playing = [](Flight &f, Scene::Look &look) {
        f.scene(1);
        f.demo = false;
        look.switches = 1;
    };
    list.push_back({"live hud", playing, {},
                    [](const Flight &f, int frame) { return liveHud(f, frame, english); }});
    list.push_back({"arabic hud", playing, {},
                    [](const Flight &f, int frame) { return liveHud(f, frame, arabic); }});
    // More new strings every frame than the HUD's cache holds, by far: the
    // labels that never change must stay laid out all the same.
    list.push_back({"text cache overflow", playing, {}, [](const Flight &f, int frame) {
        Scene::Hud hud = liveHud(f, frame, english);
        const QString n = QString::number(frame);
        hud.status += ' ' + n;
        hud.mode += ' ' + n;
        hud.subtitle += ' ' + n;
        return hud;
    }});
    // Dragging the window's edge: narrower, then back, over two seconds.
    list.push_back({"window resize", [](Flight &f, Scene::Look &) { f.scene(0); }, {}, {},
        [](const QSize &full, int frame) {
            const double t = 0.5 - 0.5 * std::cos(frame / 120.0 * 2 * M_PI);
            return Shape{QSizeF(std::round(full.width() * (1 - t / 3)), full.height())};
        }});
    // The window moving between screens of different scales: the same
    // size on each, in more pixels.
    list.push_back({"display scale change", [](Flight &f, Scene::Look &) { f.scene(0); }, {}, {},
        [](const QSize &full, int frame) {
            static const double ratios[] = {1, 1.25, 1.5, 2};
            return Shape{QSizeF(full) / 2, ratios[frame / 30 % 4]};
        }});
    return list;
}

constexpr int WarmUp = 8;
// The painting work is counted over two seconds at 60 Hz: one whole
// transition, and enough frames to fill the HUD's cache of strings.
constexpr int CountFrames = 120;
const char *const parts[] = {"sky", "ground", "world build", "world", "overlay"};

// Flies a fixture on by a frame; a wreck holds still, as flying on would end it.
void step(Flight &flight) {
    if (flight.game.wrecked < 0)
        flight.advance(1.0 / 60);
}

// Paints frame `frame` of a fixture onto `target`, `full` pixels in size.
void paintFrame(QPaintDevice *target, Scene &scene, const Fixture &fixture, Flight &flight, Scene::Look &look,
                const QSize &full, int frame) {
    step(flight);
    if (fixture.update) fixture.update(flight, look, frame);
    const Shape shape = fixture.shape ? fixture.shape(full, frame) : Shape{QSizeF(full)};
    QPainter p(target);
    p.scale(shape.ratio, shape.ratio);
    scene.paint(p, flight, shape.size, fixture.hud ? fixture.hud(flight, frame) : fixedHud, look);
}

double percentile(std::vector<double> times, double p) {
    if (times.empty())
        return 0;
    std::sort(times.begin(), times.end());
    return times[std::min(times.size() - 1, size_t(times.size() * p))];
}

double mean(const std::vector<double> &times) {
    double sum = 0;
    for (double t : times)
        sum += t;
    return times.empty() ? 0 : sum / times.size();
}

// The painting work, as a paint engine sees it. Of the busiest frame: draw
// calls, the points in them, and changes of painter state. Of all the frames
// counted: the points of filled paths not seen before, which the GPU engine
// must cut into triangles again; images not seen before, which it must
// upload; and the HUD strings laid out, which the overlay keeps in a cache.
struct Work {
    long calls = 0, points = 0, states = 0;
    long newPathPoints = 0, newImages = 0, textLayouts = 0;
};
const char *const columns[] = {"draw calls", "points", "state changes", "new path points", "new images",
                               "text layouts"};
long Work::*const fields[] = {&Work::calls, &Work::points, &Work::states, &Work::newPathPoints, &Work::newImages,
                              &Work::textLayouts};

// A batch of rectangles, lines, or points handed over at once is one call,
// as it is for a real engine. Unique filled-path points are a geometry-work
// proxy, not a measurement of driver triangulation: Qt can use stencil fills
// instead, and its caches depend on path identity rather than our value hash.
class CountingEngine : public QPaintEngine {
public:
    CountingEngine() : QPaintEngine(QPaintEngine::AllFeatures) {}
    Work work;
    std::set<size_t> seen;
    std::set<qint64> images;

    bool begin(QPaintDevice *) override { return true; }
    bool end() override { return true; }
    Type type() const override { return QPaintEngine::User; }
    void updateState(const QPaintEngineState &) override { ++work.states; }
    void drawPolygon(const QPointF *, int count, PolygonDrawMode) override { add(1, count); }
    void drawPath(const QPainterPath &path) override {
        if (state->brush().style() == Qt::NoBrush) {
            add(1, path.elementCount());
            return;
        }
        ++work.calls;
        size_t key = qHash(int(path.fillRule()));
        for (int i = 0; i < path.elementCount(); ++i) {
            const QPainterPath::Element e = path.elementAt(i);
            key = qHashMulti(key, int(e.type), e.x, e.y);
        }
        if (seen.insert(key).second)
            work.newPathPoints += path.elementCount();
    }
    void drawRects(const QRectF *, int count) override { add(1, 4 * count); }
    void drawEllipse(const QRectF &) override { add(1, 4); }
    void drawLines(const QLineF *, int count) override { add(1, 2 * count); }
    void drawPoints(const QPointF *, int count) override { add(1, count); }
    void drawTextItem(const QPointF &, const QTextItem &) override { add(1, 4); }
    void drawPixmap(const QRectF &, const QPixmap &pixmap, const QRectF &) override { image(pixmap.cacheKey()); }
    void drawTiledPixmap(const QRectF &, const QPixmap &pixmap, const QPointF &) override { image(pixmap.cacheKey()); }
    void drawImage(const QRectF &, const QImage &image, const QRectF &, Qt::ImageConversionFlags) override {
        this->image(image.cacheKey());
    }

private:
    void add(long calls, long points) {
        work.calls += calls;
        work.points += points;
    }
    void image(qint64 key) {
        add(1, 4);
        work.newImages += images.insert(key).second;
    }
};

class CountingDevice : public QPaintDevice {
public:
    explicit CountingDevice(const QSize &size) : m_size(size) {}
    QPaintEngine *paintEngine() const override { return &m_engine; }
    Work &work() { return m_engine.work; }

protected:
    int metric(PaintDeviceMetric metric) const override {
        switch (metric) {
        case PdmWidth: return m_size.width();
        case PdmHeight: return m_size.height();
        case PdmWidthMM: return m_size.width() * 254 / 960;
        case PdmHeightMM: return m_size.height() * 254 / 960;
        case PdmNumColors: return INT_MAX;
        case PdmDepth: return 32;
        case PdmDpiX: case PdmDpiY: case PdmPhysicalDpiX: case PdmPhysicalDpiY: return 96;
        default: return QPaintDevice::metric(metric);
        }
    }

private:
    QSize m_size;
    mutable CountingEngine m_engine;
};

// The work of each fixture over CountFrames frames, after the same warm-up
// as the timings.
std::map<std::string, Work> countWork(const std::vector<Fixture> &list) {
    std::map<std::string, Work> result;
    const QSize full(1920, 1080);
    for (const Fixture &fixture : list) {
        Scene scene;
        Flight flight;
        Scene::Look look;
        fixture.setUp(flight, look);
        CountingDevice device(full);
        Work &total = result[fixture.name];
        long layouts = 0;
        for (int i = 0; i < WarmUp + CountFrames; ++i) {
            if (i == WarmUp)
                layouts = scene.textLayouts();
            device.work() = {};
            paintFrame(&device, scene, fixture, flight, look, full, i);
            if (i < WarmUp)
                continue;
            const Work &frame = device.work();
            total.calls = std::max(total.calls, frame.calls);
            total.points = std::max(total.points, frame.points);
            total.states = std::max(total.states, frame.states);
            total.newPathPoints += frame.newPathPoints;
            total.newImages += frame.newImages;
        }
        total.textLayouts = scene.textLayouts() - layouts;
    }
    return result;
}

// The baseline is one line per fixture: its name, then each of Work's
// counts, in the order of `columns`, separated by tabs.
std::map<std::string, Work> readWork(const QString &path) {
    std::map<std::string, Work> result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return result;
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const QStringList fields = line.split('\t');
        if (line.startsWith('#') || fields.size() != 1 + int(std::size(columns)))
            continue;
        Work &work = result[fields[0].toStdString()];
        for (size_t i = 0; i < std::size(columns); ++i)
            work.*::fields[i] = fields[i + 1].toLong();
    }
    return result;
}

bool writeWork(const QString &path, const std::map<std::string, Work> &work) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;
    QTextStream out(&file);
    out << "# The painting work of each benchmark scene at 1920x1080, over two seconds\n"
           "# of frames: the draw calls, points, and painter state changes of the\n"
           "# busiest frame; and over them all, new filled-path points, new images,\n"
           "# and HUD strings laid out.\n"
           "# Written by bin/benchmark --record; CI checks against it.\n"
           "# scene";
    for (const char *column : columns)
        out << '\t' << column;
    out << '\n';
    for (const auto &[name, w] : work) {
        out << QString::fromStdString(name);
        for (const auto field : fields)
            out << '\t' << w.*field;
        out << '\n';
    }
    return true;
}

// A scene may ask for a little more than its baseline: floating point can
// clip a polygon differently on another machine. New images and text
// layouts are made the same everywhere, and a few more of them is just what
// a cache going wrong looks like, so they may not exceed it at all.
constexpr double Tolerance = 1.03;
constexpr long Slack = 16;

bool checkWork(const std::map<std::string, Work> &baseline, const std::map<std::string, Work> &work) {
    bool ok = true;
    std::printf("\n| scene |");
    for (const char *column : columns)
        std::printf(" %s |", column);
    std::printf("\n| --- |");
    for (size_t i = 0; i < std::size(columns); ++i)
        std::printf(" ---: |");
    std::printf("\n");
    for (const auto &[name, ignored] : baseline) {
        if (!work.count(name)) {
            std::fprintf(stderr, "render_benchmark: baseline scene no longer exercised: %s\n", name.c_str());
            ok = false;
        }
    }
    auto cell = [&](long now, long before, bool exact) {
        const bool over = exact ? now > before : now > before * Tolerance + Slack;
        ok &= !over;
        char text[96];
        if (before <= 0)
            std::snprintf(text, sizeof text, "%ld%s", now, over ? " **over**" : "");
        else
            std::snprintf(text, sizeof text, "%ld (%+.1f%%)%s", now, 100.0 * (now - before) / before, over ? " **over**" : "");
        return std::string(text);
    };
    for (const auto &[name, w] : work) {
        const auto found = baseline.find(name);
        if (found == baseline.end()) {
            std::fprintf(stderr, "render_benchmark: scene missing from baseline: %s\n", name.c_str());
            ok = false;
        }
        const Work before = found == baseline.end() ? Work{} : found->second;
        std::printf("| %s |", name.c_str());
        for (const auto field : fields) {
            const bool exact = field == &Work::newImages || field == &Work::textLayouts;
            std::printf(" %s |", cell(w.*field, before.*field, exact).c_str());
        }
        std::printf("\n");
    }
    if (!ok)
        std::printf("\nSome scenes ask for more painting work than tests/benchmark-baseline.tsv allows. If that is "
                    "intended, record a new baseline with bin/benchmark --record tests/benchmark-baseline.tsv.\n");
    return ok;
}
}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    bool cpu = false, timeParts = false;
    int frames = 60;
    std::vector<QSize> sizes;
    QString record, check;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (!std::strcmp(arg, "--cpu")) cpu = true;
        else if (!std::strcmp(arg, "--parts")) timeParts = true;
        else if (!std::strcmp(arg, "--frames") && hasValue) frames = std::max(1, std::atoi(argv[++i]));
        else if (!std::strcmp(arg, "--record") && hasValue) record = argv[++i];
        else if (!std::strcmp(arg, "--check") && hasValue) check = argv[++i];
        else if (!std::strcmp(arg, "--size") && hasValue) {
            int w = 0, h = 0;
            if (std::sscanf(argv[++i], "%dx%d", &w, &h) == 2 && w > 0 && h > 0)
                sizes.push_back(QSize(w, h));
        } else {
            std::fprintf(stderr, "usage: render_benchmark [--cpu] [--parts] [--frames N] [--size WxH]... "
                                 "[--record FILE] [--check FILE]\n");
            return 1;
        }
    }
    if (sizes.empty())
        sizes = {QSize(1920, 1080), QSize(3840, 2160)};

    const std::vector<Fixture> list = fixtures();
    // Counting needs no GPU and takes a moment; recording or checking does
    // only that.
    if (!record.isEmpty() || !check.isEmpty()) {
        const auto work = countWork(list);
        if (!record.isEmpty() && !writeWork(record, work)) {
            std::fprintf(stderr, "render_benchmark: could not write %s\n", qPrintable(record));
            return 5;
        }
        if (!check.isEmpty()) {
            const auto baseline = readWork(check);
            if (baseline.empty()) {
                std::fprintf(stderr, "render_benchmark: no baseline in %s\n", qPrintable(check));
                return 5;
            }
            if (!checkWork(baseline, work))
                return 6;
        }
        return 0;
    }

    QOpenGLContext context;
    QOffscreenSurface surface;
    if (!cpu) {
        QSurfaceFormat format;
        format.setSamples(4);
        format.setDepthBufferSize(24);
        format.setStencilBufferSize(8);
        context.setFormat(format);
        if (!context.create()) {
            std::fprintf(stderr, "render_benchmark: no OpenGL context (try --cpu)\n");
            return 2;
        }
        // Qt's paint engine chooses its stencil path from the context
        // format, not just the FBO attachment. Match Qt Quick's defaults.
        if (context.format().stencilBufferSize() < 8) {
            std::fprintf(stderr, "render_benchmark: context lacks an 8-bit stencil buffer (try --cpu)\n");
            return 2;
        }
        surface.setFormat(context.format());
        surface.create();
        if (!context.makeCurrent(&surface)) {
            std::fprintf(stderr, "render_benchmark: could not make the OpenGL context current\n");
            return 3;
        }
    }

    for (const QSize size : sizes) {
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
            GLint stencilBits = 0;
            context.functions()->glGetIntegerv(GL_STENCIL_BITS, &stencilBits);
            std::printf("OpenGL renderer: %s; context stencil: %d; framebuffer stencil: %d; samples: %d\n",
                        reinterpret_cast<const char *>(context.functions()->glGetString(GL_RENDERER)),
                        context.format().stencilBufferSize(), stencilBits, fbo->format().samples());
            if (stencilBits < 8 || fbo->format().samples() < 4) {
                std::fprintf(stderr, "render_benchmark: framebuffer lacks the requested stencil or multisampling\n");
                return 4;
            }
            device = std::make_unique<QOpenGLPaintDevice>(size);
        }
        QPaintDevice *target = cpu ? static_cast<QPaintDevice *>(&image) : device.get();
        auto finish = [&] {
            if (!cpu)
                context.functions()->glFinish();
        };

        std::printf("\n%dx%d, %s, ms per frame\n\n| scene | mean | median | p95 | p99 |", size.width(), size.height(),
                    cpu ? "cpu" : "gpu");
        if (timeParts)
            for (const char *part : parts)
                std::printf(" %s |", part);
        std::printf("\n| --- | ---: | ---: | ---: | ---: |");
        if (timeParts)
            for (size_t i = 0; i < std::size(parts); ++i)
                std::printf(" ---: |");
        std::printf("\n");

        std::vector<double> all;
        for (const Fixture &fixture : list) {
            Scene scene;
            Flight flight;
            Scene::Look look;
            fixture.setUp(flight, look);
            std::vector<double> times;
            std::map<std::string, std::vector<double>> partTimes;
            QElapsedTimer partClock;
            if (timeParts)
                scene.setProbe([&](const char *part) {
                    finish();
                    partTimes[part].push_back(partClock.nsecsElapsed() / 1e6);
                    partClock.start();
                });
            for (int i = 0; i < WarmUp + frames; ++i) {
                if (i == WarmUp)
                    partTimes.clear();
                QElapsedTimer timer;
                timer.start();
                partClock.start();
                paintFrame(target, scene, fixture, flight, look, size, i);
                finish();
                // The first frames prepare sprites and caches.
                if (i >= WarmUp)
                    times.push_back(timer.nsecsElapsed() / 1e6);
            }
            std::printf("| %s | %.2f | %.2f | %.2f | %.2f |", fixture.name, mean(times), percentile(times, 0.5),
                        percentile(times, 0.95), percentile(times, 0.99));
            if (timeParts)
                for (const char *part : parts)
                    std::printf(" %.2f |", mean(partTimes[part]));
            std::printf("\n");
            all.insert(all.end(), times.begin(), times.end());
        }
        std::printf("| **all** | **%.2f** | **%.2f** | **%.2f** | **%.2f** |\n", mean(all), percentile(all, 0.5), percentile(all, 0.95), percentile(all, 0.99));
    }
}
