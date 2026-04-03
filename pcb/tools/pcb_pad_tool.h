#ifndef PCBPADTOOL_H
#define PCBPADTOOL_H

#include "pcb_tool.h"

class PCBPadTool : public PCBTool {
    Q_OBJECT

public:
    PCBPadTool(QObject* parent = nullptr);

    // PCBTool interface
    QString tooltip() const override { return "Place pads"; }
    QString iconName() const override { return "pad"; }
    QCursor cursor() const override;

    void mousePressEvent(QMouseEvent* event) override;
};

#endif // PCBPADTOOL_H
