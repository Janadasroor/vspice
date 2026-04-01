#ifndef BATCH_EDIT_DIALOG_H
#define BATCH_EDIT_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include "../items/schematic_item.h"

/**
 * @brief Dialog for batch editing component values and properties.
 * Allows editing multiple selected components simultaneously.
 */
class BatchEditDialog : public QDialog {
    Q_OBJECT

public:
    explicit BatchEditDialog(const QList<SchematicItem*>& items, QWidget* parent = nullptr);
    ~BatchEditDialog();

    // Get the list of items to edit
    QList<SchematicItem*> selectedItems() const { return m_items; }

private slots:
    void onApply();
    void onPatternTextChanged(const QString& text);
    void onPreviewValueChanged();
    void onSelectAllToggled(bool checked);
    void onItemSelectionChanged();

private:
    void setupUI();
    void populateTable();
    QString applyPattern(const QString& pattern, int index, int total);
    void updatePreview();
    QStringList parseValuePattern(const QString& pattern);

    QList<SchematicItem*> m_items;

    // UI Widgets
    QTableWidget* m_tableWidget;
    QLineEdit* m_patternEdit;
    QLabel* m_previewLabel;
    QLabel* m_statusLabel;
    QCheckBox* m_selectAllCheck;
    QPushButton* m_applyBtn;
    QPushButton* m_cancelBtn;

    // Pattern modes
    QComboBox* m_patternModeCombo;

    int m_selectedCount;
};

#endif // BATCH_EDIT_DIALOG_H
