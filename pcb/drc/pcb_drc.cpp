#include "pcb_drc.h"
#include "pcb_item.h"
#include "trace_item.h"
#include "via_item.h"
#include "pad_item.h"
#include "component_item.h"
#include "copper_pour_item.h"
#include "ratsnest_item.h"
#include "pcb_layer.h"
#include "net_class.h"
#include <QGraphicsItem>
#include <QtMath>
#include <QDebug>
#include <QSet>
#include <QPainterPathStroker>
#include <QGraphicsSimpleTextItem>
#include <algorithm>

namespace {
int copperLayerOrderIndex(int layerId) {
    if (layerId == PCBLayerManager::TopCopper) return 0;
    if (layerId >= 100) return 1 + (layerId - 100);
    if (layerId == PCBLayerManager::BottomCopper) return 1000;
    return layerId;
}
}

// ============================================================================
// PCBDRC Implementation
// ============================================================================

PCBDRC::PCBDRC(QObject* parent)
    : QObject(parent)
{
}

void PCBDRC::runFullCheck(QGraphicsScene* scene) {
    if (!scene) return;

    emit checkStarted();
    m_violations.clear();

    emit checkProgress(0, "Starting DRC...");

    emit checkProgress(10, "Checking clearances & short circuits...");
    checkClearances(scene);

    emit checkProgress(40, "Checking trace widths...");
    checkTraceWidths(scene);

    emit checkProgress(60, "Checking connectivity...");
    checkUnconnectedNets(scene);

    emit checkProgress(70, "Checking drill sizes & clearances...");
    checkDrillSizes(scene);
    checkDrillClearance(scene);

    emit checkProgress(80, "Checking manufacturing rules...");
    checkManufacturingRules(scene);

    emit checkProgress(85, "Checking for floating copper...");
    checkFloatingCopper(scene);

    emit checkProgress(88, "Checking board edge clearance...");
    checkBoardEdge(scene);

    emit checkProgress(91, "Checking acute trace angles...");
    checkAcuteAngles(scene);

    emit checkProgress(94, "Checking for stub traces...");
    checkStubs(scene);

    emit checkProgress(97, "Checking for via-in-pad...");
    checkViaInPad(scene);

    emit checkProgress(99, "Checking redundant overlaps...");
    checkOverlaps(scene);

    emit checkProgress(100, "DRC complete");
    emit checkCompleted(errorCount(), warningCount());

    qDebug() << "DRC complete:" << m_violations.size() << "violations found"
             << "(" << errorCount() << "errors," << warningCount() << "warnings)";
}

void PCBDRC::runQuickCheck(QGraphicsScene* scene) {
    if (!scene) return;

    emit checkStarted();
    m_violations.clear();

    checkClearances(scene);
    checkTraceWidths(scene);

    emit checkCompleted(errorCount(), warningCount());
}

int PCBDRC::errorCount() const {
    int count = 0;
    for (const DRCViolation& v : m_violations) {
        if (v.severity() == DRCViolation::Error) count++;
    }
    return count;
}

int PCBDRC::warningCount() const {
    int count = 0;
    for (const DRCViolation& v : m_violations) {
        if (v.severity() == DRCViolation::Warning) count++;
    }
    return count;
}

void PCBDRC::addViolation(const DRCViolation& violation) {
    m_violations.append(violation);
    emit violationFound(violation);
}

bool PCBDRC::checkItemClearance(PCBItem* item1, PCBItem* item2, double minClearance, QPointF& violationPos) {
    if (item1 == item2) return false;

    auto itemsOverlapLayers = [](PCBItem* a, PCBItem* b) {
        // If they share a primary layer, they definitely overlap
        if (a->layer() == b->layer()) return true;

        // Check if either is a through-hole item (spans all copper)
        const PadItem* padA = dynamic_cast<const PadItem*>(a);
        const PadItem* padB = dynamic_cast<const PadItem*>(b);
        const ViaItem* viaA = dynamic_cast<const ViaItem*>(a);
        const ViaItem* viaB = dynamic_cast<const ViaItem*>(b);

        bool aIsTH = (viaA || (padA && padA->drillSize() > 0.001));
        bool bIsTH = (viaB || (padB && padB->drillSize() > 0.001));

        if (aIsTH && bIsTH) return true; // Both through-hole, they always overlap in Z

        if (aIsTH) {
            const PCBLayer* layerB = PCBLayerManager::instance().layer(b->layer());
            return layerB && layerB->isCopperLayer();
        }
        if (bIsTH) {
            const PCBLayer* layerA = PCBLayerManager::instance().layer(a->layer());
            return layerA && layerA->isCopperLayer();
        }

        return false;
    };

    if (!itemsOverlapLayers(item1, item2)) return false;
    
    // Skip items on the same net (they can touch/overlap)
    if (!item1->netName().isEmpty() && item1->netName() == item2->netName()) return false;

    QPainterPath path1 = item1->sceneTransform().map(item1->shape());
    QPainterPath path2 = item2->sceneTransform().map(item2->shape());

    // 1. Direct short circuit check
    if (path1.intersects(path2)) {
        QPainterPath inter = path1.intersected(path2);
        violationPos = inter.isEmpty() ? item1->scenePos() : inter.pointAtPercent(0.5);
        return true;
    }

    // 2. Accurate spacing check using expansion
    QPainterPathStroker stroker;
    stroker.setWidth(minClearance * 2.0);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);

    QPainterPath expanded = stroker.createStroke(path1);
    if (expanded.intersects(path2)) {
        violationPos = (item1->scenePos() + item2->scenePos()) / 2.0;
        return true;
    }

    return false;
}

QList<QPointF> PCBDRC::findLiveViolations(PCBItem* item, double clearance) {
    QList<QPointF> results;
    if (!item || !item->scene()) return results;

    // Use a bounding rect expanded by clearance to find potential candidates
    QRectF searchRect = item->sceneBoundingRect().adjusted(-clearance, -clearance, clearance, clearance);
    QList<QGraphicsItem*> candidates = item->scene()->items(searchRect);

    for (QGraphicsItem* otherItem : candidates) {
        PCBItem* otherPcbItem = dynamic_cast<PCBItem*>(otherItem);
        if (!otherPcbItem || otherPcbItem == item) continue;

        QPointF violationPos;
        if (checkItemClearance(item, otherPcbItem, clearance, violationPos)) {
            results.append(violationPos);
        }
    }

    return results;
}

void PCBDRC::checkClearances(QGraphicsScene* scene) {
    QList<QGraphicsItem*> allItems = scene->items();
    QList<PCBItem*> copperItems;

    for (auto* item : allItems) {
        PCBItem* pcbItem = dynamic_cast<PCBItem*>(item);
        if (!pcbItem) continue;

        // Only check physical primitives that carry copper
        PCBItem::ItemType type = pcbItem->itemType();
        if (type == PCBItem::PadType || type == PCBItem::ViaType || 
            type == PCBItem::TraceType || type == PCBItem::CopperPourType) {
            copperItems.append(pcbItem);
        }
    }

    double globalMinClearance = m_rules.minClearance();
    QSet<QString> checkedPairs;

    for (int i = 0; i < copperItems.size(); ++i) {
        PCBItem* item1 = copperItems[i];
        
        // Determine specific clearance for this item's net
        double itemClearance = globalMinClearance;
        if (!item1->netName().isEmpty()) {
            itemClearance = NetClassManager::instance().getClassForNet(item1->netName()).clearance;
        }
        
        // Spatial search for optimal performance
        QRectF searchRect = item1->sceneBoundingRect().adjusted(-itemClearance, -itemClearance, itemClearance, itemClearance);
        QList<QGraphicsItem*> neighbors = scene->items(searchRect);

        for (auto* neighbor : neighbors) {
            PCBItem* item2 = dynamic_cast<PCBItem*>(neighbor);
            if (!item2 || item1 == item2) continue;
            
            // Only primitives
            PCBItem::ItemType type2 = item2->itemType();
            if (type2 != PCBItem::PadType && type2 != PCBItem::ViaType && 
                type2 != PCBItem::TraceType && type2 != PCBItem::CopperPourType) continue;

            // Use the larger clearance requirement of the two nets
            double clearance2 = globalMinClearance;
            if (!item2->netName().isEmpty()) {
                clearance2 = NetClassManager::instance().getClassForNet(item2->netName()).clearance;
            }
            double requiredClearance = std::max(itemClearance, clearance2);

            // Custom rule override (net/class pair) can enforce a specific clearance.
            bool customMatched = false;
            const double custom = NetClassManager::instance().getCustomClearanceForNets(
                item1->netName(), item2->netName(), &customMatched);
            if (customMatched) {
                requiredClearance = custom;
            }

            // Ensure symmetric check only once using IDs as keys
            QString id1 = item1->idString();
            QString id2 = item2->idString();
            QString pairKey = (id1 < id2) ? (id1 + "|" + id2) : (id2 + "|" + id1);
            if (checkedPairs.contains(pairKey)) continue;
            checkedPairs.insert(pairKey);

            QPointF violationPos;
            if (checkItemClearance(item1, item2, requiredClearance, violationPos)) {
                QPainterPath p1 = item1->sceneTransform().map(item1->shape());
                QPainterPath p2 = item2->sceneTransform().map(item2->shape());
                bool isShort = p1.intersects(p2);
                
                if (isShort) {
                    addViolation(DRCViolation(
                        DRCViolation::ShortCircuit,
                        DRCViolation::Error,
                        QString("Short circuit: '%1' and '%2' (%3 and %4)")
                            .arg(item1->netName().isEmpty() ? "No Net" : item1->netName())
                            .arg(item2->netName().isEmpty() ? "No Net" : item2->netName())
                            .arg(item1->itemTypeName())
                            .arg(item2->itemTypeName()),
                        violationPos,
                        item1->idString(),
                        item2->idString()
                    ));
                } else {
                    addViolation(DRCViolation(
                        DRCViolation::ClearanceViolation,
                        DRCViolation::Warning,
                        QString("Clearance violation: %1 vs %2 (req %3mm%4)")
                            .arg(item1->itemTypeName())
                            .arg(item2->itemTypeName())
                            .arg(requiredClearance)
                            .arg(customMatched ? ", custom rule" : ""),
                        violationPos,
                        item1->idString(),
                        item2->idString()
                    ));
                }
            }
        }

        if (i % 50 == 0) {
            emit checkProgress(10 + (int)(30.0 * i / copperItems.size()), "Checking clearances...");
        }
    }
}

void PCBDRC::checkTraceWidths(QGraphicsScene* scene) {
    int count = 0;
    QList<QGraphicsItem*> items = scene->items();
    for (QGraphicsItem* item : items) {
        TraceItem* trace = dynamic_cast<TraceItem*>(item);
        if (!trace) continue;

        double width = trace->width();
        double minWidth = m_rules.minTraceWidth();
        
        // Use net class specific rule if available
        if (!trace->netName().isEmpty()) {
            minWidth = NetClassManager::instance().getClassForNet(trace->netName()).traceWidth;
        }

        if (width < minWidth - 0.001) { // Fuzzy compare
            addViolation(DRCViolation(
                DRCViolation::MinTraceWidth,
                DRCViolation::Error,
                QString("Trace width %1mm below required %2mm for net '%3'")
                    .arg(width, 0, 'f', 3)
                    .arg(minWidth, 0, 'f', 3)
                    .arg(trace->netName()),
                trace->scenePos(),
                trace->idString()
            ));
        }
        
        count++;
        if (count % 20 == 0) {
            emit checkProgress(40 + (int)(20.0 * count / items.size()), "Checking trace widths...");
        }
    }
}

void PCBDRC::checkUnconnectedNets(QGraphicsScene* scene) {
    // Collect all airwires (RatsnestItems)
    // If an airwire exists in the scene, the net is not fully connected.
    QMap<QString, int> airwireCount;
    
    for (QGraphicsItem* item : scene->items()) {
        if (RatsnestItem* rats = dynamic_cast<RatsnestItem*>(item)) {
            // Find which net this airwire belongs to by looking at its endpoints
            // (Simple heuristic: find nearest pad at endpoint p1)
            QList<QGraphicsItem*> candidates = scene->items(rats->boundingRect().center());
            for (auto* cand : candidates) {
                 if (PCBItem* pcb = dynamic_cast<PCBItem*>(cand)) {
                     if (!pcb->netName().isEmpty() && pcb->netName() != "No Net") {
                         airwireCount[pcb->netName()]++;
                         break;
                     }
                 }
            }
        }
    }

    for (auto it = airwireCount.begin(); it != airwireCount.end(); ++it) {
        addViolation(DRCViolation(
            DRCViolation::UnconnectedNet,
            DRCViolation::Warning,
            QString("Net '%1' is not fully routed (%2 airwires remaining)").arg(it.key()).arg(it.value()),
            QPointF() // No specific location for a whole net violation
        ));
    }
}

void PCBDRC::checkDrillSizes(QGraphicsScene* scene) {
    for (QGraphicsItem* item : scene->items()) {
        if (ViaItem* via = dynamic_cast<ViaItem*>(item)) {
            double drill = via->drillSize();
            double minDrill = m_rules.minViaDrill();
            
            if (!via->netName().isEmpty()) {
                minDrill = NetClassManager::instance().getClassForNet(via->netName()).viaDrill;
            }

            if (drill < minDrill - 0.001) {
                addViolation(DRCViolation(
                    DRCViolation::MinDrillSize,
                    DRCViolation::Error,
                    QString("Via drill %1mm < required %2mm for net '%3'").arg(drill).arg(minDrill).arg(via->netName()),
                    via->scenePos(),
                    via->idString()
                ));
            }

            if (via->isMicrovia()) {
                constexpr double maxMicroviaDrill = 0.15;
                constexpr double maxMicroviaDiameter = 0.35;
                if (via->drillSize() > maxMicroviaDrill + 0.001) {
                    addViolation(DRCViolation(
                        DRCViolation::MinDrillSize,
                        DRCViolation::Error,
                        QString("Microvia drill %1mm exceeds max %2mm")
                            .arg(via->drillSize(), 0, 'f', 3)
                            .arg(maxMicroviaDrill, 0, 'f', 3),
                        via->scenePos(),
                        via->idString()
                    ));
                }
                if (via->diameter() > maxMicroviaDiameter + 0.001) {
                    addViolation(DRCViolation(
                        DRCViolation::OverlappingItems,
                        DRCViolation::Warning,
                        QString("Microvia diameter %1mm exceeds typical max %2mm")
                            .arg(via->diameter(), 0, 'f', 3)
                            .arg(maxMicroviaDiameter, 0, 'f', 3),
                        via->scenePos(),
                        via->idString()
                    ));
                }

                const int a = copperLayerOrderIndex(via->startLayer());
                const int b = copperLayerOrderIndex(via->endLayer());
                const int delta = std::abs(a - b);
                if (delta != 1) {
                    addViolation(DRCViolation(
                        DRCViolation::ClearanceViolation,
                        DRCViolation::Error,
                        "Microvia must connect adjacent copper layers only",
                        via->scenePos(),
                        via->idString()
                    ));
                }
            }
        } else if (PadItem* pad = dynamic_cast<PadItem*>(item)) {
            double drill = pad->drillSize();
            double minDrill = m_rules.minDrillSize();
            
            // Pad rules might also be influenced by net class in some specs, 
            // though usually they are footprint-defined. We'll use global min as fallback.
            if (drill > 0 && drill < minDrill - 0.001) {
                addViolation(DRCViolation(
                    DRCViolation::MinDrillSize,
                    DRCViolation::Error,
                    QString("Pad drill %1mm < minimum %2mm").arg(drill).arg(m_rules.minDrillSize()),
                    pad->scenePos(),
                    pad->idString()
                ));
            }
        }
    }
}

void PCBDRC::checkManufacturingRules(QGraphicsScene* scene) {
    if (!scene) return;

    const double minAnnularRingMm = m_rules.minAnnularRing();
    const double minSolderMaskBridgeMm = m_rules.minSolderMaskBridge();

    struct MaskOpening {
        QPainterPath path;
        QRectF bounds;
        int sideLayer;
        QString id;
    };
    QList<MaskOpening> maskOpenings;
    maskOpenings.reserve(scene->items().size());

    auto addMaskOpening = [&](PCBItem* item, int sideLayer) {
        if (!item) return;
        MaskOpening opening;
        opening.path = item->sceneTransform().map(item->shape());
        opening.bounds = opening.path.boundingRect();
        opening.sideLayer = sideLayer;
        opening.id = item->idString();
        if (!opening.path.isEmpty()) {
            maskOpenings.append(opening);
        }
    };

    auto runAnnularRingCheck = [&](PCBItem* item, double outerDiameter, double drillSize) {
        if (!item || drillSize <= 0.0 || outerDiameter <= 0.0) return;
        const double annular = (outerDiameter - drillSize) * 0.5;
        if (annular + 1e-6 < minAnnularRingMm) {
            addViolation(DRCViolation(
                DRCViolation::MinDrillSize,
                DRCViolation::Error,
                QString("Annular ring too small (%1mm < %2mm) on %3")
                    .arg(annular, 0, 'f', 3)
                    .arg(minAnnularRingMm, 0, 'f', 3)
                    .arg(item->itemTypeName()),
                item->scenePos(),
                item->idString()
            ));
        }
    };

    for (QGraphicsItem* raw : scene->items()) {
        if (ViaItem* via = dynamic_cast<ViaItem*>(raw)) {
            runAnnularRingCheck(via, via->diameter(), via->drillSize());
            if (via->spansLayer(PCBLayerManager::TopCopper)) {
                addMaskOpening(via, PCBLayerManager::TopSoldermask);
            }
            if (via->spansLayer(PCBLayerManager::BottomCopper)) {
                addMaskOpening(via, PCBLayerManager::BottomSoldermask);
            }
            continue;
        }

        if (PadItem* pad = dynamic_cast<PadItem*>(raw)) {
            const QSizeF s = pad->size();
            runAnnularRingCheck(pad, std::min(s.width(), s.height()), pad->drillSize());

            PCBLayer* l = PCBLayerManager::instance().layer(pad->layer());
            if (!l) continue;
            if (l->side() == PCBLayer::Top) {
                addMaskOpening(pad, PCBLayerManager::TopSoldermask);
            } else if (l->side() == PCBLayer::Bottom) {
                addMaskOpening(pad, PCBLayerManager::BottomSoldermask);
            } else {
                addMaskOpening(pad, PCBLayerManager::TopSoldermask);
                addMaskOpening(pad, PCBLayerManager::BottomSoldermask);
            }
            continue;
        }
    }

    // Solder-mask bridge check (mask dam between nearby openings).
    QSet<QString> checkedPairs;
    QPainterPathStroker bridgeStroker;
    bridgeStroker.setWidth(minSolderMaskBridgeMm * 2.0);
    bridgeStroker.setCapStyle(Qt::RoundCap);
    bridgeStroker.setJoinStyle(Qt::RoundJoin);

    for (int i = 0; i < maskOpenings.size(); ++i) {
        const MaskOpening& a = maskOpenings[i];
        const QRectF searchRect = a.bounds.adjusted(
            -minSolderMaskBridgeMm,
            -minSolderMaskBridgeMm,
            minSolderMaskBridgeMm,
            minSolderMaskBridgeMm
        );

        for (int j = i + 1; j < maskOpenings.size(); ++j) {
            const MaskOpening& b = maskOpenings[j];
            if (a.sideLayer != b.sideLayer) continue;
            if (!searchRect.intersects(b.bounds)) continue;

            const QString pairKey = (a.id < b.id) ? (a.id + "|" + b.id) : (b.id + "|" + a.id);
            if (checkedPairs.contains(pairKey)) continue;
            checkedPairs.insert(pairKey);

            const bool overlap = a.path.intersects(b.path);
            const bool tooClose = overlap || bridgeStroker.createStroke(a.path).intersects(b.path);
            if (!tooClose) continue;

            const QPointF where = (a.bounds.center() + b.bounds.center()) * 0.5;
            addViolation(DRCViolation(
                DRCViolation::ClearanceViolation,
                DRCViolation::Warning,
                QString("Solder mask bridge too small between openings (%1mm required)")
                    .arg(minSolderMaskBridgeMm, 0, 'f', 3),
                where,
                a.id,
                b.id
            ));
        }
    }

    // Silkscreen-to-mask clearance check.
    const double silkToMaskMm = m_rules.silkToPad();
    QPainterPathStroker silkStroker;
    silkStroker.setWidth(silkToMaskMm * 2.0);
    silkStroker.setCapStyle(Qt::RoundCap);
    silkStroker.setJoinStyle(Qt::RoundJoin);

    struct SilkPrimitive {
        QPainterPath path;
        QRectF bounds;
        int sideLayer;
        QString id;
    };
    QList<SilkPrimitive> silkPrims;

    for (QGraphicsItem* raw : scene->items()) {
        if (PCBItem* pcb = dynamic_cast<PCBItem*>(raw)) {
            if (pcb->layer() == PCBLayerManager::TopSilkscreen || pcb->layer() == PCBLayerManager::BottomSilkscreen) {
                SilkPrimitive p;
                p.path = pcb->sceneTransform().map(pcb->shape());
                p.bounds = p.path.boundingRect();
                p.sideLayer = (pcb->layer() == PCBLayerManager::TopSilkscreen)
                    ? PCBLayerManager::TopSoldermask
                    : PCBLayerManager::BottomSoldermask;
                p.id = pcb->idString();
                if (!p.path.isEmpty()) silkPrims.append(p);
            }

            if (ComponentItem* comp = dynamic_cast<ComponentItem*>(pcb)) {
                const int sideLayer = (comp->layer() == PCBLayerManager::BottomCopper)
                    ? PCBLayerManager::BottomSoldermask
                    : PCBLayerManager::TopSoldermask;
                for (QGraphicsItem* child : comp->childItems()) {
                    if (!child || !child->isVisible()) continue;
                    if (dynamic_cast<PadItem*>(child)) continue;
                    if (dynamic_cast<QGraphicsSimpleTextItem*>(child)) continue;

                    SilkPrimitive p;
                    p.path = child->sceneTransform().map(child->shape());
                    p.bounds = p.path.boundingRect();
                    p.sideLayer = sideLayer;
                    p.id = comp->idString();
                    if (!p.path.isEmpty()) silkPrims.append(p);
                }
            }
        }
    }

    for (const SilkPrimitive& silk : silkPrims) {
        const QRectF searchRect = silk.bounds.adjusted(-silkToMaskMm, -silkToMaskMm, silkToMaskMm, silkToMaskMm);
        for (const MaskOpening& opening : maskOpenings) {
            if (opening.sideLayer != silk.sideLayer) continue;
            if (!searchRect.intersects(opening.bounds)) continue;

            const bool overlap = silk.path.intersects(opening.path);
            const bool tooClose = overlap || silkStroker.createStroke(silk.path).intersects(opening.path);
            if (!tooClose) continue;

            const QPointF where = (silk.bounds.center() + opening.bounds.center()) * 0.5;
            addViolation(DRCViolation(
                DRCViolation::SilkTextOnPad,
                DRCViolation::Warning,
                QString("Silkscreen too close to solder mask opening (%1mm required)")
                    .arg(silkToMaskMm, 0, 'f', 3),
                where,
                silk.id,
                opening.id
            ));
        }
    }
}

void PCBDRC::checkDrillClearance(QGraphicsScene* scene) {
    if (!scene) return;

    struct DrillHole {
        QPointF center;
        double radius;
        QString id;
    };
    QList<DrillHole> holes;

    for (auto* item : scene->items()) {
        if (ViaItem* via = dynamic_cast<ViaItem*>(item)) {
            holes.append({via->scenePos(), via->drillSize() / 2.0, via->idString()});
        } else if (PadItem* pad = dynamic_cast<PadItem*>(item)) {
            if (pad->drillSize() > 0) {
                holes.append({pad->scenePos(), pad->drillSize() / 2.0, pad->idString()});
            }
        }
    }

    const double minHoleToHole = 0.25; // 0.25mm default
    QSet<QString> checkedPairs;

    for (int i = 0; i < holes.size(); ++i) {
        for (int j = i + 1; j < holes.size(); ++j) {
            const auto& h1 = holes[i];
            const auto& h2 = holes[j];

            double dist = QLineF(h1.center, h2.center).length();
            double edgeDist = dist - (h1.radius + h2.radius);

            if (edgeDist < minHoleToHole) {
                const QString pairKey = (h1.id < h2.id) ? (h1.id + "|" + h2.id) : (h2.id + "|" + h1.id);
                if (checkedPairs.contains(pairKey)) continue;
                checkedPairs.insert(pairKey);

                addViolation(DRCViolation(
                    DRCViolation::MinDrillSize,
                    DRCViolation::Error,
                    QString("Drill holes too close (%1mm < %2mm)").arg(edgeDist, 0, 'f', 3).arg(minHoleToHole, 0, 'f', 3),
                    (h1.center + h2.center) / 2.0,
                    h1.id,
                    h2.id
                ));
            }
        }
    }
}

void PCBDRC::checkFloatingCopper(QGraphicsScene* scene) {
    if (!scene) return;

    for (auto* raw : scene->items()) {
        PCBItem* item = dynamic_cast<PCBItem*>(raw);
        if (!item) continue;

        // Only check copper items
        PCBItem::ItemType type = item->itemType();
        if (type != PCBItem::TraceType && type != PCBItem::ViaType && 
            type != PCBItem::PadType && type != PCBItem::CopperPourType) continue;

        if (item->netName().isEmpty() || item->netName() == "No Net") {
            // Check if it's a Pad (often pads are part of a component, some might be mounting holes)
            if (type == PCBItem::PadType) {
                 PadItem* pad = dynamic_cast<PadItem*>(item);
                 if (pad && (pad->drillSize() > 0 || pad->idString().contains("mounting", Qt::CaseInsensitive))) continue;
            }

            addViolation(DRCViolation(
                DRCViolation::FloatingCopper,
                DRCViolation::Info,
                QString("Floating copper (%1) with no net assigned").arg(item->itemTypeName()),
                item->scenePos(),
                item->idString()
            ));
        }
    }
}

void PCBDRC::checkBoardEdge(QGraphicsScene* scene) {
    QPainterPath boardOutline;
    bool foundEdge = false;

    // Use standard Edge Cuts layer
    for (auto* item : scene->items()) {
        PCBItem* pcbItem = dynamic_cast<PCBItem*>(item);
        if (pcbItem && pcbItem->layer() == PCBLayerManager::EdgeCuts) {
            boardOutline.addPath(pcbItem->sceneTransform().map(pcbItem->shape()));
            foundEdge = true;
        }
    }

    if (!foundEdge) return;

    QRectF boardRect = boardOutline.boundingRect();
    double gap = m_rules.copperToEdge();
    QRectF safeRect = boardRect.adjusted(gap, gap, -gap, -gap);

    for (auto* item : scene->items()) {
        PCBItem* pcbItem = dynamic_cast<PCBItem*>(item);
        if (!pcbItem || pcbItem->layer() == PCBLayerManager::EdgeCuts || pcbItem->itemType() == PCBItem::RatsnestType) continue;

        QPainterPath itemPath = pcbItem->sceneTransform().map(pcbItem->shape());
        
        // 1. Check if item is outside board
        if (!boardOutline.contains(itemPath)) {
            // Check if it's completely outside or partially
            if (boardOutline.intersected(itemPath).isEmpty()) {
                 addViolation(DRCViolation(
                    DRCViolation::BoardEdgeClearance,
                    DRCViolation::Error,
                    QString("Item on net '%1' is completely outside board area").arg(pcbItem->netName()),
                    pcbItem->scenePos(),
                    pcbItem->idString()
                ));
            } else {
                 addViolation(DRCViolation(
                    DRCViolation::BoardEdgeClearance,
                    DRCViolation::Error,
                    QString("Item on net '%1' crosses board edge").arg(pcbItem->netName()),
                    pcbItem->scenePos(),
                    pcbItem->idString()
                ));
            }
            continue;
        }

        // 2. Check gap to edge
        QPainterPathStroker stroker;
        stroker.setWidth(gap * 2.0);
        QPainterPath expanded = stroker.createStroke(itemPath);
        
        // If the expanded item path intersects the board outline path, it's too close
        if (expanded.intersects(boardOutline)) {
            addViolation(DRCViolation(
                DRCViolation::BoardEdgeClearance,
                DRCViolation::Warning,
                QString("Item on net '%1' is too close to board edge (%2mm required)").arg(pcbItem->netName()).arg(gap),
                pcbItem->scenePos(),
                pcbItem->idString()
            ));
        }
    }
}

void PCBDRC::checkAcuteAngles(QGraphicsScene* scene) {
    if (!scene) return;

    QList<TraceItem*> traces;
    for (auto* item : scene->items()) {
        if (TraceItem* trace = dynamic_cast<TraceItem*>(item)) {
            traces.append(trace);
        }
    }

    auto getAngle = [](const QPointF& p1, const QPointF& p2, const QPointF& common) {
        QLineF line1(common, p1);
        QLineF line2(common, p2);
        double angle = line1.angleTo(line2);
        if (angle > 180.0) angle = 360.0 - angle;
        return angle;
    };

    QSet<QString> checkedPairs;

    for (int i = 0; i < traces.size(); ++i) {
        TraceItem* t1 = traces[i];
        QPointF p1_s = t1->startPoint();
        QPointF p1_e = t1->endPoint();

        // Check both ends for other traces
        auto checkEnd = [&](const QPointF& common, const QPointF& other) {
            // Find all items at this point
            // Use a very small search rect
            QRectF searchRect(common.x() - 0.01, common.y() - 0.01, 0.02, 0.02);
            for (auto* item : scene->items(searchRect)) {
                TraceItem* t2 = dynamic_cast<TraceItem*>(item);
                if (!t2 || t1 == t2) continue;
                
                // Traces must be on the same layer or connected via a TH item
                if (t1->layer() != t2->layer()) continue;

                // Ensure unique pairs
                QString id1 = t1->idString();
                QString id2 = t2->idString();
                QString pairKey = (id1 < id2) ? (id1 + "|" + id2) : (id2 + "|" + id1);
                if (checkedPairs.contains(pairKey)) continue;

                // Find the other point of t2
                QPointF p2_s = t2->startPoint();
                QPointF p2_e = t2->endPoint();
                QPointF p2_other;
                
                // Fuzzy point comparison
                if (QLineF(common, p2_s).length() < 0.01) {
                    p2_other = p2_e;
                } else if (QLineF(common, p2_e).length() < 0.01) {
                    p2_other = p2_s;
                } else {
                    continue; // They don't actually share this point
                }

                double angle = getAngle(other, p2_other, common);
                if (angle < 89.0) { // Using 89 to avoid floating point issues with 90
                    checkedPairs.insert(pairKey);
                    auto l = PCBLayerManager::instance().layer(t1->layer());
                    addViolation(DRCViolation(
                        DRCViolation::AcuteAngle,
                        DRCViolation::Warning,
                        QString("Acute angle (%1°) between traces on layer %2")
                            .arg(angle, 0, 'f', 1)
                            .arg(l ? l->name() : QString::number(t1->layer())),
                        common,
                        t1->idString(),
                        t2->idString()
                    ));
                }
            }
        };

        checkEnd(p1_s, p1_e);
        checkEnd(p1_e, p1_s);

        if (i % 50 == 0) {
            emit checkProgress(91 + (int)(3.0 * i / traces.size()), "Checking acute angles...");
        }
    }
}

void PCBDRC::checkStubs(QGraphicsScene* scene) {
    if (!scene) return;

    QList<TraceItem*> traces;
    for (auto* item : scene->items()) {
        if (TraceItem* trace = dynamic_cast<TraceItem*>(item)) {
            traces.append(trace);
        }
    }

    for (int i = 0; i < traces.size(); ++i) {
        TraceItem* t1 = traces[i];
        QPointF ends[2] = { t1->startPoint(), t1->endPoint() };

        for (int e = 0; e < 2; ++e) {
            const QPointF& pt = ends[e];
            QRectF searchRect(pt.x() - 0.01, pt.y() - 0.01, 0.02, 0.02);
            bool connected = false;

            for (auto* item : scene->items(searchRect)) {
                PCBItem* pcbItem = dynamic_cast<PCBItem*>(item);
                if (!pcbItem || pcbItem == t1) continue;

                // Traces must be on the same layer to be "connected" at an endpoint
                if (pcbItem->itemType() == PCBItem::TraceType) {
                    if (pcbItem->layer() == t1->layer()) {
                        connected = true;
                        break;
                    }
                } 
                // Pads and Vias also connect
                else if (pcbItem->itemType() == PCBItem::PadType || pcbItem->itemType() == PCBItem::ViaType) {
                    // For pads/vias, they connect if they exist on the trace's layer
                    if (pcbItem->itemType() == PCBItem::ViaType) {
                        ViaItem* via = dynamic_cast<ViaItem*>(pcbItem);
                        if (via && via->spansLayer(t1->layer())) {
                            connected = true;
                            break;
                        }
                    } else { // Pad
                        PadItem* pad = dynamic_cast<PadItem*>(pcbItem);
                        if (pad) {
                            bool padOnLayer = (pad->layer() == t1->layer() || pad->drillSize() > 0.001);
                            if (padOnLayer) {
                                connected = true;
                                break;
                            }
                        }
                    }
                }
            }

            if (!connected) {
                addViolation(DRCViolation(
                    DRCViolation::StubTrace,
                    DRCViolation::Warning,
                    QString("Stub trace (unconnected end) on net '%1'").arg(t1->netName()),
                    pt,
                    t1->idString()
                ));
            }
        }

        if (i % 50 == 0) {
            emit checkProgress(94 + (int)(3.0 * i / traces.size()), "Checking stub traces...");
        }
    }
}

void PCBDRC::checkViaInPad(QGraphicsScene* scene) {
    if (!scene) return;

    QList<ViaItem*> vias;
    QList<PadItem*> pads;

    for (auto* item : scene->items()) {
        if (ViaItem* via = dynamic_cast<ViaItem*>(item)) vias.append(via);
        else if (PadItem* pad = dynamic_cast<PadItem*>(item)) pads.append(pad);
    }

    for (int i = 0; i < vias.size(); ++i) {
        ViaItem* via = vias[i];
        QRectF searchRect = via->sceneBoundingRect();
        
        for (auto* item : scene->items(searchRect)) {
            PadItem* pad = dynamic_cast<PadItem*>(item);
            if (!pad) continue;

            // Only check if they are on the same net (otherwise it's a short circuit, handled elsewhere)
            if (via->netName() != pad->netName()) continue;

            // Check if via center is inside pad
            QPainterPath padPath = pad->sceneTransform().map(pad->shape());
            if (padPath.contains(via->scenePos())) {
                addViolation(DRCViolation(
                    DRCViolation::ViaInPad,
                    DRCViolation::Info,
                    QString("Via in pad detected on net '%1'").arg(via->netName()),
                    via->scenePos(),
                    via->idString(),
                    pad->idString()
                ));
            }
        }

        if (i % 50 == 0) {
            emit checkProgress(97 + (int)(2.0 * i / vias.size()), "Checking via-in-pad...");
        }
    }
}

void PCBDRC::checkOverlaps(QGraphicsScene* scene) {
    // Covered by checkClearances for different nets.
    // Same-net overlaps are generally acceptable for copper pour/traces.
}
