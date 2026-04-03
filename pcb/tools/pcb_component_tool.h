#ifndef PCB_COMPONENT_TOOL_H
#define PCB_COMPONENT_TOOL_H

#include "pcb_tool.h"
#include <QCursor>
#include <QString>

class PCBComponentTool : public PCBTool {
    Q_OBJECT

public:
    explicit PCBComponentTool(QObject* parent = nullptr);

    QCursor cursor() const override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void activate(class PCBView* view) override;
    void deactivate() override;

    void setComponentType(const QString& type);
    QString componentType() const { return m_componentType; }

private:
    void updatePreview();

    QString m_componentType;
    class PCBItem* m_previewItem;
};

#endif // PCB_COMPONENT_TOOL_H
