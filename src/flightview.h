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
        void record(double paintMs, double stepMs, int polygons, const QSize &pixels, bool gpu, double refreshRate);
        // Frames stopped on purpose (paused, or hidden): the next one isn't
        // late, however long it has been.
        void stop() { interval.invalidate(); }

        int frames = 0, hitches = 0;
        double total = 0, worst = 0, longestGap = 0;
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
