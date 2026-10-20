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
        disconnect(m_backend, nullptr, this, nullptr);
    m_backend = backend;
    if (m_backend) {
        connect(m_backend, &Backend::frameChanged, this, [this] { update(); });
        // The overlay's status text changes with pause and the autopilot.
        connect(m_backend, &Backend::changed, this, [this] { update(); });
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
    m_polygons = m_scene.paint(*painter, m_backend->flight(), size(), m_backend->hud(), m_backend->look());
    m_renderMs = timer.nsecsElapsed() / 1e6;
    emit rendered();
    // OMAWING_STATS=1 reports frame rate and paint time once a second, for
    // checking performance on a real display.
    if (m_stats) {
        // A hitch is a frame shown more than one and a half refreshes after
        // the one before it.
        if (m_statInterval.isValid()) {
            const double gap = m_statInterval.nsecsElapsed() / 1e6;
            m_statGap = std::max(m_statGap, gap);
            if (gap > 25) {
                m_statHitches++;
                std::fprintf(stderr, "omawing: hitch %.1f ms between frames (paint %.1f ms, step %.1f ms)\n", gap, m_renderMs,
                             m_backend->lastStep() * 1000);
            }
        }
        m_statInterval.start();
        m_stepLow = std::min(m_stepLow, m_backend->lastStep() * 1000);
        m_stepHigh = std::max(m_stepHigh, m_backend->lastStep() * 1000);
        m_statFrames++;
        m_statTotal += m_renderMs;
        m_statWorst = std::max(m_statWorst, m_renderMs);
        if (!m_statClock.isValid())
            m_statClock.start();
        if (m_statClock.elapsed() >= 1000) {
            std::fprintf(stderr, "omawing: %.0f fps, paint %.1f ms mean, %.1f ms worst, longest gap %.1f ms, steps %.1f-%.1f ms, %d hitches, %d polygons, %dx%d px, %s\n",
                         m_statFrames * 1000.0 / m_statClock.elapsed(), m_statTotal / m_statFrames, m_statWorst, m_statGap,
                         m_stepLow, m_stepHigh, m_statHitches, m_polygons,
                         int(width() * window()->devicePixelRatio()), int(height() * window()->devicePixelRatio()),
                         painter->paintEngine()->type() == QPaintEngine::OpenGL2 ? "gpu" : "cpu");
            m_statFrames = 0;
            m_statTotal = m_statWorst = m_statGap = 0;
            m_statHitches = 0;
            m_stepLow = 1e9;
            m_stepHigh = 0;
            m_statClock.restart();
        }
    }
}
