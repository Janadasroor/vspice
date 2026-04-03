#ifndef TRACEITEM_H
#define TRACEITEM_H

#include "pcb_item.h"
#include "../models/trace_model.h"
#include <QPen>
#include <QColor>

class TraceItem : public PCBItem {
public:
    TraceItem(QPointF start = QPointF(), QPointF end = QPointF(), double width = 0.2, QGraphicsItem *parent = nullptr);
    TraceItem(Flux::Model::TraceModel* model, QGraphicsItem *parent = nullptr);
    ~TraceItem();

    // Data Management
    Flux::Model::TraceModel* model() const { return m_model; }
    void setModel(Flux::Model::TraceModel* model);
    void setOwned(bool owned) { m_ownsModel = owned; }

    // PCBItem interface
    QString itemTypeName() const override { return "Trace"; }
    ItemType itemType() const override { return TraceType; }
    void updateConnectivity() override;
    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    PCBItem* clone() const override;

    // Delegate properties to the model
    QPointF startPoint() const { return m_model->start(); }
    void setStartPoint(QPointF start);

    QPointF endPoint() const { return m_model->end(); }
    void setEndPoint(QPointF end);

    double width() const { return m_model->width(); }
    void setWidth(double width);

    void setLayer(int layer) override;

    // QGraphicsItem interface
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    Flux::Model::TraceModel* m_model;
    bool m_ownsModel; // Track if we should delete the model in destructor
    QPen m_pen;
};

#endif // TRACEITEM_H
