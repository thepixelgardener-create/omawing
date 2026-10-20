// A port of the Omawing SVG demo's renderer to QPainter. The SVG edition
// set the attributes of SVG elements each frame; this draws the same layers,
// in the same order, straight onto the window.
#include "scene.h"

#include <QPainter>
#include <algorithm>

#include "ground.h"
#include "sky.h"

using namespace geo;
using namespace draw;

QString Scene::actName(int act, const QString &themeName) {
    const int index = std::clamp(act, 0, Flight::Acts - 1);
    if (index == ThemeAct && !themeName.trimmed().isEmpty())
        return themeName.trimmed().toUpper();
    return View::actAt(index).name;
}

std::array<QColor, 8> Scene::themeColors(const QHash<QString, QString> &colors) {
    auto pick = [&](std::initializer_list<const char *> keys, const char *fallback) {
        for (const char *key : keys) {
            const QColor c(colors.value(key));
            if (c.isValid())
                return vecOf(c);
        }
        return hex(fallback);
    };
    const Vec background = pick({"background"}, "#1a1b26"), foreground = pick({"foreground"}, "#a9b1d6");
    const Vec accent = pick({"accent", "blue"}, "#7aa2f7"), magenta = pick({"magenta", "bright_magenta"}, "#ad8ee6");
    // Light themes swap background and foreground, but the sky stays dark
    // at the top so the HUD reads.
    auto luminance = [](const Vec &c) { return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; };
    const bool lightTheme = luminance(background) > luminance(foreground);
    const Vec dark = lightTheme ? foreground : background, light = lightTheme ? background : foreground;
    const Vec black{0, 0, 0};
    return {rgb(blend(dark, black, 0.3)), rgb(blend(dark, accent, 0.4)), rgb(blend(accent, light, 0.4)),
            rgb(blend(dark, accent, 0.28)), rgb(blend(accent, light, 0.2)), rgb(blend(dark, black, 0.15)),
            rgb(accent), rgb(magenta)};
}

int Scene::paint(QPainter &painter, const Flight &flight, const QSizeF &size, const Hud &hud, const Look &look) {
    m_view.begin(flight, size, look);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(size.width() / m_view.width, size.width() / m_view.width);
    painter.setClipRect(QRectF(0, 0, m_view.width, m_view.height));
    m_overlay.prepare(m_view, painter.worldTransform().m11());
    Sky::paint(painter, m_view);
    Ground::paint(painter, m_view);
    m_world.build(m_view);
    m_world.paint(painter);
    m_overlay.paint(painter, m_view, hud);
    painter.restore();
    return int(m_world.faces().size());
}
