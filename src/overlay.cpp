#include "overlay.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <cstring>
#include <string_view>
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
    // Looked up once: the HUD asks for a dozen fonts every frame.
    static const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    QFont font = fixed;
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
const PixelGlyph *pixelGlyph(char letter) {
    for (const PixelGlyph &g : pixelGlyphs())
        if (g.letter == letter)
            return &g;
    return nullptr;
}
// Logo pixels, in canvas units.
constexpr double TitlePixel = 2.5;

// Where the HUD sits on the canvas, in units. The bottom rows are measured
// from the top of a canvas BaseHeight tall, and move down with the view.
namespace Layout {
constexpr double Margin = 30;
constexpr double HullLabel = 389, HullBar = 398, ThrustLabel = 421, ThrustBar = 430, Altitude = 454, Status = 453;
constexpr double GaugeWidth = 94, GaugeHeight = 4, ThrustIdle = 43;
constexpr double ScrollerTop = 480, ScrollerHeight = 60, ScrollerBaseline = 519;
}

// Window scales are rounded to steps of about 2.5%, so dragging a window's
// edge doesn't redraw every sprite on every frame; within a step they show
// a hair larger or smaller than exact, which no one can see.
double quantizedScale(double pixelsPerUnit) {
    if (pixelsPerUnit <= 0)
        return pixelsPerUnit;
    return std::exp(std::round(std::log(pixelsPerUnit) * 40) / 40);
}
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
    const QString letters = QString::fromLatin1(ScrollText) + "+0123456789";
    std::vector<std::pair<QChar, Sprite>> parts;
    for (const QChar c : letters)
        if (c != ' ' && std::none_of(parts.begin(), parts.end(), [c](const auto &part) { return part.first == c; }))
            parts.emplace_back(c, renderGlyph(c));
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

Overlay::Sprite Overlay::renderGlyph(QChar c) const {
    QPainterPath path;
    const QFont font = monoFont(26, 0, QFont::Black);
    path.addText(-QFontMetricsF(font).horizontalAdvance(c) / 2, 0, font, QString(c));
    return render(path.boundingRect(), [&](QPainter &p) { p.fillPath(path, chrome(path.boundingRect())); });
}

const Overlay::Text &Overlay::text(const QFont &font, const QString &string) {
    const QString key = font.key() + '/' + string;
    auto it = m_texts.find(key);
    if (it != m_texts.end()) {
        it->used = ++m_textUse;
        return *it;
    }
    // Evict only the least recently used value. Labels used every frame stay
    // hot even while altitude and score produce new strings.
    if (m_texts.size() >= 512) {
        auto oldest = m_texts.begin();
        for (auto candidate = m_texts.begin(); candidate != m_texts.end(); ++candidate)
            if (candidate->used < oldest->used) oldest = candidate;
        m_texts.erase(oldest);
    }
    ++m_textLayouts;
    Text laid;
    laid.used = ++m_textUse;
    laid.layout = QStaticText(string);
    laid.layout.setTextFormat(Qt::PlainText);
    laid.layout.setPerformanceHint(QStaticText::AggressiveCaching);
    const QFontMetricsF metrics(font);
    laid.advance = metrics.horizontalAdvance(string);
    laid.ascent = metrics.ascent();
    return *m_texts.insert(key, laid);
}

void Overlay::drawText(QPainter &p, const QFont &font, const QPointF &at, const QString &string, double anchor) {
    const Text &laid = text(font, string);
    p.setFont(font);
    p.drawStaticText(QPointF(at.x() - laid.advance * anchor, at.y() - laid.ascent), laid.layout);
}

// Letters outside the atlas are drawn the first time they are needed.
const Overlay::Sprite &Overlay::glyph(QChar c) {
    auto it = m_glyphs.find(c);
    if (it == m_glyphs.end())
        it = m_glyphs.insert(c, renderGlyph(c));
    return *it;
}

void Overlay::prepare(const View &view, double pixelsPerUnit) {
    m_view = &view;
    pixelsPerUnit = quantizedScale(pixelsPerUnit);
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
    m_radarFace = m_radarRim = Sprite();
    // Each logo pixel is a whole number of screen pixels, so the blocks
    // stay crisp. A soft shadow one pixel down and right lifts the dark
    // bottom band off the night sky.
    const int unit = std::max(1, int(std::lround(TitlePixel * pixelsPerUnit)));
    static constexpr char Title[] = "omawing";
    int columns = 0;
    for (const char letter : std::string_view(Title))
        if (const PixelGlyph *glyph = pixelGlyph(letter))
            columns += int(std::strlen(glyph->rows[0])) + 2;
    QImage title((columns - 1) * unit, 17 * unit, QImage::Format_ARGB32_Premultiplied);
    title.fill(Qt::transparent);
    {
        QPainter t(&title);
        for (const bool shadow : {true, false}) {
            int x = 0;
            for (const char letter : std::string_view(Title)) {
                const PixelGlyph *glyph = pixelGlyph(letter);
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
    paintStreaks(p);
    paintPopups(p);
    paintScanlines(p);
    paintTitles(p, hud);
    paintScore(p, hud);
    paintGauges(p, hud);
    paintRadar(p);
    paintSwitches(p, hud);
    paintAlert(p, hud);
    if (m_view->look.banner)
        paintScroller(p);
}

// Speed lines streak outwards while boosting, and always in the dim acts;
// the rest of the time they are invisible and not drawn.
void Overlay::paintStreaks(QPainter &p) {
    static const QColor streak("#a3e5f4");
    const double time = m_view->flight->time, W = m_view->width;
    const double streaks = m_view->flight->boostAmount() * 0.35 + (m_view->style().dimSun ? 0.16 : 0);
    if (streaks <= 0.005)
        return;
    p.setOpacity(streaks);
    p.setPen(QPen(streak, 1.2));
    for (int i = 0; i < 18; ++i) {
        const double a = i / 18.0 * M_PI * 2 + 0.05 * time;
        const double length = 24 + hash(i, 4) * 70, r = 200 + std::fmod(time * 170 + i * 39, 340);
        const QPointF from(W / 2 + std::cos(a) * r, m_view->height / 2 + 10 + std::sin(a) * r * 0.6);
        p.drawLine(from, from + QPointF(std::cos(a) * length, std::sin(a) * length * 0.6));
    }
    p.setOpacity(1);
}

// Points float up from the rings they were won at, in the scroller's
// chrome letters, which are ready in the atlas; text at a new size each
// frame would make the GPU cache new glyphs.
void Overlay::paintPopups(QPainter &p) {
    for (const Popup &popup : m_view->flight->game.popups) {
        const Vec c = m_view->toCamera(popup.position);
        if (c[2] < 2)
            continue;
        const QPointF at = m_view->project(c);
        p.setOpacity(clampTo(1 - popup.age / Game::PopupTime, 0, 1));
        const QString text = "+" + QString::number(popup.points);
        const double scale = clampTo(View::Focal * 1.1 / c[2], 12, 34) / 26, advance = m_glyphAdvance * scale;
        for (int i = 0; i < text.size(); ++i)
            blitScaled(p, glyph(text[i]), {at.x() + (i - (text.size() - 1) / 2.0) * advance, at.y()}, scale);
    }
    p.setOpacity(1);
}

// Scanlines, like a CRT.
void Overlay::paintScanlines(QPainter &p) {
    p.save();
    const QRectF screen = p.worldTransform().mapRect(QRectF(0, 0, m_view->width, m_view->height));
    p.resetTransform();
    p.fillRect(screen, QBrush(m_scanlines));
    p.restore();
}

// The titles: the logo and its line on the left, the scene on the right.
// They fade out together when the interface is switched off.
void Overlay::paintTitles(QPainter &p, const SceneHud &hud) {
    static const QColor subtitle("#cbb6b1"), scene("#ffe2b3"), mode("#d3bdd5"), unlit("#65506f");
    if (m_view->look.titles <= 0.01)
        return;
    const double W = m_view->width;
    p.setOpacity(m_view->look.titles);
    blit(p, m_title, {28, 20});
    p.setPen(subtitle);
    drawText(p, hudFont(11, 2.2, hud.subtitle), {Layout::Margin, 20 + 17 * TitlePixel + 16}, hud.subtitle);
    p.setPen(scene);
    drawText(p, monoFont(13, 2), {W - Layout::Margin, 36},
             QString("%1 / %2").arg(m_view->act + 1, 2, 10, QChar('0')).arg(Scene::actName(m_view->act, m_view->look.themeName)), 1);
    p.setPen(mode);
    drawText(p, hudFont(10, 1.6, hud.mode), {W - Layout::Margin, 59}, hud.mode, 1);
    for (int i = 0; i < Flight::Acts; ++i)
        p.fillRect(QRectF(W - Layout::Margin - (Flight::Acts - i) * 15 + 3, 74, 12, 3),
                   i == m_view->act ? rgb(m_view->palette.accent) : unlit);
    p.setOpacity(1);
}

// The score, top centre: the current run while flying, the best always.
// It fades with the titles, so U leaves only the radar at the top.
void Overlay::paintScore(QPainter &p, const SceneHud &hud) {
    static const QColor label("#e2c5cf"), points("#ffe2b3"), best("#d3bdd5");
    if (m_view->look.titles <= 0.01)
        return;
    const double centre = m_view->width / 2;
    p.setOpacity(m_view->look.titles);
    if (!hud.points.isEmpty()) {
        p.setPen(label);
        drawText(p, hudFont(9, 3, hud.score), {centre, 26}, hud.score, 0.5);
        p.setPen(points);
        const QFont pointsFont = monoFont(24, 3, QFont::Black);
        drawText(p, pointsFont, {centre, 52}, hud.points, 0.5);
        if (m_view->flight->game.multiplier > 1) {
            p.setPen(rgb(m_view->palette.accent));
            drawText(p, monoFont(14, 1, QFont::Bold), {centre + text(pointsFont, hud.points).advance / 2 + 8, 50},
                     QString::fromUtf8("×") + QLocale().toString(m_view->flight->game.multiplier));
        }
    }
    if (!hud.best.isEmpty()) {
        p.setPen(best);
        drawText(p, hudFont(10, 1.6, hud.best), {centre, hud.points.isEmpty() ? 36.0 : 70.0}, hud.best, 0.5);
    }
    p.setOpacity(1);
}

// Bottom left: the hull while you fly (green, then amber, then red,
// blinking after a hit), the thrust, and the altitude; bottom right, the
// status line.
void Overlay::paintGauges(QPainter &p, const SceneHud &hud) {
    using namespace Layout;
    static const QColor label("#e2c5cf"), track("#2e2544"), thrust("#ffd19c"), status("#ffe0b0");
    static const QColor healthy("#9be8a5"), worn("#ffd166"), failing("#ff6b6b");
    const double b = m_view->height - View::BaseHeight, boost = m_view->flight->boostAmount();
    const Game &game = m_view->flight->game;
    if (!hud.hull.isEmpty()) {
        const double hull = game.hull / Game::FullHull;
        p.setPen(label);
        drawText(p, hudFont(9, 2, hud.hull), {Margin, b + HullLabel}, hud.hull);
        p.fillRect(QRectF(Margin, b + HullBar, GaugeWidth, GaugeHeight), track);
        if (game.hurt <= 0 || std::fmod(game.hurt, 0.2) >= 0.1)
            p.fillRect(QRectF(Margin, b + HullBar, GaugeWidth * hull, GaugeHeight),
                       hull > 0.5 ? healthy : hull > 0.25 ? worn : failing);
    }

    p.setPen(label);
    drawText(p, hudFont(9, 2, hud.thrust), {Margin, b + ThrustLabel}, hud.thrust);
    p.fillRect(QRectF(Margin, b + ThrustBar, GaugeWidth, GaugeHeight), track);
    p.fillRect(QRectF(Margin, b + ThrustBar, ThrustIdle + boost * (GaugeWidth - ThrustIdle), GaugeHeight), thrust);
    drawText(p, monoFont(10), {Margin, b + Altitude}, hud.altitude);
    p.setPen(status);
    drawText(p, hudFont(11, 1, hud.status), {m_view->width - Margin, b + Status}, hud.status, 1);
}

// A big chrome message across the middle when the hull gives out.
void Overlay::paintAlert(QPainter &p, const SceneHud &hud) {
    static const QColor shadow("#211638");
    if (hud.alert.isEmpty() || std::fmod(m_view->flight->time, 0.6) >= 0.42)
        return;
    QPainterPath text;
    const QFont font = hudFont(34, 5, hud.alert, QFont::Black);
    text.addText(0, 0, font, hud.alert);
    const QRectF box = text.boundingRect();
    text.translate(m_view->width / 2 - box.center().x(), m_view->height * 0.36 - box.center().y());
    p.setOpacity(0.85);
    p.fillPath(text.translated(2, 4), shadow);
    p.setOpacity(1);
    p.fillPath(text, chrome(text.boundingRect()));
}

// The sine scroller along the bottom.
void Overlay::paintScroller(QPainter &p) {
    using namespace Layout;
    static const QColor band("#101024"), edge = withAlpha("#e6a67c", 0.65);
    static const QString text = QString::fromLatin1(ScrollText);
    const double time = m_view->flight->time, W = m_view->width, b = m_view->height - View::BaseHeight;
    p.setOpacity(0.84);
    p.fillRect(QRectF(0, b + ScrollerTop, W, ScrollerHeight), band);
    p.setOpacity(1);
    p.setPen(QPen(edge, 1));
    p.drawLine(QPointF(0, b + ScrollerTop + 1), QPointF(W, b + ScrollerTop + 1));
    p.save();
    p.setClipRect(QRectF(0, b + ScrollerTop + 2, W, ScrollerHeight - 2), Qt::IntersectClip);
    const double scroll = time * 8;
    const int offset = int(std::floor(scroll)), letters = int(std::ceil(W / 20)) + 3;
    for (int i = 0; i < letters; ++i) {
        const double x = (i - (scroll - offset)) * 20 - 20;
        const QChar c = text[(i + offset) % text.size()];
        if (c != ' ')
            blit(p, glyph(c), {x, b + ScrollerBaseline + std::sin(x * 0.012 + time * 2.4) * 7});
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
    static const QColor ballColor("#db4c70"), shipColor("#f4f0ff");
    // The face (its backing, rings, and cross) and the rim only change with
    // the act's accent, so they're drawn into images and reused.
    const QRgb accent = rgb(v.palette.accent).rgb();
    if (m_radarFace.image.isNull() || m_radarAccent != accent) {
        m_radarAccent = accent;
        const QRectF box(-Radius - 1, -Radius - 1, 2 * Radius + 2, 2 * Radius + 2);
        m_radarFace = render(box, [&](QPainter &f) {
            static const QColor backing = withAlpha("#0b0b1c", 0.62);
            f.setPen(Qt::NoPen);
            f.setBrush(backing);
            f.drawEllipse(QPointF(0, 0), Radius, Radius);
            f.setBrush(Qt::NoBrush);
            f.setPen(QPen(withAlpha(QColor(accent), 0.18), 0.8));
            f.drawEllipse(QPointF(0, 0), Radius / 3, Radius / 3);
            f.drawEllipse(QPointF(0, 0), Radius * 2 / 3, Radius * 2 / 3);
            f.drawLine(QPointF(-Radius, 0), QPointF(Radius, 0));
            f.drawLine(QPointF(0, -Radius), QPointF(0, Radius));
        });
        m_radarRim = render(box, [&](QPainter &f) {
            f.setPen(QPen(QColor(accent), 1.2));
            f.drawEllipse(QPointF(0, 0), Radius, Radius);
        });
    }
    blit(p, m_radarFace, centre);

    // The scan: a ring every Period seconds, growing from the ship to the
    // rim and fading as it goes, with a soft glow just inside it. The glow
    // fades through the painter's opacity, so its gradient stays the same
    // and the GPU keeps it.
    constexpr double Period = 1.8;
    const double phase = std::fmod(time, Period) / Period;
    {
        const double r = phase * Radius, fade = 1 - phase;
        QRadialGradient glow(centre, std::max(1.0, r));
        glow.setColorAt(0, withAlpha(rgb(v.palette.accent), 0));
        glow.setColorAt(0.75, withAlpha(rgb(v.palette.accent), 0));
        glow.setColorAt(1, withAlpha(rgb(v.palette.accent), 0.22));
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.setOpacity(fade);
        p.drawEllipse(centre, r, r);
        p.setOpacity(1);
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
    World::forEachScenery(ship.x, ship.z, 4, time, [&](const Obstacle &o) {
        const QPointF at = place(o.x, o.z);
        if (!inside(at, 3))
            return;
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
    });
    for (const Obstacle &ball : World::balls(ship.z, Range, time)) {
        const QPointF ballAt = place(ball.x, ball.z);
        if (inside(ballAt, 3)) {
            p.setBrush(withAlpha(ballColor, lit(ballAt)));
            p.drawEllipse(ballAt, 2.4, 2.4);
        }
    }

    // Rings stand across the course; from above they are bars. The nearest
    // one still ahead of the ship pulses.
    const auto [first, last] = World::ringsBetween(ship.z - Range, ship.z + Range);
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
    p.setBrush(shipColor);
    const QPointF chevron[] = {centre + QPointF(0, -4.5), centre + QPointF(3.4, 3.5), centre + QPointF(0, 1.8),
                               centre + QPointF(-3.4, 3.5)};
    p.drawPolygon(chevron, 4);

    p.setOpacity(0.45 + 0.2 * v.look.beat);
    blit(p, m_radarRim, centre);
    p.restore();
}

// The three view switches as keycaps, bottom right above the status line:
// U for the titles, L for the toolbar, B for the banner. A lit key is on, a
// dim hollow one off. They fade in for a moment at startup, when one is
// pressed, and when the pointer moves, so the way back is never a mystery.
void Overlay::paintSwitches(QPainter &p, const SceneHud &hud) {
    const SceneLook &look = m_view->look;
    if (look.switches <= 0.01 || hud.titlesSwitch.isEmpty())
        return;
    struct Switch {
        QString key, label;
        bool on;
    };
    const Switch switches[] = {{"U", hud.titlesSwitch, look.titlesOn},
                               {"L", hud.toolbarSwitch, look.toolbarOn},
                               {"B", hud.bannerSwitch, look.banner}};
    const double cap = 15, gap = 18, y = m_view->height - View::BaseHeight + 418;
    const QFont keyFont = monoFont(10, 0, QFont::Bold);
    const QColor accent = rgb(blend(m_view->palette.accent, {255, 255, 255}, 0.2)), dim("#8c7b98");
    double x = m_view->width - 30;
    p.save();
    p.setOpacity(look.switches);
    // A soft dark backing keeps them readable over any scenery.
    double width = 0;
    for (const Switch &s : switches)
        width += text(hudFont(9, 1.8, s.label), s.label).advance + 6 + cap + gap;
    width -= gap;
    p.setPen(Qt::NoPen);
    p.setBrush(withAlpha("#0b0b1c", 0.6));
    p.drawRoundedRect(QRectF(x - width - 8, y - 5, width + 16, cap + 10), 6, 6);
    // Laid out from the right edge leftwards.
    for (int i = 2; i >= 0; --i) {
        const Switch &s = switches[i];
        const QFont labelFont = hudFont(9, 1.8, s.label);
        const double labelWidth = text(labelFont, s.label).advance;
        p.setPen(s.on ? QColor("#ffe2b3") : dim);
        drawText(p, labelFont, {x, y + cap / 2 + 3.5}, s.label, 1);
        x -= labelWidth + 6 + cap;
        const QRectF key(x, y, cap, cap);
        if (s.on) {
            p.setPen(Qt::NoPen);
            p.setBrush(accent);
            p.drawRoundedRect(key, 3, 3);
            p.setPen(QColor("#14122a"));
        } else {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(dim, 1));
            p.drawRoundedRect(key.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
            p.setPen(dim);
        }
        p.setFont(keyFont);
        p.drawText(key, Qt::AlignCenter, s.key);
        x -= gap;
    }
    p.restore();
}

