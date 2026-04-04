#ifndef DESIGN_REPORT_DIALOG_H
#define DESIGN_REPORT_DIALOG_H

#include <QDialog>
#include <QCheckBox>
#include <QLineEdit>
#include <QComboBox>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include "design_report_generator.h"

class QGraphicsScene;

class DesignReportDialog : public QDialog {
    Q_OBJECT

public:
    explicit DesignReportDialog(QGraphicsScene* scene, QWidget* parent = nullptr);
    ~DesignReportDialog();

private slots:
    void onGenerate();
    void onPreview();
    void onBrowse();
    void onFormatChanged();

private:
    void setupUI();
    void updatePreview();
    DesignReportGenerator::ReportOptions collectOptions();

    QGraphicsScene* m_scene;

    // Options
    QComboBox* m_formatCombo;
    QLineEdit* m_filePathEdit;
    QPushButton* m_browseBtn;
    QLineEdit* m_companyEdit;
    QLineEdit* m_projectEdit;

    // Sections
    QCheckBox* m_includeStats;
    QCheckBox* m_includeLayers;
    QCheckBox* m_includeDRC;
    QCheckBox* m_includeNets;
    QCheckBox* m_includeComponents;
    QCheckBox* m_includeNetClasses;
    QCheckBox* m_includeBOM;

    // Preview
    QTextEdit* m_previewEdit;
    QLabel* m_statusLabel;

    QPushButton* m_previewBtn;
    QPushButton* m_generateBtn;
    QPushButton* m_closeBtn;
};

#endif // DESIGN_REPORT_DIALOG_H
