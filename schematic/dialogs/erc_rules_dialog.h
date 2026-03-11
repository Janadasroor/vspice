#ifndef ERC_RULES_DIALOG_H
#define ERC_RULES_DIALOG_H

#include <QDialog>
#include <QTableWidget>
#include "../analysis/schematic_erc_rules.h"

class ERCRulesDialog : public QDialog {
    Q_OBJECT

public:
    explicit ERCRulesDialog(const SchematicERCRules& currentRules, QWidget* parent = nullptr);
    SchematicERCRules getRules() const;

private slots:
    void onCellClicked(int row, int col);
    void onResetDefaults();

private:
    void setupTable();
    void updateCell(int row, int col);
    QString typeToName(int type) const;
    QColor severityToColor(SchematicERCRules::RuleResult res) const;
    QString severityToIcon(SchematicERCRules::RuleResult res) const;

    QTableWidget* m_table;
    SchematicERCRules m_rules;
};

#endif // ERC_RULES_DIALOG_H
