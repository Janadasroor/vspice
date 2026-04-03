#ifndef PCBERASETOOL_H
#define PCBERASETOOL_H

#include "pcb_tool.h"

/**
 * @brief Tool for deleting PCB items (traces, vias, components) by clicking on them
 */
class PCBEraseTool : public PCBTool {
    Q_OBJECT

public:
    explicit PCBEraseTool(QObject* parent = nullptr);

    QString tooltip() const override { return "Erase board items (Traces, Vias, etc.)"; }
    QString iconName() const override { return "erase"; }
    QCursor cursor() const override;

    void mousePressEvent(QMouseEvent* event) override;
};

#endif // PCBERASETOOL_H
