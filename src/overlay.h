#pragma once

// What sits on top of the world: speed lines, ring points, scanlines, the
// title, the HUD, and the sine scroller.

#include <QHash>
#include <QImage>
#include <QPointF>
#include <QRect>
#include <functional>

#include "view.h"

class QPainter;

class Overlay {
public:
    // Text and patterns never change shape, so they are drawn once per
    // window scale into images and reused every frame.
    void prepare(const View &view, double pixelsPerUnit);
    void paint(QPainter &p, const View &view, const SceneHud &hud);

private:
    struct Sprite {
        QImage image;
        QPointF offset;   // Canvas units from the anchor to the image's corner.
        QRect source;     // The part of a shared atlas image, or empty for all of it.
    };
    Sprite render(const QRectF &bounds, const std::function<void(QPainter &)> &draw) const;
    void blit(QPainter &p, const Sprite &sprite, const QPointF &anchor) const;
    // Draws a sprite resized by `scale`, for text that moves in depth.
    void blitScaled(QPainter &p, const Sprite &sprite, const QPointF &anchor, double scale) const;
    const Sprite &glyph(QChar c);
    void prepareGlyphs();
    void paintRadar(QPainter &p);

    const View *m_view = nullptr;
    double m_pixelsPerUnit = 0, m_preparedWidth = 0, m_preparedHeight = 0;
    QHash<QChar, Sprite> m_glyphs;
    double m_glyphAdvance = 16;
    Sprite m_title;
    QImage m_scanlines;   // One period of the scanlines, a pixel wide, tiled over the view.
};
