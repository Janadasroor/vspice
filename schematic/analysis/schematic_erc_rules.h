#ifndef SCHEMATIC_ERC_RULES_H
#define SCHEMATIC_ERC_RULES_H

#include <QVector>
#include <QJsonObject>
#include "../items/schematic_item.h"

class SchematicERCRules {
public:
    enum RuleResult {
        OK = 0,
        Warning = 1,
        Error = 2,
        Critical = 3
    };

    SchematicERCRules();

    // Get/Set rule for connecting two pin types
    RuleResult getRule(SchematicItem::PinElectricalType t1, SchematicItem::PinElectricalType t2) const;
    void setRule(SchematicItem::PinElectricalType t1, SchematicItem::PinElectricalType t2, RuleResult result);

    // Serialization
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json);

    // Default KiCad-like matrix
    static SchematicERCRules defaultRules();

private:
    // 12x12 matrix (using the order in PinElectricalType enum)
    RuleResult m_matrix[12][12];
};

#endif // SCHEMATIC_ERC_RULES_H
