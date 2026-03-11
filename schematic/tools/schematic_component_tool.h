#ifndef SCHEMATICCOMPONENTTOOL_H
#define SCHEMATICCOMPONENTTOOL_H

#include "schematic_tool.h"
#include <QString>

class WireItem;

class SchematicComponentTool : public SchematicTool {
    Q_OBJECT

public:
    SchematicComponentTool(const QString& componentType, QObject* parent = nullptr);

    // SchematicTool interface
    QString tooltip() const override { return "Place " + m_componentType + " components"; }
    QString iconName() const override { return m_componentType.toLower(); }
    QCursor cursor() const override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

    void activate(SchematicView* view) override;
    void deactivate() override;

    void keyPressEvent(QKeyEvent* event) override;

private:
    QString m_componentType;
    class SchematicItem* m_previewItem = nullptr;
    QList<WireItem*> m_previewWires;
    qreal m_currentRotation = 0;
    void clearPreviewWires();
};

#endif // SCHEMATICCOMPONENTTOOL_H
