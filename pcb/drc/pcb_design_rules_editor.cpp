#include "pcb_design_rules_editor.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QGroupBox>
#include <QScrollArea>
#include <QFrame>

namespace Flux {

PCBDesignRulesEditor::PCBDesignRulesEditor(DRCRules& rules, QWidget* parent)
    : QWidget(parent), m_rules(rules) {
    setupUi();
}

void PCBDesignRulesEditor::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    setStyleSheet("background: #09090b;");

    // Header
    QWidget* header = new QWidget();
    header->setFixedHeight(60);
    header->setStyleSheet("background: #18181b; border-bottom: 1px solid #27272a;");
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    
    QLabel* title = new QLabel("DESIGN RULES");
    title->setStyleSheet("color: #ffffff; font-weight: 800; font-size: 12px; letter-spacing: 1.5px;");
    headerLayout->addWidget(title);
    headerLayout->addStretch();
    
    mainLayout->addWidget(header);

    // Scroll Area
    QScrollArea* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("background: transparent;");
    
    QWidget* content = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(content);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(20);

    // Category: Clearances
    QGroupBox* clearanceGroup = new QGroupBox("Clearances & Distances");
    clearanceGroup->setStyleSheet("QGroupBox { color: #3b82f6; font-weight: bold; border: 1px solid #27272a; margin-top: 15px; padding-top: 15px; }");
    QVBoxLayout* cLayout = new QVBoxLayout(clearanceGroup);
    addRuleRow(cLayout, "Minimum Clearance", "Minimum distance between different copper nets.", m_rules.minClearance(), [this](double v) { m_rules.setMinClearance(v); });
    addRuleRow(cLayout, "Copper to Edge", "Minimum distance from copper to the board edge.", m_rules.copperToEdge(), [this](double v) { m_rules.setCopperToEdge(v); });
    layout->addWidget(clearanceGroup);

    // Category: Traces
    QGroupBox* traceGroup = new QGroupBox("Trace Constraints");
    traceGroup->setStyleSheet("QGroupBox { color: #10b981; font-weight: bold; border: 1px solid #27272a; margin-top: 15px; padding-top: 15px; }");
    QVBoxLayout* tLayout = new QVBoxLayout(traceGroup);
    addRuleRow(tLayout, "Min Trace Width", "Minimum width for any copper trace.", m_rules.minTraceWidth(), [this](double v) { m_rules.setMinTraceWidth(v); });
    addRuleRow(tLayout, "Max Trace Width", "Maximum allowed width for signal traces.", m_rules.maxTraceWidth(), [this](double v) { m_rules.setMaxTraceWidth(v); });
    layout->addWidget(traceGroup);

    // Category: Vias & Drills
    QGroupBox* viaGroup = new QGroupBox("Via & Drill Constraints");
    viaGroup->setStyleSheet("QGroupBox { color: #f59e0b; font-weight: bold; border: 1px solid #27272a; margin-top: 15px; padding-top: 15px; }");
    QVBoxLayout* vLayout = new QVBoxLayout(viaGroup);
    addRuleRow(vLayout, "Min Via Diameter", "Minimum outer diameter for vias.", m_rules.minViaDiameter(), [this](double v) { m_rules.setMinViaDiameter(v); });
    addRuleRow(vLayout, "Min Via Drill", "Minimum drill hole size for vias.", m_rules.minViaDrill(), [this](double v) { m_rules.setMinViaDrill(v); });
    addRuleRow(vLayout, "Min Hole Size", "Minimum hole size for any pad or via.", m_rules.minDrillSize(), [this](double v) { m_rules.setMinDrillSize(v); });
    layout->addWidget(viaGroup);

    // Category: Manufacturing
    QGroupBox* mfgGroup = new QGroupBox("Manufacturing Rules");
    mfgGroup->setStyleSheet("QGroupBox { color: #ec4899; font-weight: bold; border: 1px solid #27272a; margin-top: 15px; padding-top: 15px; }");
    QVBoxLayout* mLayout = new QVBoxLayout(mfgGroup);
    addRuleRow(mLayout, "Silk to Pad", "Minimum clearance between silkscreen and pads.", m_rules.silkToPad(), [this](double v) { m_rules.setSilkToPad(v); });
    layout->addWidget(mfgGroup);

    layout->addStretch();
    scroll->setWidget(content);
    mainLayout->addWidget(scroll);
}

void PCBDesignRulesEditor::addRuleRow(QVBoxLayout* layout, const QString& label, const QString& tooltip, double value, std::function<void(double)> setter) {
    QWidget* row = new QWidget();
    QHBoxLayout* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 4, 0, 4);

    QLabel* lbl = new QLabel(label);
    lbl->setToolTip(tooltip);
    lbl->setStyleSheet("color: #a1a1aa; font-size: 12px;");

    QDoubleSpinBox* spin = new QDoubleSpinBox();
    spin->setRange(0.0, 100.0);
    spin->setDecimals(3);
    spin->setSuffix(" mm");
    spin->setValue(value);
    spin->setStyleSheet(
        "QDoubleSpinBox {"
        "   background: #18181b; color: #fff; border: 1px solid #27272a;"
        "   border-radius: 4px; padding: 4px 8px; font-size: 12px;"
        "}"
    );

    connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, setter = std::move(setter)](double val) mutable {
        if (m_blockSignals) return;
        setter(val);
        emit rulesChanged();
    });

    rowLayout->addWidget(lbl, 1);
    rowLayout->addWidget(spin, 0);
    layout->addWidget(row);
}

void PCBDesignRulesEditor::onRuleChanged() {
    // Placeholder for internal logic
}

} // namespace Flux
