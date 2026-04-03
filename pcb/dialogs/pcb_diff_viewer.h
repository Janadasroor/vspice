#ifndef PCB_DIFF_VIEWER_H
#define PCB_DIFF_VIEWER_H

#include <QDialog>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QTabWidget>
#include <QTextEdit>
#include "../analysis/pcb_diff_engine.h"

class QGraphicsScene;

/**
 * @brief Visual diff viewer for comparing two PCB boards.
 * 
 * Shows differences in three views:
 * 1. Table list of all differences (clickable to zoom)
 * 2. Statistics summary
 * 3. JSON diff report
 */
class PCBDiffViewer : public QDialog {
    Q_OBJECT

public:
    explicit PCBDiffViewer(const DiffReport& report, QGraphicsScene* sceneA, QGraphicsScene* sceneB,
                           QWidget* parent = nullptr);
    ~PCBDiffViewer();

private slots:
    void onDiffEntryClicked(QTableWidgetItem* item);
    void onCategoryFilterChanged(int index);
    void onSaveReport();

private:
    void setupUI();
    void populateDiffTable();
    void populateStatsTab();
    void populateReportTab();
    void highlightDifference(const DiffEntry& entry);

    DiffReport m_report;
    QGraphicsScene* m_sceneA;
    QGraphicsScene* m_sceneB;

    QTabWidget* m_tabs;

    // Table tab
    QTableWidget* m_diffTable;
    QComboBox* m_categoryFilter;
    QLabel* m_summaryLabel;

    // Stats tab
    QTextEdit* m_statsEdit;

    // Report tab
    QTextEdit* m_reportEdit;

    QPushButton* m_saveBtn;
    QPushButton* m_closeBtn;
};

#endif // PCB_DIFF_VIEWER_H
