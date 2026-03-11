#include "selection_filter_widget.h"
#include <QLabel>
#include <QHBoxLayout>
#include <QGridLayout>

SelectionFilterWidget::SelectionFilterWidget(QWidget *parent)
    : QWidget(parent) {
    setupUi();
}

void SelectionFilterWidget::setupUi() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Header label
    QLabel* header = new QLabel("   SELECTION FILTER", this);
    header->setFixedHeight(28);
    header->setStyleSheet(
        "background-color: #1a1a1a;"
        "color: #555555;"
        "font-size: 10px;"
        "font-weight: 700;"
        "border-bottom: 1px solid #2d2d2d;"
        "border-top: 1px solid #2d2d2d;"
    );
    layout->addWidget(header);

    QWidget* gridContainer = new QWidget(this);
    gridContainer->setStyleSheet("background-color: #121212;");
    QGridLayout* grid = new QGridLayout(gridContainer);
    grid->setContentsMargins(15, 12, 15, 12);
    grid->setSpacing(10);

    auto addFilter = [this, grid](const QString& label, int row, int col, bool checked = true) {
        QCheckBox* check = new QCheckBox(label);
        check->setChecked(checked);
        check->setStyleSheet(R"(
            QCheckBox {
                color: #a1a1aa;
                font-size: 11px;
                background: transparent;
            }
            QCheckBox::indicator {
                width: 14px;
                height: 14px;
                border: 1px solid #333333;
                border-radius: 2px;
                background: #1a1a1a;
            }
            QCheckBox::indicator:checked {
                background: #007acc;
                border-color: #007acc;
                image: url(:/icons/check.svg);
            }
            QCheckBox:hover {
                color: #ffffff;
            }
        )");
        grid->addWidget(check, row, col);
        m_filters[label] = check;
        connect(check, &QCheckBox::toggled, this, &SelectionFilterWidget::filterChanged);
    };

    // Row 1
    addFilter("Symbols", 0, 0);
    addFilter("Wires", 0, 1);
    
    // Row 2
    addFilter("Bus/Entry", 1, 0);
    addFilter("Labels", 1, 1);

    // Row 3 (for PCB also)
    addFilter("Traces", 2, 0);
    addFilter("Pads/Vias", 2, 1);

    // Row 4 (PCB-specific filters)
    addFilter("Pours", 3, 0);
    addFilter("Ratsnest", 3, 1);

    // Row 5 (PCB layer selection filter)
    addFilter("Active Layer", 4, 0, false);

    layout->addWidget(gridContainer);
}

bool SelectionFilterWidget::isFilterEnabled(const QString& type) const {
    if (m_filters.contains(type)) {
        return m_filters[type]->isChecked();
    }
    return true;
}

void SelectionFilterWidget::setFilterEnabled(const QString& type, bool enabled) {
    if (m_filters.contains(type)) {
        m_filters[type]->setChecked(enabled);
    }
}
