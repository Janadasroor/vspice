// batch_edit_dialog.cpp
// Dialog for batch editing component values and properties

#include "batch_edit_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QHeaderView>
#include <QRegularExpression>

BatchEditDialog::BatchEditDialog(const QList<SchematicItem*>& items, QWidget* parent)
    : QDialog(parent)
    , m_items(items)
    , m_tableWidget(nullptr)
    , m_patternEdit(nullptr)
    , m_previewLabel(nullptr)
    , m_statusLabel(nullptr)
    , m_selectAllCheck(nullptr)
    , m_applyBtn(nullptr)
    , m_cancelBtn(nullptr)
    , m_patternModeCombo(nullptr)
    , m_selectedCount(0)
{
    setWindowTitle("Batch Edit Components");
    setMinimumSize(600, 400);
    resize(700, 500);

    setupUI();
    populateTable();
    updatePreview();
}

BatchEditDialog::~BatchEditDialog() {
}

void BatchEditDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Selection info
    m_statusLabel = new QLabel(QString("Editing %1 component(s)").arg(m_items.size()));
    m_statusLabel->setStyleSheet("font-weight: bold; color: #007acc;");
    mainLayout->addWidget(m_statusLabel);

    // Table widget
    m_tableWidget = new QTableWidget();
    m_tableWidget->setColumnCount(4);
    m_tableWidget->setHorizontalHeaderLabels({"✓", "Reference", "Current Value", "New Value"});
    m_tableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableWidget->verticalHeader()->setVisible(false);
    m_tableWidget->setAlternatingRowColors(true);
    m_tableWidget->setStyleSheet(
        "QTableWidget {"
        "    gridline-color: #e0e0e0;"
        "    selection-background-color: #e3f2fd;"
        "}"
        "QTableWidget::item:selected {"
        "    background-color: #bbdefb;"
        "    color: #000000;"
        "}"
    );
    mainLayout->addWidget(m_tableWidget);

    connect(m_tableWidget, &QTableWidget::itemChanged, this, [this]() {
        updatePreview();
    });

    // Pattern section
    QGroupBox* patternGroup = new QGroupBox("Value Pattern");
    QVBoxLayout* patternLayout = new QVBoxLayout(patternGroup);

    QHBoxLayout* patternModeLayout = new QHBoxLayout();
    patternModeLayout->addWidget(new QLabel("Pattern Mode:"));
    m_patternModeCombo = new QComboBox();
    m_patternModeCombo->addItem("Sequential (1k, 2k, 3k...)", "sequential");
    m_patternModeCombo->addItem("Same Value (All get same value)", "same");
    m_patternModeCombo->addItem("Linear Scale (start, start+step...)", "linear");
    m_patternModeCombo->addItem("Custom Pattern", "custom");
    patternModeLayout->addWidget(m_patternModeCombo);
    patternModeLayout->addStretch();
    patternLayout->addLayout(patternModeLayout);

    QHBoxLayout* patternEditLayout = new QHBoxLayout();
    patternEditLayout->addWidget(new QLabel("Pattern:"));
    m_patternEdit = new QLineEdit();
    m_patternEdit->setPlaceholderText("e.g., 1k, 10k, R{index}, {index}k");
    m_patternEdit->setClearButtonEnabled(true);
    patternEditLayout->addWidget(m_patternEdit);
    connect(m_patternEdit, &QLineEdit::textChanged, this, &BatchEditDialog::onPatternTextChanged);
    patternLayout->addLayout(patternEditLayout);

    // Preview
    QHBoxLayout* previewLayout = new QHBoxLayout();
    previewLayout->addWidget(new QLabel("Preview:"));
    m_previewLabel = new QLabel("—");
    m_previewLabel->setStyleSheet("font-family: monospace; background: #f5f5f5; padding: 4px 8px; border-radius: 4px;");
    m_previewLabel->setMinimumWidth(200);
    previewLayout->addWidget(m_previewLabel);
    previewLayout->addStretch();
    patternLayout->addLayout(previewLayout);

    mainLayout->addWidget(patternGroup);

    // Select All
    QHBoxLayout* selectLayout = new QHBoxLayout();
    m_selectAllCheck = new QCheckBox("Select All Items");
    m_selectAllCheck->setChecked(true);
    selectLayout->addWidget(m_selectAllCheck);
    selectLayout->addStretch();
    connect(m_selectAllCheck, &QCheckBox::toggled, this, &BatchEditDialog::onSelectAllToggled);
    mainLayout->addLayout(selectLayout);

    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_cancelBtn = new QPushButton("Cancel");
    m_cancelBtn->setMinimumWidth(100);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    buttonLayout->addWidget(m_cancelBtn);

    m_applyBtn = new QPushButton("Apply Changes");
    m_applyBtn->setMinimumWidth(120);
    m_applyBtn->setDefault(true);
    m_applyBtn->setStyleSheet(
        "QPushButton {"
        "    background-color: #007acc;"
        "    color: white;"
        "    font-weight: bold;"
        "    padding: 8px 16px;"
        "    border-radius: 4px;"
        "}"
        "QPushButton:hover {"
        "    background-color: #005a9e;"
        "}"
    );
    connect(m_applyBtn, &QPushButton::clicked, this, &BatchEditDialog::onApply);
    buttonLayout->addWidget(m_applyBtn);

    mainLayout->addLayout(buttonLayout);
}

void BatchEditDialog::populateTable() {
    m_tableWidget->setRowCount(m_items.size());

    int row = 0;
    for (SchematicItem* item : m_items) {
        // Checkbox
        QCheckBox* check = new QCheckBox();
        check->setChecked(true);
        check->setProperty("row", row);
        connect(check, &QCheckBox::stateChanged, this, [this, row](int state) {
            QTableWidgetItem* refItem = m_tableWidget->item(row, 1);
            if (refItem) {
                refItem->setForeground(state == Qt::Checked ? QColor("#000000") : QColor("#999999"));
            }
            updatePreview();
        });
        QWidget* checkWidget = new QWidget();
        QHBoxLayout* checkLayout = new QHBoxLayout(checkWidget);
        checkLayout->addWidget(check);
        checkLayout->setAlignment(Qt::AlignCenter);
        checkLayout->setContentsMargins(0, 0, 0, 0);
        m_tableWidget->setCellWidget(row, 0, checkWidget);

        // Reference
        QTableWidgetItem* refItem = new QTableWidgetItem(item->referenceDisplayText());
        refItem->setFlags(refItem->flags() & ~Qt::ItemIsEditable);
        m_tableWidget->setItem(row, 1, refItem);

        // Current Value
        QTableWidgetItem* currentValItem = new QTableWidgetItem(item->value());
        currentValItem->setFlags(currentValItem->flags() & ~Qt::ItemIsEditable);
        currentValItem->setForeground(QColor("#666666"));
        m_tableWidget->setItem(row, 2, currentValItem);

        // New Value (editable)
        QTableWidgetItem* newValItem = new QTableWidgetItem(item->value());
        newValItem->setForeground(QColor("#007acc"));
        m_tableWidget->setItem(row, 3, newValItem);

        row++;
    }

    m_tableWidget->setCurrentCell(0, 3);
}

void BatchEditDialog::onPatternTextChanged(const QString& text) {
    updatePreview();
}

void BatchEditDialog::updatePreview() {
    QString pattern = m_patternEdit->text();
    int totalSelected = 0;

    // Count selected items
    for (int row = 0; row < m_items.size(); ++row) {
        QCheckBox* check = qobject_cast<QCheckBox*>(m_tableWidget->cellWidget(row, 0));
        if (check && check->isChecked()) {
            totalSelected++;
        }
    }

    if (pattern.isEmpty() || totalSelected == 0) {
        m_previewLabel->setText("—");
        return;
    }

    // Show first 3 previews
    QStringList previews;
    int shown = 0;
    for (int row = 0; row < m_items.size() && shown < 3; ++row) {
        QCheckBox* check = qobject_cast<QCheckBox*>(m_tableWidget->cellWidget(row, 0));
        if (check && check->isChecked()) {
            QString preview = applyPattern(pattern, shown, totalSelected);
            previews.append(preview);
            shown++;
        }
    }

    QString previewText = previews.join(", ");
    if (totalSelected > 3) {
        previewText += ", ...";
    }

    m_previewLabel->setText(previewText);
}

void BatchEditDialog::onSelectAllToggled(bool checked) {
    for (int row = 0; row < m_items.size(); ++row) {
        QCheckBox* check = qobject_cast<QCheckBox*>(m_tableWidget->cellWidget(row, 0));
        if (check) {
            check->setChecked(checked);
        }
    }
    updatePreview();
}

void BatchEditDialog::onPreviewValueChanged() {
    updatePreview();
}

void BatchEditDialog::onItemSelectionChanged() {
    updatePreview();
}

void BatchEditDialog::onApply() {
    QString pattern = m_patternEdit->text();
    QString mode = m_patternModeCombo->currentData().toString();

    // Collect selected items and their new values
    QList<SchematicItem*> selectedItems;
    QStringList newValues;

    for (int row = 0; row < m_items.size(); ++row) {
        QCheckBox* check = qobject_cast<QCheckBox*>(m_tableWidget->cellWidget(row, 0));
        if (check && check->isChecked()) {
            selectedItems.append(m_items[row]);

            // Get value from table (user might have edited directly)
            QTableWidgetItem* valItem = m_tableWidget->item(row, 3);
            if (valItem && !pattern.isEmpty()) {
                // Apply pattern
                newValues.append(applyPattern(pattern, newValues.size(), selectedItems.size()));
            } else if (valItem) {
                newValues.append(valItem->text());
            }
        }
    }

    if (selectedItems.isEmpty()) {
        QMessageBox::warning(this, "No Selection", "Please select at least one component to edit.");
        return;
    }

    // Apply changes using undo commands
    int appliedCount = 0;
    for (int i = 0; i < selectedItems.size() && i < newValues.size(); ++i) {
        SchematicItem* item = selectedItems[i];
        QString newValue = newValues[i];

        if (item && !newValue.isEmpty()) {
            item->setValue(newValue);
            appliedCount++;
        }
    }

    // Refresh schematic view
    if (appliedCount > 0) {
        QMessageBox::information(this, "Success",
            QString("Successfully updated %1 component(s).").arg(appliedCount));
        accept();
    }
}

QString BatchEditDialog::applyPattern(const QString& pattern, int index, int total) {
    QString mode = m_patternModeCombo->currentData().toString();
    QString result = pattern;

    if (mode == "same") {
        // All items get the same value (remove any index placeholders)
        result.replace("{index}", "");
        result.replace("{n}", "");
        result.replace("{i}", "");
    } else if (mode == "sequential") {
        // Sequential: 1, 2, 3, 4...
        result.replace("{index}", QString::number(index + 1));
        result.replace("{n}", QString::number(index + 1));
        result.replace("{i}", QString::number(index + 1));
    } else if (mode == "linear") {
        // Try to parse start and step from pattern like "1k,10k" or "100,step:50"
        QStringList parts = pattern.split(QRegularExpression("[,;\\s]+"));
        if (parts.size() >= 2) {
            // Extract numeric values
            double start = parts[0].toDouble();
            double step = parts[1].toDouble();
            double value = start + (index * step);

            // Preserve unit suffix
            QRegularExpression unitRx("([kKmMuunpfg])?$", QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch match = unitRx.match(parts[0].trimmed());
            QString unit = match.hasMatch() ? match.captured(1) : "";

            result = QString::number(value) + unit;
        }
    }
    // Custom mode: just use pattern as-is with {index} replacement

    return result.trimmed();
}
