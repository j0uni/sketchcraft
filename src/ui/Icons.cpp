#include "ui/Icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>

#include <array>
#include <cmath>
#include <numbers>

namespace ui {

namespace {

// Drawing happens on a 32 x 32 grid.
const QColor kInk(40, 40, 40);
const QColor kRed(214, 48, 39), kGreen(46, 150, 60), kBlue(40, 90, 200), kYellow(242, 196, 48);

// A small box seen from the front-right, used by the view and style icons.
void drawBox(QPainter& g, const QColor& top, const QColor& left, const QColor& right, bool edges = true,
             bool dashedBack = false, bool hideFaces = false) {
    const QPointF a(6, 11), b(16, 6), c(26, 11), d(16, 16);  // top face
    const QPointF e(6, 23), f(16, 28), h(26, 23);           // bottom corners
    QPen pen(kInk, 1.4);
    pen.setJoinStyle(Qt::RoundJoin);
    if (!hideFaces) {
        g.setPen(Qt::NoPen);
        g.setBrush(top);
        g.drawPolygon(QPolygonF({a, b, c, d}));
        g.setBrush(left);
        g.drawPolygon(QPolygonF({a, d, f, e}));
        g.setBrush(right);
        g.drawPolygon(QPolygonF({d, c, h, f}));
    }
    if (dashedBack) {
        QPen dp(kInk, 1.0, Qt::DashLine);
        g.setPen(dp);
        const QPointF back(16, 18);
        g.drawLine(b, back);
        g.drawLine(back, e);
        g.drawLine(back, h);
    }
    if (edges) {
        g.setPen(pen);
        g.setBrush(Qt::NoBrush);
        g.drawPolygon(QPolygonF({a, b, c, d}));
        g.drawLine(a, e);
        g.drawLine(d, f);
        g.drawLine(c, h);
        g.drawLine(e, f);
        g.drawLine(f, h);
    }
}

void arrowHead(QPainter& g, QPointF tip, double angle, double size, const QColor& c) {
    const double s = size;
    QPolygonF tri({tip, tip + QPointF(-s * std::cos(angle - 0.45), -s * std::sin(angle - 0.45)),
                   tip + QPointF(-s * std::cos(angle + 0.45), -s * std::sin(angle + 0.45))});
    g.setPen(Qt::NoPen);
    g.setBrush(c);
    g.drawPolygon(tri);
}

void pencil(QPainter& g, QPointF tip, double len) {
    // A yellow pencil lying from the tip up and to the right.
    g.save();
    g.translate(tip);
    g.rotate(-45);
    g.setPen(QPen(kInk, 1.0));
    g.setBrush(QColor(250, 214, 120));
    g.drawPolygon(QPolygonF({QPointF(0, 0), QPointF(5, -2.6), QPointF(5, 2.6)}));
    g.setBrush(kYellow);
    g.drawRect(QRectF(5, -2.6, len - 8, 5.2));
    g.setBrush(QColor(230, 120, 140));
    g.drawRect(QRectF(len - 3, -2.6, 3, 5.2));
    g.restore();
}

void paintIcon(QPainter& g, const QString& id) {
    g.setRenderHint(QPainter::Antialiasing);
    QPen pen(kInk, 1.6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    g.setPen(pen);
    if (id == QLatin1String("select")) {
        QPolygonF arrow({QPointF(9, 4), QPointF(9, 25), QPointF(14, 20), QPointF(18, 28), QPointF(21, 26.5),
                         QPointF(17, 18.5), QPointF(24, 18.5)});
        g.setBrush(Qt::white);
        g.drawPolygon(arrow);
    } else if (id == QLatin1String("eraser")) {
        g.save();
        g.translate(16, 17);
        g.rotate(-35);
        g.setBrush(QColor(240, 150, 170));
        g.drawRoundedRect(QRectF(-11, -6, 16, 12), 2, 2);
        g.setBrush(QColor(90, 130, 210));
        g.drawRoundedRect(QRectF(5, -6, 7, 12), 2, 2);
        g.restore();
    } else if (id == QLatin1String("line")) {
        g.drawLine(QPointF(4, 28), QPointF(14, 18));
        pencil(g, QPointF(14, 18), 20);
    } else if (id == QLatin1String("rectangle")) {
        g.setBrush(QColor(255, 255, 255));
        g.drawRect(QRectF(4, 8, 18, 16));
        pencil(g, QPointF(18, 19), 16);
    } else if (id == QLatin1String("circle")) {
        g.setBrush(Qt::white);
        g.drawEllipse(QRectF(4, 6, 20, 20));
        g.setPen(QPen(kRed, 1.4));
        g.drawLine(QPointF(14, 16), QPointF(24, 16));
        pencil(g, QPointF(20, 20), 14);
    } else if (id == QLatin1String("pushpull")) {
        drawBox(g, QColor(235, 235, 235), QColor(190, 190, 190), QColor(160, 160, 160));
        g.setPen(QPen(kRed, 2.4, Qt::SolidLine, Qt::RoundCap));
        g.drawLine(QPointF(16, 15), QPointF(16, 2.5));
        arrowHead(g, QPointF(16, 0.8), -std::numbers::pi / 2, 7, kRed);
    } else if (id == QLatin1String("move")) {
        g.setPen(QPen(kRed, 2.2, Qt::SolidLine, Qt::RoundCap));
        g.drawLine(QPointF(16, 6), QPointF(16, 26));
        g.drawLine(QPointF(6, 16), QPointF(26, 16));
        arrowHead(g, QPointF(16, 3), -std::numbers::pi / 2, 7, kRed);
        arrowHead(g, QPointF(16, 29), std::numbers::pi / 2, 7, kRed);
        arrowHead(g, QPointF(3, 16), std::numbers::pi, 7, kRed);
        arrowHead(g, QPointF(29, 16), 0, 7, kRed);
    } else if (id == QLatin1String("tape")) {
        g.setBrush(kYellow);
        g.drawEllipse(QRectF(4, 8, 18, 18));
        g.setBrush(QColor(80, 80, 80));
        g.drawEllipse(QRectF(10, 14, 6, 6));
        g.setBrush(QColor(250, 230, 140));
        g.drawRect(QRectF(13, 22, 16, 4));
        for (int x = 16; x < 29; x += 3) g.drawLine(QPointF(x, 22), QPointF(x, 24));
    } else if (id == QLatin1String("paint")) {
        g.save();
        g.translate(14, 17);
        g.rotate(-25);
        g.setBrush(QColor(200, 200, 205));
        g.drawPolygon(QPolygonF({QPointF(-9, -6), QPointF(7, -6), QPointF(5, 9), QPointF(-7, 9)}));
        g.setBrush(QColor(60, 120, 220));
        g.drawEllipse(QRectF(-9, -9, 16, 6));
        g.restore();
        g.setPen(Qt::NoPen);
        g.setBrush(QColor(60, 120, 220));
        g.drawEllipse(QRectF(22, 18, 6, 9));
    } else if (id == QLatin1String("orbit")) {
        g.setBrush(QColor(225, 232, 240));
        g.drawEllipse(QRectF(9, 9, 14, 14));
        g.setBrush(Qt::NoBrush);
        g.setPen(QPen(kRed, 2.0));
        g.drawArc(QRectF(3, 5, 26, 22), 30 * 16, 120 * 16);
        g.setPen(QPen(kBlue, 2.0));
        g.drawArc(QRectF(3, 5, 26, 22), 210 * 16, 120 * 16);
        arrowHead(g, QPointF(5.5, 11), 2.4, 6, kRed);
        arrowHead(g, QPointF(26.5, 21), -0.75, 6, kBlue);
    } else if (id == QLatin1String("pan")) {
        g.setBrush(QColor(250, 225, 200));
        QPainterPath hand;
        hand.moveTo(9, 28);
        hand.cubicTo(5, 22, 4, 17, 7, 15);
        hand.lineTo(10, 18);
        hand.lineTo(10, 7);
        hand.cubicTo(10, 5, 13, 5, 13, 7);
        hand.lineTo(13, 15);
        hand.lineTo(14, 5);
        hand.cubicTo(14, 3, 17, 3, 17, 5);
        hand.lineTo(17, 15);
        hand.lineTo(19, 6);
        hand.cubicTo(19, 4, 22, 4, 22, 6);
        hand.lineTo(21, 16);
        hand.lineTo(23, 10);
        hand.cubicTo(23.5, 8, 26.5, 8.5, 26, 11);
        hand.lineTo(24, 22);
        hand.cubicTo(23, 26, 21, 28, 19, 28);
        hand.closeSubpath();
        g.drawPath(hand);
    } else if (id == QLatin1String("zoom") || id == QLatin1String("zoomext")) {
        g.setBrush(QColor(220, 235, 250));
        g.drawEllipse(QRectF(5, 4, 16, 16));
        g.setPen(QPen(kInk, 4.0, Qt::SolidLine, Qt::RoundCap));
        g.drawLine(QPointF(19, 18), QPointF(27, 27));
        if (id == QLatin1String("zoomext")) {
            g.setPen(QPen(kRed, 1.6));
            for (const auto& [x, y, dx, dy] : std::array<std::array<double, 4>, 4>{
                     {{1, 1, 5, 5}, {31, 1, -5, 5}, {1, 31, 5, -5}, {31, 31, -5, -5}}}) {
                g.drawLine(QPointF(x, y), QPointF(x + dx, y));
                g.drawLine(QPointF(x, y), QPointF(x, y + dy));
            }
        }
    } else if (id == QLatin1String("iso") || id == QLatin1String("top") || id == QLatin1String("front") ||
               id == QLatin1String("right") || id == QLatin1String("back") || id == QLatin1String("left")) {
        const QColor hi(245, 160, 60), plain(235, 235, 235), side(205, 205, 205);
        const bool iso = id == QLatin1String("iso");
        drawBox(g, id == QLatin1String("top") || iso ? hi : plain,
                id == QLatin1String("front") || id == QLatin1String("left") || iso ? hi.darker(115) : side,
                id == QLatin1String("right") || id == QLatin1String("back") || iso ? hi.darker(130) : side.darker(115),
                true, id == QLatin1String("back") || id == QLatin1String("left"));
    } else if (id == QLatin1String("xray")) {
        g.setOpacity(0.55);
        drawBox(g, QColor(200, 215, 235), QColor(170, 185, 210), QColor(150, 165, 195), false);
        g.setOpacity(1.0);
        drawBox(g, Qt::transparent, Qt::transparent, Qt::transparent, true, true, true);
    } else if (id == QLatin1String("backedges")) {
        drawBox(g, QColor(235, 235, 235), QColor(205, 205, 205), QColor(180, 180, 180), true, true);
    } else if (id == QLatin1String("wireframe")) {
        drawBox(g, Qt::transparent, Qt::transparent, Qt::transparent, true, false, true);
        g.setPen(QPen(kInk, 1.2));
        const QPointF back(16, 18);
        g.drawLine(QPointF(16, 6), back);
        g.drawLine(back, QPointF(6, 23));
        g.drawLine(back, QPointF(26, 23));
    } else if (id == QLatin1String("hiddenline")) {
        drawBox(g, Qt::white, Qt::white, Qt::white);
    } else if (id == QLatin1String("shaded")) {
        drawBox(g, QColor(150, 190, 235), QColor(105, 145, 200), QColor(80, 115, 170));
    } else if (id == QLatin1String("textured")) {
        drawBox(g, QColor(205, 120, 90), QColor(175, 95, 70), QColor(150, 80, 60));
        g.setPen(QPen(QColor(245, 230, 220), 0.9));
        for (double y : {19.0, 23.0}) {
            g.drawLine(QPointF(6, y - 4), QPointF(16, y + 1));
            g.drawLine(QPointF(16, y + 1), QPointF(26, y - 4));
        }
    } else if (id == QLatin1String("monochrome")) {
        drawBox(g, QColor(250, 250, 250), QColor(215, 215, 215), QColor(185, 185, 185));
    } else if (id == QLatin1String("new") || id == QLatin1String("open") || id == QLatin1String("save")) {
        if (id == QLatin1String("open")) {
            g.setBrush(QColor(240, 200, 100));
            g.drawPolygon(QPolygonF({QPointF(3, 9), QPointF(12, 9), QPointF(14, 12), QPointF(28, 12),
                                     QPointF(28, 26), QPointF(3, 26)}));
        } else if (id == QLatin1String("save")) {
            g.setBrush(QColor(70, 110, 180));
            g.drawRoundedRect(QRectF(5, 5, 22, 22), 2, 2);
            g.setBrush(Qt::white);
            g.drawRect(QRectF(9, 5, 14, 8));
            g.drawRect(QRectF(9, 18, 14, 9));
        } else {
            g.setBrush(Qt::white);
            g.drawPolygon(QPolygonF({QPointF(7, 3), QPointF(20, 3), QPointF(26, 9), QPointF(26, 29), QPointF(7, 29)}));
            g.drawLine(QPointF(20, 3), QPointF(20, 9));
            g.drawLine(QPointF(20, 9), QPointF(26, 9));
        }
    } else if (id == QLatin1String("undo") || id == QLatin1String("redo")) {
        const bool undo = id == QLatin1String("undo");
        g.save();
        if (!undo) {
            g.translate(32, 0);
            g.scale(-1, 1);
        }
        g.setPen(QPen(kInk, 2.4, Qt::SolidLine, Qt::RoundCap));
        g.setBrush(Qt::NoBrush);
        g.drawArc(QRectF(7, 9, 18, 16), 0, 180 * 16);
        g.drawLine(QPointF(25, 17), QPointF(25, 24));
        arrowHead(g, QPointF(7, 21), std::numbers::pi / 2, 7, kInk);
        g.restore();
    } else if (id == QLatin1String("perspective")) {
        g.setBrush(QColor(230, 230, 230));
        g.drawPolygon(QPolygonF({QPointF(4, 26), QPointF(28, 26), QPointF(20, 8), QPointF(12, 8)}));
    } else if (id == QLatin1String("arc")) {
        g.setBrush(Qt::NoBrush);
        g.setPen(QPen(kInk, 1.8));
        g.drawArc(QRectF(4, 8, 24, 24), 20 * 16, 140 * 16);
        g.setPen(QPen(kRed, 1.4));
        g.drawLine(QPointF(6, 16), QPointF(26, 16));
        pencil(g, QPointF(25, 13), 12);
    } else if (id == QLatin1String("polygon")) {
        QPolygonF hex;
        for (int i = 0; i < 6; ++i) {
            const double a = std::numbers::pi / 3 * i + std::numbers::pi / 6;
            hex << QPointF(15 + 11 * std::cos(a), 16 + 11 * std::sin(a));
        }
        g.setBrush(Qt::white);
        g.drawPolygon(hex);
        pencil(g, QPointF(22, 20), 12);
    } else if (id == QLatin1String("rotate")) {
        g.setBrush(Qt::NoBrush);
        g.setPen(QPen(kRed, 2.6, Qt::SolidLine, Qt::RoundCap));
        g.drawArc(QRectF(5, 5, 22, 22), 40 * 16, 280 * 16);
        arrowHead(g, QPointF(25, 9), -0.6, 8, kRed);
    } else if (id == QLatin1String("scale")) {
        g.setBrush(QColor(225, 225, 225));
        g.drawRect(QRectF(4, 12, 16, 16));
        g.setPen(QPen(kRed, 2.2, Qt::SolidLine, Qt::RoundCap));
        g.drawLine(QPointF(14, 18), QPointF(26, 6));
        arrowHead(g, QPointF(28, 4), -std::numbers::pi / 4, 8, kRed);
    } else if (id == QLatin1String("offset")) {
        g.setBrush(QColor(230, 230, 230));
        g.drawRect(QRectF(4, 6, 24, 20));
        g.setBrush(Qt::white);
        g.setPen(QPen(kRed, 1.6));
        g.drawRect(QRectF(9, 11, 14, 10));
    } else if (id == QLatin1String("zoomwindow")) {
        g.setPen(QPen(kRed, 1.4, Qt::DashLine));
        g.setBrush(Qt::NoBrush);
        g.drawRect(QRectF(13, 13, 16, 14));
        g.setPen(QPen(kInk, 1.6));
        g.setBrush(QColor(220, 235, 250));
        g.drawEllipse(QRectF(3, 3, 13, 13));
        g.setPen(QPen(kInk, 3.4, Qt::SolidLine, Qt::RoundCap));
        g.drawLine(QPointF(14, 14), QPointF(19, 19));
    } else if (id == QLatin1String("fov")) {
        g.setBrush(QColor(220, 235, 250));
        g.drawPolygon(QPolygonF({QPointF(4, 16), QPointF(28, 5), QPointF(28, 27)}));
        g.setBrush(kInk);
        g.drawEllipse(QRectF(2, 13, 6, 6));
    } else if (id == QLatin1String("walk")) {
        g.setBrush(kInk);
        g.drawEllipse(QRectF(6, 4, 8, 12));
        g.drawEllipse(QRectF(17, 14, 8, 12));
        g.drawEllipse(QRectF(7, 18, 6, 4));
        g.drawEllipse(QRectF(18, 28, 6, 3));
    } else if (id == QLatin1String("lookaround")) {
        g.setBrush(Qt::white);
        g.drawEllipse(QRectF(3, 9, 26, 14));
        g.setBrush(QColor(60, 120, 200));
        g.drawEllipse(QRectF(11, 10, 12, 12));
        g.setBrush(kInk);
        g.drawEllipse(QRectF(14, 13, 6, 6));
    } else if (id == QLatin1String("positioncamera")) {
        g.setBrush(QColor(240, 200, 120));
        g.drawEllipse(QRectF(12, 3, 8, 8));
        g.setBrush(QColor(80, 120, 200));
        g.drawRoundedRect(QRectF(11, 11, 10, 12), 3, 3);
        g.setPen(QPen(kRed, 1.6));
        g.drawLine(QPointF(6, 29), QPointF(26, 29));
        g.drawLine(QPointF(16, 23), QPointF(16, 29));
    } else if (id == QLatin1String("previous") || id == QLatin1String("next")) {
        g.save();
        if (id == QLatin1String("next")) {
            g.translate(32, 0);
            g.scale(-1, 1);
        }
        g.setBrush(QColor(220, 235, 250));
        g.drawEllipse(QRectF(10, 4, 16, 16));
        g.setPen(QPen(kInk, 3.4, Qt::SolidLine, Qt::RoundCap));
        g.drawLine(QPointF(22, 18), QPointF(28, 26));
        g.setPen(QPen(kBlue, 2.2, Qt::SolidLine, Qt::RoundCap));
        g.drawLine(QPointF(4, 24), QPointF(14, 24));
        arrowHead(g, QPointF(2, 24), std::numbers::pi, 6, kBlue);
        g.restore();
    } else if (id == QLatin1String("component")) {
        drawBox(g, QColor(240, 240, 240), QColor(210, 210, 210), QColor(185, 185, 185));
        g.setPen(QPen(kBlue, 1.6));
        g.setBrush(Qt::NoBrush);
        g.drawRect(QRectF(2, 3, 28, 27));
    } else if (id == QLatin1String("scene")) {
        g.setBrush(QColor(230, 236, 245));
        g.drawRoundedRect(QRectF(3, 8, 26, 18), 3, 3);
        g.setBrush(kGreen);
        g.drawPolygon(QPolygonF({QPointF(5, 24), QPointF(13, 15), QPointF(19, 21), QPointF(23, 17), QPointF(27, 24)}));
    }
}

}  // namespace

QIcon icon(const QString& id) {
    QIcon out;
    for (int size : {32, 64}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter g(&pm);
        g.scale(size / 32.0, size / 32.0);
        paintIcon(g, id);
        g.end();
        out.addPixmap(pm);
    }
    return out;
}

}  // namespace ui
