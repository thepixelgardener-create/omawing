#include "flightview.h"

#include <QElapsedTimer>
#include <QPaintEngine>
#include <QPainter>
#include <QQuickWindow>
#include <QScreen>
#include <algorithm>
#include <cstdio>

#include "backend.h"

FlightView::FlightView(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setOpaquePainting(true);
    setFillColor(QColor("#100d33"));
    // Painting straight into a GPU framebuffer, antialiased by multisampling,
    // is several times faster than painting pixels on the CPU and uploading
    // them each frame. Qt falls back to the CPU when OpenGL isn't in use.
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
    setAntialiasing(true);
    m_stats = qEnvironmentVariableIsSet("OMAWING_STATS");
}

void FlightView::setSource(Backend *backend) {
    if (backend == m_backend)
        return;
    if (m_backend)
        disconnect(m_backend.data(), nullptr, this, nullptr);
    m_backend = backend;
    if (m_backend) {
        connect(m_backend.data(), &Backend::frameChanged, this, [this] { update(); });
        // The overlay's status text changes with pause and the autopilot.
        connect(m_backend.data(), &Backend::changed, this, [this] { update(); });
        connect(m_backend.data(), &QObject::destroyed, this, [this] { update(); });
    }
    emit sourceChanged();
    update();
}

void FlightView::paint(QPainter *painter) {
    if (!m_backend || width() <= 0 || height() <= 0)
        return;
    // The flight steps by whole refreshes of the screen the window is on.
    if (window() && window()->screen())
        m_backend->setRefreshRate(window()->screen()->refreshRate());
    QElapsedTimer timer;
    timer.start();
    const int polygons = m_scene.paint(*painter, m_backend->flight(), size(), m_backend->hud(), m_backend->look());
    if (m_stats) {
        const double ratio = window() ? window()->devicePixelRatio() : 1;
        m_frameStats.record(timer.nsecsElapsed() / 1e6, m_backend->lastStep() * 1000, polygons,
                            QSize(int(width() * ratio), int(height() * ratio)),
                            painter->paintEngine()->type() == QPaintEngine::OpenGL2,
                            window() && window()->screen() ? window()->screen()->refreshRate() : 60);
    }
}

void FlightView::FrameStats::record(double paintMs, double stepMs, int polygons, const QSize &pixels, bool gpu, double refreshRate) {
    // A hitch is a frame shown more than one and a half refreshes after the
    // one before it.
    if (interval.isValid()) {
        const double gap = interval.nsecsElapsed() / 1e6;
        longestGap = std::max(longestGap, gap);
        if (gap > 1500.0 / (refreshRate >= 20 && refreshRate <= 500 ? refreshRate : 60)) {
            hitches++;
            std::fprintf(stderr, "omawing: hitch %.1f ms between frames (paint %.1f ms, step %.1f ms)\n", gap, paintMs, stepMs);
        }
    }
    interval.start();
    stepLow = std::min(stepLow, stepMs);
    stepHigh = std::max(stepHigh, stepMs);
    frames++;
    total += paintMs;
    worst = std::max(worst, paintMs);
    if (!clock.isValid())
        clock.start();
    if (clock.elapsed() < 1000)
        return;
    std::fprintf(stderr,
                 "omawing: %.0f fps, paint %.1f ms mean, %.1f ms worst, longest gap %.1f ms, steps %.1f-%.1f ms, %d hitches, "
                 "%d polygons, %dx%d px, %s\n",
                 frames * 1000.0 / clock.elapsed(), total / frames, worst, longestGap, stepLow, stepHigh, hitches, polygons,
                 pixels.width(), pixels.height(), gpu ? "gpu" : "cpu");
    const QElapsedTimer lastInterval = interval;
    *this = FrameStats();
    interval = lastInterval;
    clock.start();
}
