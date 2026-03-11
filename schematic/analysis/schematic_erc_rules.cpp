#include "schematic_erc_rules.h"
#include <QJsonArray>

SchematicERCRules::SchematicERCRules() {
    for (int i = 0; i < 12; ++i) {
        for (int j = 0; j < 12; ++j) {
            m_matrix[i][j] = OK;
        }
    }
}

SchematicERCRules::RuleResult SchematicERCRules::getRule(SchematicItem::PinElectricalType t1, SchematicItem::PinElectricalType t2) const {
    return m_matrix[static_cast<int>(t1)][static_cast<int>(t2)];
}

void SchematicERCRules::setRule(SchematicItem::PinElectricalType t1, SchematicItem::PinElectricalType t2, RuleResult result) {
    m_matrix[static_cast<int>(t1)][static_cast<int>(t2)] = result;
    m_matrix[static_cast<int>(t2)][static_cast<int>(t1)] = result; // Symmetric
}

QJsonObject SchematicERCRules::toJson() const {
    QJsonObject root;
    QJsonArray rows;
    for (int i = 0; i < 12; ++i) {
        QJsonArray col;
        for (int j = 0; j < 12; ++j) {
            col.append(static_cast<int>(m_matrix[i][j]));
        }
        rows.append(col);
    }
    root["matrix"] = rows;
    return root;
}

void SchematicERCRules::fromJson(const QJsonObject& json) {
    QJsonArray rows = json["matrix"].toArray();
    for (int i = 0; i < 12 && i < rows.size(); ++i) {
        QJsonArray col = rows[i].toArray();
        for (int j = 0; j < 12 && j < col.size(); ++j) {
            m_matrix[i][j] = static_cast<RuleResult>(col[j].toInt());
        }
    }
}

SchematicERCRules SchematicERCRules::defaultRules() {
    SchematicERCRules r;
    
    using T = SchematicItem::PinElectricalType;
    using Res = RuleResult;

    // 1. Initialize ALL to OK (0)
    for (int i = 0; i < 12; ++i) {
        for (int j = 0; j < 12; ++j) {
            r.m_matrix[i][j] = Res::OK;
        }
    }

    // 2. Severe Conflicts (Errors)
    r.setRule(T::OutputPin, T::OutputPin, Res::Error);
    r.setRule(T::OutputPin, T::PowerOutputPin, Res::Error);
    r.setRule(T::PowerOutputPin, T::PowerOutputPin, Res::Error);
    r.setRule(T::OpenCollectorPin, T::OpenEmitterPin, Res::Error);

    // 3. Potential Conflicts (Warnings)
    r.setRule(T::OutputPin, T::OpenCollectorPin, Res::Warning);
    r.setRule(T::OutputPin, T::OpenEmitterPin, Res::Warning);
    r.setRule(T::OutputPin, T::BidirectionalPin, Res::Warning);
    r.setRule(T::OutputPin, T::TriStatePin, Res::Warning);
    
    r.setRule(T::PowerOutputPin, T::OpenCollectorPin, Res::Warning);
    r.setRule(T::PowerOutputPin, T::OpenEmitterPin, Res::Warning);
    r.setRule(T::PowerOutputPin, T::BidirectionalPin, Res::Warning);
    r.setRule(T::PowerOutputPin, T::TriStatePin, Res::Warning);

    // 4. Unspecified Pin (Warnings when connected to almost anything)
    for (int i = 0; i < 12; ++i) {
        T type = static_cast<T>(i);
        if (type != T::UnspecifiedPin && type != T::NotConnectedPin) {
            r.setRule(T::UnspecifiedPin, type, Res::Warning);
        }
    }

    // 5. Not Connected Pin (Errors if connected to ANYTHING, including other NC pins)
    for (int i = 0; i < 12; ++i) {
        r.m_matrix[static_cast<int>(T::NotConnectedPin)][i] = Res::Error;
        r.m_matrix[i][static_cast<int>(T::NotConnectedPin)] = Res::Error;
    }
    // Exception: NC to NC on same net is still an error (NC means NO connection)
    // but the engine will report it for each pin.

    return r;
}
