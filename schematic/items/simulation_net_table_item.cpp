#include "simulation_net_table_item.h"

#include <QApplication>
#include <QClipboard>
#include <QGraphicsScene>
#include <QGraphicsSceneContextMenuEvent>
#include <QStyleOptionGraphicsItem>
#include <QMenu>
#include <QPainter>
#include <QStyle>

namespace {
QString formatMetric(double value) {
    return QString::number(value, 'g', 6);
}
}

SimulationNetTableItem::SimulationNetTableItem(QGraphicsItem* parent)
    : QGraphicsObject(parent) {
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemIsMovable, true);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
    setZValue(900);
    setAcceptHoverEvents(true);
}

QRectF SimulationNetTableItem::tableRect() const {
    const qreal height = m_padding * 2.0 + m_headerHeight + (m_rows.isEmpty() ? m_rowHeight : m_rowHeight * m_rows.size());
    return QRectF(0, 0, m_width, height);
}

QRectF SimulationNetTableItem::boundingRect() const {
    return tableRect().adjusted(-4, -4, 4, 4);
}

void SimulationNetTableItem::setRows(const QVector<Row>& rows) {
    prepareGeometryChange();
    m_rows = rows;
    update();
}

void SimulationNetTableItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(widget)

    painter->setRenderHint(QPainter::Antialiasing, true);
    const QRectF rect = tableRect();

    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(12, 16, 24, 232));
    painter->drawRoundedRect(rect, 10.0, 10.0);

    painter->setPen(QPen(QColor(64, 92, 132, 220), 1.25));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(rect, 10.0, 10.0);

    QRectF headerRect = rect.adjusted(0, 0, 0, -(rect.height() - m_headerHeight - m_padding));
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(22, 30, 44, 240));
    painter->drawRoundedRect(headerRect.adjusted(m_padding / 2.0, m_padding / 2.0, -m_padding / 2.0, 0), 8.0, 8.0);

    QFont titleFont("Inter", 10, QFont::Bold);
    painter->setFont(titleFont);
    painter->setPen(QColor("#e5eefb"));
    painter->drawText(QRectF(m_padding, m_padding - 2.0, rect.width() - 2 * m_padding, 18.0),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      "Transient Net Voltage Summary");

    QFont headerFont("Inter", 8, QFont::Bold);
    QFont rowFont("Inter", 8);
    painter->setFont(headerFont);
    painter->setPen(QColor("#8ea6ca"));

    const qreal top = m_padding + m_headerHeight;
    const qreal colorX = m_padding;
    const qreal netX = colorX + 30.0;
    const qreal avgX = 190.0;
    const qreal rmsX = 280.0;
    const qreal minX = 370.0;
    const qreal maxX = 455.0;
    painter->drawText(QPointF(colorX, top), "Clr");
    painter->drawText(QPointF(netX, top), "Net");
    painter->drawText(QPointF(avgX, top), "Vavg");
    painter->drawText(QPointF(rmsX, top), "Vrms");
    painter->drawText(QPointF(minX, top), "Vmin");
    painter->drawText(QPointF(maxX, top), "Vmax");

    painter->setFont(rowFont);
    qreal y = top + 8.0;
    for (int index = 0; index < m_rows.size(); ++index) {
        const Row& row = m_rows[index];
        const QRectF rowRect(m_padding / 2.0, y, rect.width() - m_padding, m_rowHeight);

        if (index % 2 == 0) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(255, 255, 255, 10));
            painter->drawRoundedRect(rowRect, 4.0, 4.0);
        }

        painter->setPen(Qt::NoPen);
        painter->setBrush(row.color);
        painter->drawRoundedRect(QRectF(colorX, y + 4.0, 16.0, 12.0), 3.0, 3.0);

        painter->setPen(QColor("#f3f7ff"));
        painter->drawText(QRectF(netX, y, avgX - netX - 8.0, m_rowHeight), Qt::AlignLeft | Qt::AlignVCenter, row.netName);
        painter->drawText(QRectF(avgX, y, 75.0, m_rowHeight), Qt::AlignLeft | Qt::AlignVCenter, formatMetric(row.average));
        painter->drawText(QRectF(rmsX, y, 75.0, m_rowHeight), Qt::AlignLeft | Qt::AlignVCenter, formatMetric(row.rms));
        painter->drawText(QRectF(minX, y, 70.0, m_rowHeight), Qt::AlignLeft | Qt::AlignVCenter, formatMetric(row.minimum));
        painter->drawText(QRectF(maxX, y, 70.0, m_rowHeight), Qt::AlignLeft | Qt::AlignVCenter, formatMetric(row.maximum));
        y += m_rowHeight;
    }

    if (option && (option->state & QStyle::State_Selected)) {
        painter->setPen(QPen(QColor("#f59e0b"), 1.5, Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(rect.adjusted(-2, -2, 2, 2), 11.0, 11.0);
    }
}

void SimulationNetTableItem::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
    QMenu menu;
    QAction* copyAction = menu.addAction("Copy Table");
    QAction* deleteAction = menu.addAction("Delete Table");

    QAction* chosen = menu.exec(event->screenPos());
    if (chosen == copyAction) {
        QStringList lines;
        lines << "Net\tVavg\tVrms\tVmin\tVmax";
        for (const Row& row : m_rows) {
            lines << QString("%1\t%2\t%3\t%4\t%5")
                         .arg(row.netName)
                         .arg(formatMetric(row.average))
                         .arg(formatMetric(row.rms))
                         .arg(formatMetric(row.minimum))
                         .arg(formatMetric(row.maximum));
        }
        QApplication::clipboard()->setText(lines.join('\n'));
    } else if (chosen == deleteAction) {
        Q_EMIT deleteRequested();
        if (scene()) {
            scene()->removeItem(this);
        }
        deleteLater();
    }
}
