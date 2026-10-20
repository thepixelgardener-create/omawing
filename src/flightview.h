#pragma once

#include <QElapsedTimer>
#include <QQuickPaintedItem>

#include "backend.h"
#include "scene.h"

// The flight, painted into the window. It repaints whenever the backend
// moves the flight forward.
class FlightView : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(Backend *source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(double renderMs READ renderMs NOTIFY rendered)
    Q_PROPERTY(int polygons READ polygons NOTIFY rendered)

public:
    explicit FlightView(QQuickItem *parent = nullptr);

    Backend *source() const { return m_backend; }
    void setSource(Backend *backend);
    double renderMs() const { return m_renderMs; }
    int polygons() const { return m_polygons; }

    void paint(QPainter *painter) override;

signals:
    void sourceChanged();
    void rendered();

private:
    Backend *m_backend = nullptr;
    Scene m_scene;
    double m_renderMs = 0;
    int m_polygons = 0;
    bool m_stats = false;
    int m_statFrames = 0;
    double m_statTotal = 0, m_statWorst = 0;
    QElapsedTimer m_statClock, m_statInterval;
    double m_statGap = 0;
    int m_statHitches = 0;
    double m_stepLow = 1e9, m_stepHigh = 0;
};
