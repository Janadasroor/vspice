#ifndef COPPERPOURITEM_H
#define COPPERPOURITEM_H

#include "pcb_item.h"
#include "../models/copper_pour_model.h"
#include <QPolygonF>
#include <QPen>
#include <QBrush>
#include <QPainterPath>

class CopperPourItem : public PCBItem {
public:
    CopperPourItem(QGraphicsItem* parent = nullptr);
    CopperPourItem(Flux::Model::CopperPourModel* model, QGraphicsItem* parent = nullptr);
    ~CopperPourItem();

    // Data Management
    Flux::Model::CopperPourModel* model() const { return m_model; }
    void setModel(Flux::Model::CopperPourModel* model);
    void setOwned(bool owned) { m_ownsModel = owned; }

    // PCBItem interface
    QString itemTypeName() const override { return "CopperPour"; }
    ItemType itemType() const override { return CopperPourType; }
    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    PCBItem* clone() const override;

    // Delegate properties to the model
    QPolygonF polygon() const { return m_model->polygon(); }
    void setPolygon(const QPolygonF& polygon);
    void addPoint(const QPointF& point);
    void closePolygon();
    void rebuild();

    void setLayer(int layer) override;

    QString netName() const { return m_model->netName(); }
    void setNetName(const QString& net) { m_model->setNetName(net); update(); }

    double clearance() const { return m_model->clearance(); }
    void setClearance(double clearance) { m_model->setClearance(clearance); }

    double minWidth() const { return m_model->minWidth(); }
    void setMinWidth(double width) { m_model->setMinWidth(width); rebuild(); }

    bool filled() const { return m_model->filled(); }
    void setFilled(bool filled) { m_model->setFilled(filled); update(); }

    bool isSolid() const { return m_model->pourType() == Flux::Model::CopperPourModel::SolidPour; }
    void setSolid(bool solid) { m_model->setPourType(solid ? Flux::Model::CopperPourModel::SolidPour : Flux::Model::CopperPourModel::HatchPour); rebuild(); }

    double hatchWidth() const { return m_model->hatchWidth(); }
    void setHatchWidth(double width) { m_model->setHatchWidth(width); rebuild(); }

    int priority() const { return m_model->priority(); }
    void setPriority(int priority);

    bool removeIslands() const { return m_model->removeIslands(); }
    void setRemoveIslands(bool remove);

    // Thermal Relief Settings
    bool useThermalReliefs() const { return m_model->useThermalReliefs(); }
    void setUseThermalReliefs(bool use) { m_model->setUseThermalReliefs(use); rebuild(); }

    double thermalSpokeWidth() const { return m_model->thermalSpokeWidth(); }
    void setThermalSpokeWidth(double width);

    int thermalSpokeCount() const { return m_model->thermalSpokeCount(); }
    void setThermalSpokeCount(int count);

    double thermalSpokeAngleDeg() const { return m_model->thermalSpokeAngleDeg(); }
    void setThermalSpokeAngleDeg(double deg);

    // Pour type
    typedef Flux::Model::CopperPourModel::PourType PourType;
    PourType pourType() const { return m_model->pourType(); }
    void setPourType(PourType type) { m_model->setPourType(type); rebuild(); }

    // QGraphicsItem interface
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
    void updatePath();
    void generateHatchPattern();

    Flux::Model::CopperPourModel* m_model;
    bool m_ownsModel;

    QPainterPath m_path;
    QPainterPath m_hatchPath;
};

#endif // COPPERPOURITEM_H
