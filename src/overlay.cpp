#include "overlay.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <cstring>
#include <QLocale>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <algorithm>
#include <cmath>

#include "scene.h"

using namespace geo;
using namespace draw;

namespace {
const char *const ScrollText =
    "   *** OMAWING ***   GREETINGS TO THE DREAMERS, CODERS & PIXEL PUSHERS   //"
    "   OMARCHY: BEAUTIFUL, MODERN & OPINIONATED   //   OMAKASE ARCH, SERVED ON HYPRLAND   //"
    "   SUPER + RETURN SUMMONS A TERMINAL   //   SUPER + SPACE OPENS THE OMARCHY MENU   //"
    "   KEYBOARD FIRST, MOUSE OPTIONAL   //   ONE THEME SWITCH AND THE WHOLE DESKTOP FOLLOWS   //"
    "   TILES ALL THE WAY DOWN   //   NO DISKS REQUIRED   //   ALL VECTORS, ALL VIBES   //"
    "   FLY THROUGH THE RINGS AND CHASE THE HIGH SCORE!   ";

QFont monoFont(double pixelSize, double spacing = 0, int weight = QFont::Normal) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(std::max(1, int(std::lround(pixelSize))));
    font.setLetterSpacing(QFont::AbsoluteSpacing, spacing);
    font.setWeight(QFont::Weight(weight));
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
}

// The HUD spaces its letters out, which only suits scripts whose letters
// stand alone; joined scripts like Arabic, and the Indic scripts, keep
// their natural spacing.
QFont hudFont(double pixelSize, double spacing, const QString &text, int weight = QFont::Normal) {
    for (const QChar c : text) {
        const QChar::Script script = c.script();
        if (script != QChar::Script_Latin && script != QChar::Script_Greek && script != QChar::Script_Cyrillic &&
            script != QChar::Script_Common && script != QChar::Script_Inherited)
            return monoFont(pixelSize, 0, weight);
    }
    return monoFont(pixelSize, spacing, weight);
}

// Draws text with its end at x, like SVG's text-anchor="end".
void drawEnd(QPainter &p, double x, double y, const QString &text) {
    p.drawText(QPointF(x - QFontMetricsF(p.font()).horizontalAdvance(text), y), text);
}
void drawCentred(QPainter &p, double x, double y, const QString &text) {
    p.drawText(QPointF(x - QFontMetricsF(p.font()).horizontalAdvance(text) / 2, y), text);
}

// The demo's chrome: a sunset ramp with a hard horizon line through the middle.
QLinearGradient chrome(const QRectF &box) {
    QLinearGradient g(0, box.top(), 0, box.bottom());
    g.setColorAt(0, QColor("#fbf0b3"));
    g.setColorAt(0.44, QColor("#fff7d9"));
    g.setColorAt(0.49, QColor("#edaa84"));
    g.setColorAt(0.5, QColor("#a53b7d"));
    g.setColorAt(0.73, QColor("#f16c96"));
    g.setColorAt(1, QColor("#f6cb9d"));
    return g;
}
// The title in the pixel lettering of the Omarchy logo: 16 pixels tall,
// strokes 3 wide, notched corners, shaded in five bands from light at the
// top to dark at the bottom. o, m, and a are the logo's own letters; w, i,
// n, and g are drawn to match.
struct PixelGlyph {
    char letter;
    std::array<const char *, 16> rows;
};
const std::array<PixelGlyph, 7> &pixelGlyphs() {
    static const std::array<PixelGlyph, 7> glyphs{{
        {'o', {"..#####..", ".#######.", "###...###", "###...###", "###...###", "###...###", "###...###", "###...###",
               "###...###", "###...###", "###...###", "###...###", "###...###", "###...###", ".#######.", "..#####.#"}},
        {'m', {"..###########..", ".##############", "###...###...###", "###...###...###", "###...###...###",
               "###...###...###", "###...###...###", "###...###...###", "###...###...###", "###...###...###",
               "###...###...###", "###...###...###", "###...###...###", "###...###...###", ".##...###...###",
               "..#...###...#.."}},
        {'a', {"...########", "..#########", ".####...###", ".####...###", ".####...###", ".####...###", ".####...###",
               "###########", "###########", ".####...###", ".####...###", ".####...###", ".####...###", "..###...###",
               "..###...##.", "..###...#.."}},
        {'w', {"..#...###...#..", ".##...###...###", "###...###...###", "###...###...###", "###...###...###",
               "###...###...###", "###...###...###", "###...###...###", "###...###...###", "###...###...###",
               "###...###...###", "###...###...###", "###...###...###", "###...###...###", ".##############",
               "..###########.."}},
        {'i', {"###", "###", "...", "###", "###", "###", "###", "###", "###", "###", "###", "###", "###", "###", "##.",
               "#.."}},
        {'n', {"..#####..", ".########", "###...###", "###...###", "###...###", "###...###", "###...###", "###...###",
               "###...###", "###...###", "###...###", "###...###", "###...###", "###...###", ".##...###", "..#...#.."}},
        {'g', {"..#####..", ".#######.", "###...###", "###...###", "###...###", "###...###", "###...###", "#########",
               "#########", "......###", ".##...###", "###...###", "###...###", "###...###", ".#######.", "#.#####.."}},
    }};
    return glyphs;
}
// The band each row is shaded with, from the logo.
QColor pixelBand(int row) {
    static const QColor bands[] = {QColor("#e3d7d5"), QColor("#cbb6b1"), QColor("#b59790"), QColor("#72605c"), QColor("#3a322f")};
    return bands[row < 4 ? 0 : row < 6 ? 1 : row < 10 ? 2 : row < 13 ? 3 : 4];
}
// Logo pixels, in canvas units.
constexpr double TitlePixel = 2.5;
} // namespace

Overlay::Sprite Overlay::render(const QRectF &bounds, const std::function<void(QPainter &)> &draw) const {
    const QRectF box = bounds.adjusted(-1, -1, 1, 1);
    QImage image(QSize(std::max(1, int(std::ceil(box.width() * m_pixelsPerUnit))),
                       std::max(1, int(std::ceil(box.height() * m_pixelsPerUnit)))),
                 QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(m_pixelsPerUnit, m_pixelsPerUnit);
    p.translate(-box.topLeft());
    draw(p);
    return {image, box.topLeft()};
}

// Draws a sprite pixel for pixel, without scaling it again.
void Overlay::blit(QPainter &p, const Sprite &sprite, const QPointF &anchor) const {
    const QPointF at = p.worldTransform().map(anchor + sprite.offset);
    p.save();
    p.resetTransform();
    p.drawImage(QPointF(std::round(at.x()), std::round(at.y())), sprite.image, sprite.source);
    p.restore();
}

void Overlay::blitScaled(QPainter &p, const Sprite &sprite, const QPointF &anchor, double scale) const {
    const QSizeF size = (sprite.source.isEmpty() ? sprite.image.size() : sprite.source.size()).toSizeF();
    p.drawImage(QRectF(anchor + sprite.offset * scale, size * (scale / m_pixelsPerUnit)), sprite.image,
                sprite.source.isEmpty() ? QRectF(sprite.image.rect()) : QRectF(sprite.source));
}

// Every letter the scroller and the ring points can show, drawn up front into
// one atlas. Drawing each letter the first time it scrolled in, and uploading
// it to the GPU, made a small hitch every time a new one appeared.
void Overlay::prepareGlyphs() {
    m_glyphs.clear();
    QString letters = QString::fromLatin1(ScrollText) + "+0123456789";
    std::vector<std::pair<QChar, Sprite>> parts;
    for (const QChar c : letters)
        if (c != ' ' && std::none_of(parts.begin(), parts.end(), [c](const auto &part) { return part.first == c; }))
            parts.emplace_back(c, glyph(c));
    m_glyphs.clear();
    m_glyphAdvance = QFontMetricsF(monoFont(26, 0, QFont::Black)).horizontalAdvance('0');
    const int rowWidth = 1024;
    int x = 0, y = 0, rowHeight = 0, width = 1;
    std::vector<QPoint> places;
    for (const auto &[c, sprite] : parts) {
        if (x + sprite.image.width() > rowWidth) {
            x = 0;
            y += rowHeight + 1;
            rowHeight = 0;
        }
        places.emplace_back(x, y);
        x += sprite.image.width() + 1;
        width = std::max(width, x);
        rowHeight = std::max(rowHeight, sprite.image.height());
    }
    QImage atlas(width, std::max(1, y + rowHeight), QImage::Format_ARGB32_Premultiplied);
    atlas.fill(Qt::transparent);
    {
        QPainter p(&atlas);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        for (size_t i = 0; i < parts.size(); ++i)
            p.drawImage(places[i], parts[i].second.image);
    }
    for (size_t i = 0; i < parts.size(); ++i)
        m_glyphs.insert(parts[i].first, {atlas, parts[i].second.offset, QRect(places[i], parts[i].second.image.size())});
}

const Overlay::Sprite &Overlay::glyph(QChar c) {
    auto it = m_glyphs.find(c);
    if (it == m_glyphs.end()) {
        QPainterPath path;
        const QFont font = monoFont(26, 0, QFont::Black);
        path.addText(-QFontMetricsF(font).horizontalAdvance(c) / 2, 0, font, QString(c));
        it = m_glyphs.insert(c, render(path.boundingRect(), [&](QPainter &p) { p.fillPath(path, chrome(path.boundingRect())); }));
    }
    return *it;
}

void Overlay::prepare(const View &view, double pixelsPerUnit) {
    m_view = &view;
    if (std::abs(pixelsPerUnit - m_pixelsPerUnit) < 1e-6 && std::abs(m_preparedWidth - m_view->width) < 1e-6 &&
        std::abs(m_preparedHeight - m_view->height) < 1e-6)
        return;
    const bool rescaled = std::abs(pixelsPerUnit - m_pixelsPerUnit) >= 1e-6;
    m_pixelsPerUnit = pixelsPerUnit;
    m_preparedWidth = m_view->width;
    m_preparedHeight = m_view->height;
    if (!rescaled)
        return;
    // A dark line one unit tall every four units. Tiling a tiny pattern reads
    // far less memory each frame than a window-sized image, and nothing needs
    // redrawing when the window is resized.
    const int period = std::max(2, int(std::lround(4 * pixelsPerUnit)));
    m_scanlines = QImage(1, period, QImage::Format_ARGB32_Premultiplied);
    m_scanlines.fill(Qt::transparent);
    for (int y = period - std::max(1, int(std::lround(pixelsPerUnit))); y < period; ++y)
        m_scanlines.setPixelColor(0, y, withAlpha("#090f29", 0.14));
    prepareGlyphs();
    // Each logo pixel is a whole number of screen pixels, so the blocks
    // stay crisp. A soft shadow one pixel down and right lifts the dark
    // bottom band off the night sky.
    const int unit = std::max(1, int(std::lround(TitlePixel * pixelsPerUnit)));
    int columns = 0;
    for (const char letter : QByteArrayLiteral("omawing")) {
        for (const PixelGlyph &g : pixelGlyphs())
            if (g.letter == letter)
                columns += int(std::strlen(g.rows[0])) + 2;
    }
    QImage title((columns - 1) * unit, 17 * unit, QImage::Format_ARGB32_Premultiplied);
    title.fill(Qt::transparent);
    {
        QPainter t(&title);
        for (const bool shadow : {true, false}) {
            int x = 0;
            for (const char letter : QByteArrayLiteral("omawing")) {
                const PixelGlyph *glyph = nullptr;
                for (const PixelGlyph &g : pixelGlyphs())
                    if (g.letter == letter)
                        glyph = &g;
                if (!glyph)
                    continue;
                const int width = int(std::strlen(glyph->rows[0]));
                for (int row = 0; row < 16; ++row)
                    for (int column = 0; column < width; ++column)
                        if (glyph->rows[row][column] == '#')
                            t.fillRect((x + column + shadow) * unit, (row + shadow) * unit, unit, unit,
                                       shadow ? withAlpha("#07060d", 0.55) : pixelBand(row));
                x += width + 2;
            }
        }
    }
    m_title = {title, {0, 0}, {}};
}

void Overlay::paint(QPainter &p, const View &view, const SceneHud &hud) {
    m_view = &view;
    const double time = m_view->flight->time, W = m_view->width, boost = m_view->flight->boostAmount();
    // The gauges, status, and scroller stay pinned to the bottom.
    const double b = m_view->height - View::BaseHeight;
    const QLocale locale;

    // Speed lines streak outwards while boosting, and always in the dim
    // acts; the rest of the time they are invisible and not drawn.
    const double streaks = boost * 0.35 + (m_view->style().dimSun ? 0.16 : 0);
    if (streaks > 0.005) {
        p.setOpacity(streaks);
        p.setPen(QPen(QColor("#a3e5f4"), 1.2));
        for (int i = 0; i < 18; ++i) {
            const double a = i / 18.0 * M_PI * 2 + 0.05 * time;
            const double length = 24 + hash(i, 4) * 70, r = 200 + std::fmod(time * 170 + i * 39, 340);
            const QPointF from(W / 2 + std::cos(a) * r, m_view->height / 2 + 10 + std::sin(a) * r * 0.6);
            p.drawLine(from, from + QPointF(std::cos(a) * length, std::sin(a) * length * 0.6));
        }
        p.setOpacity(1);
    }

    // Points float up from the rings they were won at.
    for (const Popup &popup : m_view->flight->game.popups) {
        const Vec c = m_view->toCamera(popup.position);
        if (c[2] < 2)
            continue;
        const QPointF at = m_view->project(c);
        p.setOpacity(clampTo(1 - popup.age / 1.4, 0, 1));
        // In the scroller's chrome letters, which are ready in the atlas;
        // text at a new size each frame would make the GPU cache new glyphs.
        const QString text = "+" + QString::number(popup.points);
        const double scale = clampTo(View::Focal * 1.1 / c[2], 12, 34) / 26, advance = m_glyphAdvance * scale;
        for (int i = 0; i < text.size(); ++i)
            blitScaled(p, glyph(text[i]), {at.x() + (i - (text.size() - 1) / 2.0) * advance, at.y()}, scale);
    }
    p.setOpacity(1);

    // Scanlines, like a CRT.
    p.save();
    const QRectF screen = p.worldTransform().mapRect(QRectF(0, 0, W, m_view->height));
    p.resetTransform();
    p.fillRect(screen, QBrush(m_scanlines));
    p.restore();
    // The titles: the logo and its line on the left, the scene on the right.
    // They fade out together when the interface is switched off.
    if (m_view->look.titles > 0.01) {
        p.setOpacity(m_view->look.titles);
        blit(p, m_title, {28, 20});
        p.setPen(QColor("#cbb6b1"));
        p.setFont(hudFont(11, 2.2, hud.subtitle));
        p.drawText(QPointF(30, 20 + 17 * TitlePixel + 16), hud.subtitle);
        p.setPen(QColor("#ffe2b3"));
        p.setFont(monoFont(13, 2));
        drawEnd(p, W - 30, 36, QString("%1 / %2").arg(m_view->act + 1, 2, 10, QChar('0')).arg(Scene::actName(m_view->act, m_view->look.themeName)));
        p.setPen(QColor("#d3bdd5"));
        p.setFont(hudFont(10, 1.6, hud.mode));
        drawEnd(p, W - 30, 59, hud.mode);
        for (int i = 0; i < Flight::Acts; ++i)
            p.fillRect(QRectF(W - 30 - (Flight::Acts - i) * 15 + 3, 74, 12, 3), i == m_view->act ? rgb(m_view->palette.accent) : QColor("#65506f"));
        p.setOpacity(1);
    }

    // The score, top centre: the current run while flying, the best always.
    // It fades with the titles, so U leaves only the radar at the top.
    p.setOpacity(m_view->look.titles);
    if (m_view->look.titles > 0.01 && !hud.points.isEmpty()) {
        p.setPen(QColor("#e2c5cf"));
        p.setFont(hudFont(9, 3, hud.score));
        drawCentred(p, W / 2, 26, hud.score);
        p.setPen(QColor("#ffe2b3"));
        p.setFont(monoFont(24, 3, QFont::Black));
        drawCentred(p, W / 2, 52, hud.points);
        if (m_view->flight->game.multiplier > 1) {
            p.setPen(rgb(m_view->palette.accent));
            p.setFont(monoFont(14, 1, QFont::Bold));
            p.drawText(QPointF(W / 2 + QFontMetricsF(monoFont(24, 3, QFont::Black)).horizontalAdvance(hud.points) / 2 + 8, 50),
                       QString::fromUtf8("×") + locale.toString(m_view->flight->game.multiplier));
        }
    }
    if (m_view->look.titles > 0.01 && !hud.best.isEmpty()) {
        p.setPen(QColor("#d3bdd5"));
        p.setFont(hudFont(10, 1.6, hud.best));
        drawCentred(p, W / 2, hud.points.isEmpty() ? 36 : 70, hud.best);
    }
    p.setOpacity(1);

    // The hull, while you fly: green, then amber, then red, blinking after a hit.
    if (!hud.hull.isEmpty()) {
        const double hull = m_view->flight->game.hull / Game::FullHull;
        const QColor color = hull > 0.5 ? QColor("#9be8a5") : hull > 0.25 ? QColor("#ffd166") : QColor("#ff6b6b");
        p.setPen(QColor("#e2c5cf"));
        p.setFont(hudFont(9, 2, hud.hull));
        p.drawText(QPointF(30, b + 389), hud.hull);
        p.fillRect(QRectF(30, b + 398, 94, 4), QColor("#2e2544"));
        if (m_view->flight->game.hurt <= 0 || std::fmod(m_view->flight->game.hurt, 0.2) >= 0.1)
            p.fillRect(QRectF(30, b + 398, 94 * hull, 4), color);
    }

    p.setPen(QColor("#e2c5cf"));
    p.setFont(hudFont(9, 2, hud.thrust));
    p.drawText(QPointF(30, b + 421), hud.thrust);
    p.fillRect(QRectF(30, b + 430, 94, 4), QColor("#2e2544"));
    p.fillRect(QRectF(30, b + 430, 43 + boost * 51, 4), QColor("#ffd19c"));
    p.setFont(monoFont(10));
    p.drawText(QPointF(30, b + 454), hud.altitude);
    p.setPen(QColor("#ffe0b0"));
    p.setFont(hudFont(11, 1, hud.status));
    drawEnd(p, W - 30, b + 453, hud.status);

    paintRadar(p);

    // A big chrome message across the middle when the hull gives out.
    if (!hud.alert.isEmpty() && std::fmod(time, 0.6) < 0.42) {
        QPainterPath text;
        const QFont font = hudFont(34, 5, hud.alert, QFont::Black);
        text.addText(0, 0, font, hud.alert);
        const QRectF box = text.boundingRect();
        text.translate(W / 2 - box.center().x(), m_view->height * 0.36 - box.center().y());
        p.setOpacity(0.85);
        p.fillPath(text.translated(2, 4), QColor("#211638"));
        p.setOpacity(1);
        p.fillPath(text, chrome(text.boundingRect()));
    }

    if (!m_view->look.banner)
        return;
    // The sine scroller along the bottom.
    p.setOpacity(0.84);
    p.fillRect(QRectF(0, b + 480, W, 60), QColor("#101024"));
    p.setOpacity(1);
    p.setPen(QPen(withAlpha("#e6a67c", 0.65), 1));
    p.drawLine(QPointF(0, b + 481), QPointF(W, b + 481));
    p.save();
    p.setClipRect(QRectF(0, b + 482, W, 58), Qt::IntersectClip);
    static const QString text = QString::fromLatin1(ScrollText);
    const double scroll = time * 8;
    const int offset = int(std::floor(scroll)), letters = int(std::ceil(W / 20)) + 3;
    for (int i = 0; i < letters; ++i) {
        const double x = (i - (scroll - offset)) * 20 - 20;
        const QChar c = text[(i + offset) % text.size()];
        if (c != ' ')
            blit(p, glyph(c), {x, b + 519 + std::sin(x * 0.012 + time * 2.4) * 7});
    }
    p.restore();
}

// A round radar on the right, under the scene name: the ship in the middle,
// heading up, and what stands within Range around it. Pyramids are
// triangles, crystals diamonds, the ball a dot, and rings short bars, seen
// from above; the next ring ahead pulses. Scans ripple out from the ship in
// rings, lighting each blip as they pass, and the rim brightens on the beat.
void Overlay::paintRadar(QPainter &p) {
    constexpr double Radius = 56, Range = 60;
    const View &v = *m_view;
    const Flight &flight = *v.flight;
    const Ship &ship = flight.ship;
    const QPointF centre(v.width - 30 - Radius, 92 + Radius);
    const double c = std::cos(ship.yaw), s = std::sin(ship.yaw), time = flight.time;
    // World to radar: relative to the ship, turned so its heading points up.
    auto place = [&](double x, double z) {
        const double dx = x - ship.x, dz = z - ship.z;
        const double right = dx * c - dz * s, ahead = dx * s + dz * c;
        return QPointF(centre.x() + right / Range * Radius, centre.y() - ahead / Range * Radius);
    };
    auto inside = [&](const QPointF &at, double margin) { return QLineF(centre, at).length() <= Radius - margin; };

    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(withAlpha("#0b0b1c", 0.62));
    p.drawEllipse(centre, Radius, Radius);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(withAlpha(rgb(v.palette.accent), 0.18), 0.8));
    p.drawEllipse(centre, Radius / 3, Radius / 3);
    p.drawEllipse(centre, Radius * 2 / 3, Radius * 2 / 3);
    p.drawLine(centre - QPointF(Radius, 0), centre + QPointF(Radius, 0));
    p.drawLine(centre - QPointF(0, Radius), centre + QPointF(0, Radius));

    // The scan: a ring every Period seconds, growing from the ship to the
    // rim and fading as it goes, with a soft glow just inside it.
    constexpr double Period = 1.8;
    const double phase = std::fmod(time, Period) / Period;
    {
        const double r = phase * Radius, fade = 1 - phase;
        QRadialGradient glow(centre, std::max(1.0, r));
        glow.setColorAt(0, withAlpha(rgb(v.palette.accent), 0));
        glow.setColorAt(0.75, withAlpha(rgb(v.palette.accent), 0));
        glow.setColorAt(1, withAlpha(rgb(v.palette.accent), 0.22 * fade));
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(centre, r, r);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(withAlpha(rgb(blend(v.palette.accent, {255, 255, 255}, 0.3)), 0.75 * fade), 1.4));
        p.drawEllipse(centre, r, r);
    }
    // A blip flares as the scan passes it and dims until the next one.
    auto lit = [&](const QPointF &at) {
        const double since = std::fmod(phase - QLineF(centre, at).length() / Radius + 1, 1.0);
        return 0.45 + 0.55 * std::exp(-since * 5);
    };
    p.setPen(Qt::NoPen);

    const QColor pyramid = rgb(blend(v.palette.mid, {255, 255, 255}, 0.35));
    const QColor crystal = rgb(blend(v.palette.crystal, {255, 255, 255}, 0.25 + 0.2 * v.look.beat));
    for (const Obstacle &o : World::scenery(ship.x, ship.z, 4, time)) {
        const QPointF at = place(o.x, o.z);
        if (!inside(at, 3))
            continue;
        p.setBrush(withAlpha(o.kind == Obstacle::Pyramid ? pyramid : crystal, lit(at)));
        if (o.kind == Obstacle::Pyramid) {
            const double r = World::PyramidHalf / Range * Radius;
            const QPointF triangle[] = {at + QPointF(0, -r), at + QPointF(r, r * 0.8), at + QPointF(-r, r * 0.8)};
            p.drawConvexPolygon(triangle, 3);
        } else {
            const double r = 2.3;
            const QPointF diamond[] = {at + QPointF(0, -r), at + QPointF(r, 0), at + QPointF(0, r), at + QPointF(-r, 0)};
            p.drawConvexPolygon(diamond, 4);
        }
    }
    const Obstacle ball = World::ball(ship.z, time);
    const QPointF ballAt = place(ball.x, ball.z);
    if (inside(ballAt, 3)) {
        p.setBrush(withAlpha(QColor("#db4c70"), lit(ballAt)));
        p.drawEllipse(ballAt, 2.4, 2.4);
    }

    // Rings stand across the course; from above they are bars. The nearest
    // one still ahead of the ship pulses.
    const int first = int(std::floor((ship.z - Range - 24) / World::RingSpacing));
    const int last = int(std::ceil((ship.z + Range - 24) / World::RingSpacing));
    int next = 0;
    double nearest = 1e9;
    for (int i = first; i <= last; ++i) {
        const Ring r = World::ring(i);
        const double ahead = (r.x - ship.x) * s + (r.z - ship.z) * c;
        if (!flight.game.collected.count(i) && ahead > 0 && ahead < nearest) {
            nearest = ahead;
            next = i;
        }
    }
    for (int i = first; i <= last; ++i) {
        const Ring r = World::ring(i);
        if (flight.game.collected.count(i))
            continue;
        const QPointF a = place(r.x - World::RingRadius, r.z), b = place(r.x + World::RingRadius, r.z);
        if (!inside((a + b) / 2, 4))
            continue;
        const bool highlight = i == next && nearest < 1e9;
        const double glow = highlight ? 0.85 + 0.15 * std::sin(time * 8) : 0.8 * lit((a + b) / 2);
        p.setPen(QPen(withAlpha(rgb(highlight ? blend(v.palette.crystal, {255, 255, 255}, 0.2) : v.palette.accent), glow),
                      highlight ? 3.6 : 2.2, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(a, b);
    }

    // The ship, always pointing up.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#f4f0ff"));
    const QPointF chevron[] = {centre + QPointF(0, -4.5), centre + QPointF(3.4, 3.5), centre + QPointF(0, 1.8),
                               centre + QPointF(-3.4, 3.5)};
    p.drawPolygon(chevron, 4);

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(withAlpha(rgb(v.palette.accent), 0.45 + 0.2 * v.look.beat), 1.2));
    p.drawEllipse(centre, Radius, Radius);
    p.restore();
}

