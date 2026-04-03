#ifndef PCBDRC_H
#define PCBDRC_H

#include <QObject>
#include <QString>
#include <QPointF>
#include <QList>
#include <QGraphicsScene>
#include "../models/drc_types.h"

class PCBItem;

// Main DRC engine
class PCBDRC : public QObject {
    Q_OBJECT

public:
    explicit PCBDRC(QObject* parent = nullptr);

    // Run checks
    void runFullCheck(QGraphicsScene* scene);
    void runQuickCheck(QGraphicsScene* scene);  // Faster, fewer checks

    // Results
    const QList<DRCViolation>& violations() const { return m_violations; }
    int errorCount() const;
    int warningCount() const;
    void clearViolations() { m_violations.clear(); }

    // Rules
    DRCRules& rules() { return m_rules; }
    const DRCRules& rules() const { return m_rules; }

    // Individual checks
    void checkClearances(QGraphicsScene* scene);
    void checkTraceWidths(QGraphicsScene* scene);
    void checkUnconnectedNets(QGraphicsScene* scene);
    void checkDrillSizes(QGraphicsScene* scene);
    void checkManufacturingRules(QGraphicsScene* scene);
    void checkBoardEdge(QGraphicsScene* scene);
    void checkOverlaps(QGraphicsScene* scene);
    void checkAcuteAngles(QGraphicsScene* scene);
    void checkStubs(QGraphicsScene* scene);
    void checkViaInPad(QGraphicsScene* scene);
    void checkDrillClearance(QGraphicsScene* scene);
    void checkFloatingCopper(QGraphicsScene* scene);

    bool checkItemClearance(PCBItem* item1, PCBItem* item2, double minClearance, QPointF& violationPos);
    QList<QPointF> findLiveViolations(PCBItem* item, double clearance);

signals:
    void checkStarted();
    void checkProgress(int percent, const QString& message);
    void checkCompleted(int errorCount, int warningCount);
    void violationFound(const DRCViolation& violation);

private:
    void addViolation(const DRCViolation& violation);

    QList<DRCViolation> m_violations;
    DRCRules m_rules;
};

#endif // PCBDRC_H
