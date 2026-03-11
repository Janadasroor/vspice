#ifndef ERC_MARKER_ITEM_H
#define ERC_MARKER_ITEM_H

#include <QGraphicsItem>
#include <QString>
#include "schematic_erc.h"

class ERCMarkerItem : public QGraphicsItem {
public:
    ERCMarkerItem(const ERCViolation& violation, QGraphicsItem* parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QString message() const { return m_violation.message; }
    ERCViolation::Severity severity() const { return m_violation.severity; }

private:
    ERCViolation m_violation;
};

#endif // ERC_MARKER_ITEM_H
