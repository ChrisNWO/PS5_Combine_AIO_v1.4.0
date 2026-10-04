#pragma once
// Иконки бокового меню, нарисованные кодом (QPainter): не зависят ни от шрифтов, ни от Qt SVG,
// поэтому одинаково выглядят на любом ПК и не требуют лишних DLL.
#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>

namespace NavIcons {

enum Kind { Library, Pack, Fpkg, Inspect, Backport, Tasks, Settings, About };

inline QPixmap draw(Kind kind, const QColor &color, int logical = 22, qreal dpr = 2.0)
{
    QPixmap pm(int(logical * dpr), int(logical * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(logical / 24.0, logical / 24.0);          // рисуем в сетке 24x24
    p.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);

    switch (kind) {
    case Library:   // три «корешка» на полке
        p.drawRoundedRect(QRectF(4, 7, 4.2, 12), 1.2, 1.2);
        p.drawRoundedRect(QRectF(9.9, 4, 4.2, 15), 1.2, 1.2);
        p.drawRoundedRect(QRectF(15.8, 9, 4.2, 10), 1.2, 1.2);
        break;
    case Pack:      // коробка с крышкой
        p.drawRoundedRect(QRectF(4, 6, 16, 13), 2.5, 2.5);
        p.drawLine(QPointF(4, 10.5), QPointF(20, 10.5));
        p.drawLine(QPointF(10, 14.5), QPointF(14, 14.5));
        break;
    case Fpkg: {    // пакет (куб)
        QPolygonF hex;
        hex << QPointF(12, 3) << QPointF(20, 7.5) << QPointF(20, 16.5)
            << QPointF(12, 21) << QPointF(4, 16.5) << QPointF(4, 7.5);
        p.drawPolygon(hex);
        p.drawLine(QPointF(12, 12), QPointF(12, 21));
        p.drawLine(QPointF(12, 12), QPointF(4, 7.5));
        p.drawLine(QPointF(12, 12), QPointF(20, 7.5));
        break;
    }
    case Inspect:   // лупа
        p.drawEllipse(QPointF(10.5, 10.5), 6.2, 6.2);
        p.drawLine(QPointF(15.2, 15.2), QPointF(20, 20));
        break;
    case Backport:  // стрелка вниз над «полкой»: понижение
        p.drawLine(QPointF(12, 4.5), QPointF(12, 15.5));
        p.drawLine(QPointF(7, 10.5), QPointF(12, 15.5));
        p.drawLine(QPointF(17, 10.5), QPointF(12, 15.5));
        p.drawLine(QPointF(5.5, 19.5), QPointF(18.5, 19.5));
        break;
    case Tasks:     // список с галочками
        p.drawLine(QPointF(9, 7), QPointF(20, 7));
        p.drawLine(QPointF(9, 12), QPointF(20, 12));
        p.drawLine(QPointF(9, 17), QPointF(20, 17));
        p.drawLine(QPointF(4, 7), QPointF(5.6, 8.6));
        p.drawLine(QPointF(5.6, 8.6), QPointF(7, 5.6));
        p.drawEllipse(QPointF(5.2, 12), 1.1, 1.1);
        p.drawEllipse(QPointF(5.2, 17), 1.1, 1.1);
        break;
    case Settings:  // три ползунка
        p.drawLine(QPointF(4, 7), QPointF(20, 7));
        p.drawLine(QPointF(4, 12), QPointF(20, 12));
        p.drawLine(QPointF(4, 17), QPointF(20, 17));
        p.setBrush(color);
        p.drawEllipse(QPointF(9, 7), 2.3, 2.3);
        p.drawEllipse(QPointF(15, 12), 2.3, 2.3);
        p.drawEllipse(QPointF(8, 17), 2.3, 2.3);
        break;
    case About:     // «i» в круге
        p.drawEllipse(QPointF(12, 12), 8.7, 8.7);
        p.drawLine(QPointF(12, 11), QPointF(12, 16.5));
        p.setBrush(color);
        p.drawEllipse(QPointF(12, 7.6), 1.1, 1.1);
        break;
    }
    return pm;
}

// Серый — обычное состояние, синий — выбранный пункт. Цвета читаются и на тёмной, и на светлой теме.
inline QIcon icon(Kind kind)
{
    QIcon i;
    i.addPixmap(draw(kind, QColor(QStringLiteral("#98a2b3"))), QIcon::Normal, QIcon::Off);
    i.addPixmap(draw(kind, QColor(QStringLiteral("#5b8cff"))), QIcon::Selected, QIcon::Off);
    return i;
}

} // namespace NavIcons
