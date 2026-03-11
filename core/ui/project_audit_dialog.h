#ifndef PROJECTAUDITDIALOG_H
#define PROJECTAUDITDIALOG_H

#include <QDialog>
#include <QTreeWidget>
#include <QPushButton>
#include <QLabel>
#include <QTimer>

class ProjectAuditDialog : public QDialog {
    Q_OBJECT

public:
    explicit ProjectAuditDialog(QWidget *parent = nullptr);
    ~ProjectAuditDialog();

private slots:
    void refreshStats();
    void runFullAudit();

private:
    void setupUi();
    void applyDarkTheme();
    
    // Stats gathering
    void updateMemoryStats();
    void updateSolverStats();
    void updateErcDrcSummary();

    QTreeWidget* m_statsTree;
    QLabel* m_statusLabel;
    QTimer* m_refreshTimer;
    
    // Category items
    QTreeWidgetItem* m_memRoot;
    QTreeWidgetItem* m_solverRoot;
    QTreeWidgetItem* m_healthRoot;
};

#endif // PROJECTAUDITDIALOG_H
