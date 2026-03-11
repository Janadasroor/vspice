#ifndef SCHEMATIC_ERC_H
#define SCHEMATIC_ERC_H

#include <QList>
#include <QString>
#include <QPointF>
#include <QGraphicsScene>
#include "schematic_erc_rules.h"

class SchematicItem;

struct ERCViolation {
    enum Severity {
        Warning,
        Error,
        Critical
    };

    enum Category {
        Connectivity,
        Conflict,
        Annotation,
        Custom
    };
    
    Severity severity;
    Category category;
    QString message;
    QString netName;
    QPointF position;
    SchematicItem* item; // The primary item causing the violation
};

struct SheetContext {
    QString filePath;
    QString sheetName;
};

class SchematicERC {
public:
    static QList<ERCViolation> run(QGraphicsScene* scene, const QString& projectDir = "", class NetManager* netManager = nullptr, const SchematicERCRules& rules = SchematicERCRules::defaultRules());

    /**
     * @brief Run targeted ERC check for a list of items (e.g. while dragging or after placement)
     */
    static QList<ERCViolation> runLive(QGraphicsScene* scene, const QList<SchematicItem*>& items, class NetManager* netManager, const SchematicERCRules& rules = SchematicERCRules::defaultRules());
};

#endif // SCHEMATIC_ERC_H
