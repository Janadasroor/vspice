#ifndef SCHEMATICHINTITEM_H
#define SCHEMATICHINTITEM_H

#include <QGraphicsObject>
#include <QString>
#include <QColor>
#include <QPropertyAnimation>

class SchematicHintItem : public QGraphicsObject {
    Q_OBJECT
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)
    Q_PROPERTY(qreal scale READ scale WRITE setScale)

public:
    explicit SchematicHintItem(const QString& text, QGraphicsItem* parent = nullptr);
    ~SchematicHintItem() override;

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    void setHintText(const QString& text) { m_text = text; }
    QString hintText() const { return m_text; }

    void setPulse(bool pulse);
    void expand();
    void collapse();

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;

private:
    QString m_text;
    bool m_isHovered = false;
    bool m_isExpanded = false;
    QColor m_hintColor;
    QPropertyAnimation* m_pulseAnim = nullptr;
};

#endif // SCHEMATICHINTITEM_H
