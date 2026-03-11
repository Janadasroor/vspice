#ifndef SCHEMATICBUSTOOL_H
#define SCHEMATICBUSTOOL_H

#include "schematic_tool.h"

class BusItem;

class SchematicBusTool : public SchematicTool {
    Q_OBJECT

public:
    SchematicBusTool(QObject* parent = nullptr);

    // SchematicTool interface
    QString tooltip() const override { return "Group multiple signals into a bus"; }
    QString iconName() const override { return "bus"; }
    QCursor cursor() const override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    
    void activate(SchematicView* view) override;
    void deactivate() override;

    QMap<QString, QVariant> toolProperties() const override;
    void setToolProperty(const QString& name, const QVariant& value) override;

private:
    void finishBus();
    void updatePreview();
    void reset();

    BusItem* m_currentBus;
    bool m_isDrawing;
    bool m_hFirst;
    QList<QPointF> m_committedPoints;

    double m_width;
    QString m_color;
    QString m_style;
};

#endif // SCHEMATICBUSTOOL_H
