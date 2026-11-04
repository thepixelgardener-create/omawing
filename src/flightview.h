#pragma once

#include <QElapsedTimer>
#include <QPointer>
#include <QQuickPaintedItem>

#include "backend.h"
#include "scene.h"

// The flight, painted into the window. It repaints whenever the backend
// moves the flight forward.
class FlightView : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(Backend *source READ source WRITE setSource NOTIFY sourceChanged)

public:
    explicit FlightView(QQuickItem *parent = nullptr);

    // The view only watches the backend: if it goes away first, the view
    // shows nothing rather than drawing from a dangling pointer.
    Backend *source() const { return m_backend; }
    void setSource(Backend *backend);

    void paint(QPainter *painter) override;

    // With OMAWING_STATS set, frame rate, paint times, and hitches are
    // reported on stderr once a second.
    struct FrameStats {
        // The times of a frame, in milliseconds: painting it, putting its
        // HUD's text together, the tick that moved the flight on, and the
        // time that tick simulated.
        struct Times {
            double paint = 0, hud = 0, tick = 0, step = 0;
        };
        void record(const Times &times, int polygons, const QSize &pixels, bool gpu, double refreshRate);
        // Frames stopped on purpose (paused, or hidden): the next one isn't
        // late, however long it has been.
        void stop() { interval.invalidate(); }

        int frames = 0, hitches = 0;
        double total = 0, worst = 0, longestGap = 0, hudTotal = 0, tickTotal = 0, tickWorst = 0;
        double stepLow = 1e9, stepHigh = 0;
        QElapsedTimer clock, interval;
    };
    // For the tests.
    const FrameStats &frameStats() const { return m_frameStats; }

signals:
    void sourceChanged();

private:
    QPointer<Backend> m_backend;
    Scene m_scene;
    bool m_stats = false;
    FrameStats m_frameStats;
};
