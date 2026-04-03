#ifndef SIMULATION_NET_TABLE_ITEM_H
#define SIMULATION_NET_TABLE_ITEM_H

#include <QColor>
#include <QGraphicsObject>
#include <QVector>

class SimulationNetTableItem : public QGraphicsObject {
    Q_OBJECT

public:
    struct Row {
        QString netName;
        QColor color;
        double average = 0.0;
        double rms = 0.0;
        double minimum = 0.0;
        double maximum = 0.0;
    };

    explicit SimulationNetTableItem(QGraphicsItem* parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    void setRows(const QVector<Row>& rows);
    QVector<Row> rows() const { return m_rows; }

Q_SIGNALS:
    void deleteRequested();

protected:
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

private:
    QRectF tableRect() const;

    QVector<Row> m_rows;
    qreal m_width = 540.0;
    qreal m_rowHeight = 24.0;
    qreal m_headerHeight = 30.0;
    qreal m_padding = 12.0;
};

#endif
