#include "erc_rules_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QHeaderView>
#include <QToolButton>

ERCRulesDialog::ERCRulesDialog(const SchematicERCRules& currentRules, QWidget* parent)
    : QDialog(parent), m_rules(currentRules) {
    
    setWindowTitle("Electrical Rules Matrix");
    resize(800, 600);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QLabel* info = new QLabel("Click on a cell to cycle through severities: OK → Warning → Error → Critical");
    info->setStyleSheet("font-style: italic; color: #888;");
    mainLayout->addWidget(info);

    m_table = new QTableWidget(12, 12, this);
    setupTable();
    mainLayout->addWidget(m_table);

    // Legend
    QHBoxLayout* legend = new QHBoxLayout();
    auto addLegend = [&](const QString& name, SchematicERCRules::RuleResult res) {
        QLabel* color = new QLabel();
        color->setFixedSize(16, 16);
        color->setStyleSheet(QString("background-color: %1; border: 1px solid #444;").arg(severityToColor(res).name()));
        legend->addWidget(color);
        legend->addWidget(new QLabel(name));
        legend->addSpacing(10);
    };
    addLegend("OK", SchematicERCRules::OK);
    addLegend("Warning", SchematicERCRules::Warning);
    addLegend("Error", SchematicERCRules::Error);
    addLegend("Critical", SchematicERCRules::Critical);
    legend->addStretch();
    mainLayout->addLayout(legend);

    QHBoxLayout* buttons = new QHBoxLayout();
    QPushButton* resetBtn = new QPushButton("Reset to Defaults");
    connect(resetBtn, &QPushButton::clicked, this, &ERCRulesDialog::onResetDefaults);
    buttons->addWidget(resetBtn);
    buttons->addStretch();

    QPushButton* cancelBtn = new QPushButton("Cancel");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(cancelBtn);

    QPushButton* okBtn = new QPushButton("Apply Rules");
    okBtn->setDefault(true);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(okBtn);

    mainLayout->addLayout(buttons);

    connect(m_table, &QTableWidget::cellClicked, this, &ERCRulesDialog::onCellClicked);
}

void ERCRulesDialog::setupTable() {
    QStringList headers;
    for (int i = 0; i < 12; ++i) {
        headers << typeToName(i);
    }
    m_table->setHorizontalHeaderLabels(headers);
    m_table->setVerticalHeaderLabels(headers);
    
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    
    for (int r = 0; r < 12; ++r) {
        for (int c = 0; c < 12; ++c) {
            updateCell(r, c);
        }
    }
}

void ERCRulesDialog::updateCell(int row, int col) {
    SchematicERCRules::RuleResult res = m_rules.getRule(static_cast<SchematicItem::PinElectricalType>(row), 
                                                       static_cast<SchematicItem::PinElectricalType>(col));
    
    QTableWidgetItem* item = m_table->item(row, col);
    if (!item) {
        item = new QTableWidgetItem();
        m_table->setItem(row, col, item);
    }
    
    item->setBackground(severityToColor(res));
    item->setText(severityToIcon(res));
    item->setTextAlignment(Qt::AlignCenter);
    
    QString tooltip = QString("%1 connected to %2: %3")
                        .arg(typeToName(row), typeToName(col), 
                             (res == SchematicERCRules::OK ? "OK" : 
                              (res == SchematicERCRules::Warning ? "Warning" : 
                               (res == SchematicERCRules::Error ? "Error" : "Critical"))));
    item->setToolTip(tooltip);
}

void ERCRulesDialog::onCellClicked(int row, int col) {
    SchematicItem::PinElectricalType t1 = static_cast<SchematicItem::PinElectricalType>(row);
    SchematicItem::PinElectricalType t2 = static_cast<SchematicItem::PinElectricalType>(col);
    
    int current = static_cast<int>(m_rules.getRule(t1, t2));
    int next = (current + 1) % 4;
    
    m_rules.setRule(t1, t2, static_cast<SchematicERCRules::RuleResult>(next));
    
    updateCell(row, col);
    if (row != col) updateCell(col, row); // Mirror
}

void ERCRulesDialog::onResetDefaults() {
    m_rules = SchematicERCRules::defaultRules();
    for (int r = 0; r < 12; ++r) {
        for (int c = 0; c < 12; ++c) {
            updateCell(r, c);
        }
    }
}

SchematicERCRules ERCRulesDialog::getRules() const {
    return m_rules;
}

QString ERCRulesDialog::typeToName(int type) const {
    switch (type) {
        case 0: return "Passive";
        case 1: return "Input";
        case 2: return "Output";
        case 3: return "BiDi";
        case 4: return "TriState";
        case 5: return "Free";
        case 6: return "Unspec";
        case 7: return "Pwr In";
        case 8: return "Pwr Out";
        case 9: return "OC";
        case 10: return "OE";
        case 11: return "NC";
        default: return "?";
    }
}

QColor ERCRulesDialog::severityToColor(SchematicERCRules::RuleResult res) const {
    switch (res) {
        case SchematicERCRules::OK: return QColor(40, 40, 40);
        case SchematicERCRules::Warning: return QColor(180, 150, 0, 180);
        case SchematicERCRules::Error: return QColor(200, 50, 50, 180);
        case SchematicERCRules::Critical: return QColor(255, 0, 0, 220);
        default: return Qt::transparent;
    }
}

QString ERCRulesDialog::severityToIcon(SchematicERCRules::RuleResult res) const {
    switch (res) {
        case SchematicERCRules::OK: return "•";
        case SchematicERCRules::Warning: return "!";
        case SchematicERCRules::Error: return "X";
        case SchematicERCRules::Critical: return "!!!";
        default: return "";
    }
}
