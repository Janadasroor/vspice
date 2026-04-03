#ifndef PCBDIFFPAIRTOOL_H
#define PCBDIFFPAIRTOOL_H

#include "pcb_trace_tool.h"

/**
 * Differential Pair Routing Tool
 * Routes two parallel traces (P and N) with a fixed gap.
 */
class PCBDiffPairTool : public PCBTraceTool {
    Q_OBJECT
public:
    explicit PCBDiffPairTool(QObject* parent = nullptr);
    
    QString tooltip() const override { return "Diff Pair Routing: Routing parallel P/N signals"; }
    
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void deactivate() override;
    QMap<QString, QVariant> toolProperties() const override;
    void setToolProperty(const QString& name, const QVariant& value) override;

    // Diff pair properties
    double gap() const { return m_pairGap; }
    void setGap(double g) { m_pairGap = g; }

private:
    bool detectPairNetsAtPoint(const QPointF& scenePos, QString& pNet, QString& nNet, QString& pairBase) const;
    QPointF pairOffsetForTarget(const QPointF& pFrom, const QPointF& pTo, const QPointF& nFrom) const;
    void startDiffPair(QPointF p_pos, QPointF n_pos);
    void updateDiffPreview(QPointF p_pos);
    void addDiffSegment(QPointF p_pos);
    void finishDiffPair();
    void cleanupDiffPreview();

    bool m_isDiffRouting = false;
    double m_pairGap = 0.2; // mm
    
    QPointF m_lastP;
    QPointF m_lastN;
    
    // Preview items for the second trace (N)
    QGraphicsLineItem* m_previewN1 = nullptr;
    QGraphicsLineItem* m_previewN2 = nullptr;
    QGraphicsPathItem* m_clearanceHaloN1 = nullptr;
    QGraphicsPathItem* m_clearanceHaloN2 = nullptr;
};

#endif // PCBDIFFPAIRTOOL_H
