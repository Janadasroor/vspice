#ifndef PCB_DESIGN_RULES_EDITOR_H
#define PCB_DESIGN_RULES_EDITOR_H

#include <QWidget>
#include <functional>
#include "pcb_drc.h"

class QVBoxLayout;
class QDoubleSpinBox;

namespace Flux {

/**
 * @brief A modern, interactive editor for PCB design rules and constraints.
 */
class PCBDesignRulesEditor : public QWidget {
    Q_OBJECT
public:
    explicit PCBDesignRulesEditor(DRCRules& rules, QWidget* parent = nullptr);

signals:
    void rulesChanged();

private slots:
    void onRuleChanged();

private:
    void setupUi();
    void addRuleRow(QVBoxLayout* layout, const QString& label, const QString& tooltip, double value, std::function<void(double)> setter);

    DRCRules& m_rules;
    bool m_blockSignals = false;
};

} // namespace Flux

#endif // PCB_DESIGN_RULES_EDITOR_H
