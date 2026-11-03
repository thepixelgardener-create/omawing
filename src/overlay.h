#pragma once

// What sits on top of the world: speed lines, ring points, scanlines, the
// title, the HUD, and the sine scroller.

#include <QHash>
#include <QImage>
#include <QPointF>
#include <QRect>
#include <QStaticText>
#include <functional>

#include "view.h"

class QPainter;

class Overlay {
public:
    // Text and patterns never change shape, so they are drawn once per
    // window scale into images and reused every frame.
    void prepare(const View &view, double pixelsPerUnit);
    void paint(QPainter &p, const View &view, const SceneHud &hud);
    // How many strings have been laid out, rather than found laid out.
    long textLayouts() const { return m_textLayouts; }

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
    Sprite renderGlyph(QChar c) const;
    // Text laid out once and kept: shaping and measuring every string every
    // frame was a good share of the overlay's time. `anchor` places the text
    // at its start (0), centre (0.5), or end (1); y is its baseline.
    struct Text {
        QStaticText layout;
        double advance = 0, ascent = 0;
        quint64 used = 0;
    };
    const Text &text(const QFont &font, const QString &string);
    void drawText(QPainter &p, const QFont &font, const QPointF &at, const QString &string, double anchor = 0);
    const Sprite &glyph(QChar c);
    void prepareGlyphs();
    void paintStreaks(QPainter &p);
    void paintPopups(QPainter &p);
    void paintScanlines(QPainter &p);
    void paintTitles(QPainter &p, const SceneHud &hud);
    void paintScore(QPainter &p, const SceneHud &hud);
    void paintGauges(QPainter &p, const SceneHud &hud);
    void paintRadar(QPainter &p);
    void paintSwitches(QPainter &p, const SceneHud &hud);
    void paintAlert(QPainter &p, const SceneHud &hud);
    void paintScroller(QPainter &p);

    const View *m_view = nullptr;
    double m_pixelsPerUnit = 0, m_preparedWidth = 0, m_preparedHeight = 0;
    QHash<QChar, Sprite> m_glyphs;
    double m_glyphAdvance = 16;
    Sprite m_title;
    QImage m_scanlines;   // One period of the scanlines, a pixel wide, tiled over the view.
    QHash<QString, Text> m_texts;
    quint64 m_textUse = 0;
    long m_textLayouts = 0;
};
