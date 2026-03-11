#include "schematic_tool.h"
#include "schematic_view.h"

SchematicTool::SchematicTool(const QString& name, QObject* parent)
    : QObject(parent), m_name(name), m_view(nullptr) {
}

void SchematicTool::activate(SchematicView* view) {
    m_view = view;
}

void SchematicTool::deactivate() {
    m_view = nullptr;
}

QPointF SchematicTool::snapPoint(QPointF scenePos) {
    if (m_view) {
        return m_view->snapToGrid(scenePos);
    }
    return scenePos;
}
