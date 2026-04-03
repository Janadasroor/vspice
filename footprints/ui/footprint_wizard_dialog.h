#ifndef FOOTPRINT_WIZARD_DIALOG_H
#define FOOTPRINT_WIZARD_DIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QFormLayout>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QListWidget>
#include "footprint_wizard.h"

class FootprintEditor;

/**
 * @brief Footprint Wizard Dialog with live preview.
 * 
 * Allows users to select a standard package type, adjust parameters,
 * preview the footprint, and save directly to a library.
 */
class FootprintWizardDialog : public QDialog {
    Q_OBJECT

public:
    explicit FootprintWizardDialog(FootprintEditor* editor, QWidget* parent = nullptr);
    ~FootprintWizardDialog();

private slots:
    void onPackageSelected(int index);
    void onParamChanged();
    void onGenerate();
    void onSaveToLibrary();

private:
    void setupUI();
    void updatePreview();
    void populatePackageList();
    void updateFormFields();
    FootprintWizard::WizardParams collectParams();

    FootprintEditor* m_editor;
    QGraphicsScene* m_previewScene;
    QGraphicsView* m_previewView;
    QList<QGraphicsItem*> m_previewItems;

    // Package selection
    QListWidget* m_packageList;

    // Parameter form
    QSpinBox* m_pinCountSpin;
    QDoubleSpinBox* m_pitchSpin;
    QDoubleSpinBox* m_padWidthSpin;
    QDoubleSpinBox* m_padHeightSpin;
    QDoubleSpinBox* m_bodyWidthSpin;
    QDoubleSpinBox* m_bodyHeightSpin;
    QDoubleSpinBox* m_rowSpanSpin;
    QDoubleSpinBox* m_drillSizeSpin;
    QDoubleSpinBox* m_courtyardExtraSpin;
    QLineEdit* m_nameEdit;

    // Preview
    QLabel* m_summaryLabel;

    // Buttons
    QPushButton* m_generateBtn;
    QPushButton* m_saveBtn;
    QPushButton* m_closeBtn;
};

#endif // FOOTPRINT_WIZARD_DIALOG_H
