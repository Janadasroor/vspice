#ifndef ERC_DIAGNOSTICS_PANEL_H
#define ERC_DIAGNOSTICS_PANEL_H

#include <QWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include "../analysis/schematic_erc.h"

class ERCDiagnosticsPanel : public QWidget {
    Q_OBJECT

public:
    explicit ERCDiagnosticsPanel(QWidget* parent = nullptr);

    void setViolations(const QList<ERCViolation>& violations);
    void clear();

signals:
    void violationSelected(const ERCViolation& violation);
    void ignoreRequested(const ERCViolation& violation);
    void clearIgnoredRequested();
    void aiFixRequested(const QString& violationsSummary);

private slots:
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onFilterChanged();
    void onCustomContextMenu(const QPoint& pos);

private:
    void updateList();
    QIcon iconForSeverity(ERCViolation::Severity severity);
    QColor colorForSeverity(ERCViolation::Severity severity);

    QTreeWidget* m_treeWidget;
    QComboBox* m_severityFilter;
    QList<ERCViolation> m_violations;
};

#endif // ERC_DIAGNOSTICS_PANEL_H
