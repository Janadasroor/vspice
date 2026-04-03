#include "pcb_sync_dialog.h"
#include "../../footprints/footprint_library.h"
#include "../../core/theme_manager.h"
#include <QHeaderView>
#include <QIcon>
#include <QColor>

namespace {
int countFootprintPads(const FootprintDefinition& def) {
    int padCount = 0;
    for (const auto& prim : def.primitives()) {
        if (prim.type == FootprintPrimitive::Pad) {
            ++padCount;
        }
    }
    return padCount;
}
}

PCBSyncDialog::PCBSyncDialog(const ECOPackage& pkg, QWidget* parent)
    : QDialog(parent), m_package(pkg) {
    setupUI();
    populateTable();
}

PCBSyncDialog::~PCBSyncDialog() {
}

void PCBSyncDialog::setupUI() {
    setWindowTitle("Update PCB from Schematic");
    resize(700, 500);
    const PCBTheme* theme = ThemeManager::theme();
    const QColor background = theme ? theme->panelBackground() : QColor("#13131a");
    const QColor textColor = theme ? theme->textColor() : QColor("#e2e8f0");
    const QColor borderColor = theme ? theme->panelBorder() : QColor("#1f2937");
    const QColor accentColor = theme ? theme->accentColor() : QColor("#0ea5e9");
    const QColor accentHover = theme ? theme->accentHover() : QColor("#38bdf8");
    const QString dialogStyle = QString(
        "QDialog { background-color: %1; color: %2; }"
        "QLabel { color: %2; }"
        "QCheckBox { color: %2; font-size: 12px; spacing: 6px; }"
        "QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid %3; border-radius: 3px; background-color: %1; }"
        "QCheckBox::indicator:hover { border-color: %4; }"
        "QCheckBox::indicator:checked { border-color: %5; background-color: %5; image: url(:/icons/check.svg); }"
        "QTableWidget::indicator, QTableView::indicator { width: 14px; height: 14px; border: 1px solid %3; border-radius: 3px; background-color: %1; }"
        "QTableWidget::indicator:hover, QTableView::indicator:hover { border-color: %4; }"
        "QTableWidget::indicator:checked, QTableView::indicator:checked { border-color: %5; background-color: %5; image: url(:/icons/check.svg); }"
    ).arg(background.name(), textColor.name(), borderColor.name(), accentHover.name(), accentColor.name());
    setStyleSheet(dialogStyle);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    QLabel* header = new QLabel("<b>Sync Review</b>: Verify the following changes before applying to PCB.");
    header->setStyleSheet(QString("font-size: 14px; color: %1; font-weight: bold;").arg(textColor.name()));
    mainLayout->addWidget(header);

    m_table = new QTableWidget();
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({"Reference", "Value", "Footprint", "Status"});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->setStyleSheet(QString(
        "QTableWidget { background-color: %1; color: %2; gridline-color: %3; }"
        "QHeaderView::section { background-color: %1; color: %2; padding: 5px; border: 1px solid %3; }"
    ).arg(background.name(), textColor.name(), borderColor.name()));
    mainLayout->addWidget(m_table);

    m_summaryLabel = new QLabel();
    mainLayout->addWidget(m_summaryLabel);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    
    m_cancelBtn = new QPushButton("Cancel");
    m_cancelBtn->setMinimumWidth(100);
    
    m_applyBtn = new QPushButton("Apply Changes");
    m_applyBtn->setMinimumWidth(120);
    const QColor lightApply = theme ? theme->accentHover() : QColor("#7dd3fc");
    m_applyBtn->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: #0b1220; font-weight: bold; border: 1px solid %2; border-radius: 4px; padding: 6px 10px; }"
        "QPushButton:hover { background-color: %2; }"
        "QPushButton:disabled { background-color: #4b5563; color: #d1d5db; border-color: #4b5563; }"
    ).arg(lightApply.name(), accentColor.name()));
    
    connect(m_applyBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    
    btnLayout->addWidget(m_cancelBtn);
    btnLayout->addWidget(m_applyBtn);
    mainLayout->addLayout(btnLayout);
}

void PCBSyncDialog::populateTable() {
    m_table->setRowCount(m_package.components.size());
    int errors = 0;
    int warnings = 0;

    auto& lib = FootprintLibraryManager::instance();

    for (int i = 0; i < m_package.components.size(); ++i) {
        const auto& comp = m_package.components[i];
        
        m_table->setItem(i, 0, new QTableWidgetItem(comp.reference));
        m_table->setItem(i, 1, new QTableWidgetItem(comp.value));
        m_table->setItem(i, 2, new QTableWidgetItem(comp.footprint.isEmpty() ? "<Not Assigned>" : comp.footprint));

        QTableWidgetItem* statusItem = new QTableWidgetItem();
        if (comp.footprint.isEmpty()) {
            statusItem->setText("❌ Error: Missing Footprint");
            statusItem->setForeground(QColor("#ef4444"));
            errors++;
        } else if (!lib.hasFootprint(comp.footprint)) {
            statusItem->setText("⚠️ Warning: Not found in library");
            statusItem->setForeground(QColor("#f59e0b"));
            warnings++;
        } else if (comp.symbolPinCount > 0) {
            const FootprintDefinition fp = lib.findFootprint(comp.footprint);
            const int padCount = countFootprintPads(fp);
            if (padCount != comp.symbolPinCount) {
                statusItem->setText(
                    QString("❌ Error: Pin/Pad mismatch (%1 pins vs %2 pads)")
                        .arg(comp.symbolPinCount)
                        .arg(padCount)
                );
                statusItem->setForeground(QColor("#ef4444"));
                errors++;
            } else {
                statusItem->setText("✅ OK");
                statusItem->setForeground(QColor("#10b981"));
            }
        } else {
            statusItem->setText("✅ OK");
            statusItem->setForeground(QColor("#10b981"));
        }
        m_table->setItem(i, 3, statusItem);
    }

    m_summaryLabel->setText(QString("Summary: %1 components, %2 nets. (%3 errors, %4 warnings)")
                            .arg(m_package.components.size())
                            .arg(m_package.nets.size())
                            .arg(errors)
                            .arg(warnings));

    // Disable apply if there are critical errors
    m_applyBtn->setEnabled(errors == 0);
    if (errors > 0) {
        m_applyBtn->setToolTip("Please fix sync errors (missing footprint or pin/pad mismatch) first.");
    }
}
