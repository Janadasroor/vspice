#include "component_properties_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QFormLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QHeaderView>
#include <QMessageBox>
#include "../../core/theme_manager.h"
#include "../../symbols/symbol_library.h"
#include "../../symbols/models/symbol_definition.h"
#include "../items/schematic_sheet_item.h"
#include "visual_spice_mapper_dialog.h"

using Flux::Model::SymbolDefinition;

ComponentPropertiesDialog::ComponentPropertiesDialog(const QList<SchematicItem*>& items, QWidget* parent)
    : QDialog(parent), m_items(items) {
    if (items.isEmpty()) return;

    if (items.size() == 1) {
        setWindowTitle(QString("Properties - %1 %2").arg(items.first()->reference()).arg(items.first()->name()));
    } else {
        setWindowTitle(QString("Properties - %1 items selected").arg(items.size()));
    }
    
    resize(480, 560);
    
    setupUI();
    loadFromItem();
    
    // Premium styling
    setStyleSheet(
        "QDialog { background-color: #1e1e1e; color: #e0e0e0; font-family: 'Inter', 'Segoe UI', sans-serif; }"
        "QTabWidget::pane { border: 1px solid #333; background: #1a1a1a; border-radius: 4px; }"
        "QTabBar::tab { background: #252525; padding: 12px 24px; border-top-left-radius: 6px; border-top-right-radius: 6px; margin-right: 4px; color: #999; font-weight: 600; font-size: 11px; }"
        "QTabBar::tab:selected { background: #333; border-bottom: 3px solid #007acc; color: #fff; }"
        "QTabBar::tab:hover { background: #2d2d2d; color: #ccc; }"
        "QLabel { color: #aaaaaa; font-weight: 500; font-size: 12px; }"
        "QLineEdit, QTextEdit { background: #2d2d2d; border: 1px solid #3d3d3d; border-radius: 4px; color: #e0e0e0; padding: 8px 12px; font-size: 13px; min-height: 24px; }"
        "QLineEdit:focus, QTextEdit:focus { border: 1px solid #007acc; background: #333; }"
        "QLineEdit:read-only { background: #222222; color: #777; }"
        "QPushButton { background: #3d3d3d; color: #e0e0e0; border: none; padding: 10px 20px; border-radius: 4px; font-weight: 600; font-size: 12px; }"
        "QPushButton:hover { background: #4d4d4d; }"
        "QPushButton:pressed { background: #2d2d2d; }"
        "QPushButton#okButton { background: #007acc; color: white; }"
        "QPushButton#okButton:hover { background: #008be5; }"
    );
}

ComponentPropertiesDialog::~ComponentPropertiesDialog() {}

void ComponentPropertiesDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0); // Remove outer margins to let scroll area/header handle it
    mainLayout->setSpacing(0);

    // Header with Title
    QWidget* headerWidget = new QWidget(this);
    headerWidget->setFixedHeight(50);
    headerWidget->setStyleSheet("background-color: #1a1a1a; border-bottom: 2px solid #007acc;");
    QHBoxLayout* headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(20, 0, 20, 0);
    
    QLabel* titleLabel = new QLabel(windowTitle().toUpper());
    titleLabel->setStyleSheet("color: white; font-weight: 800; font-size: 11px; letter-spacing: 1px; border: none; background: transparent;");
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    mainLayout->addWidget(headerWidget);

    // Scroll Area for content
    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet("background: #1e1e1e; border: none;");
    
    QWidget* contentWidget = new QWidget();
    QVBoxLayout* contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(20, 20, 20, 20);
    contentLayout->setSpacing(15);

    QTabWidget* tabs = new QTabWidget(this);

    // --- General Tab ---
    QWidget* generalTab = new QWidget();
    QFormLayout* generalLayout = new QFormLayout(generalTab);
    generalLayout->setContentsMargins(20, 20, 20, 20);
    generalLayout->setSpacing(15);
    generalLayout->setLabelAlignment(Qt::AlignRight);

    m_refEdit = new QLineEdit();
    m_valEdit = new QLineEdit();
    m_nameEdit = new QLineEdit();
    m_nameEdit->setReadOnly(true);
    m_fileEdit = new QLineEdit();

    generalLayout->addRow("Designator:", m_refEdit);
    generalLayout->addRow("Value:", m_valEdit);
    generalLayout->addRow("Symbol Name:", m_nameEdit);
    
    m_excludeSimCheck = new QCheckBox("Exclude from Simulation");
    const QString checkStyle =
        "QCheckBox { color: #cccccc; font-size: 12px; spacing: 8px; }"
        "QCheckBox::indicator {"
        "   width: 14px;"
        "   height: 14px;"
        "   border: 1px solid #666666;"
        "   border-radius: 2px;"
        "   background-color: #1a1a1a;"
        "}"
        "QCheckBox::indicator:hover { border-color: #007acc; }"
        "QCheckBox::indicator:checked {"
        "   border-color: #007acc;"
        "   background-color: #007acc;"
        "   image: url(:/icons/check.svg);"
        "}";
    m_excludeSimCheck->setStyleSheet(checkStyle);
    
    generalLayout->addRow("", m_excludeSimCheck);

    // Only show sheet file if we have a sheet in selection
    bool hasSheet = false;
    for (auto* item : m_items) {
        if (item->itemType() == SchematicItem::SheetType) {
            hasSheet = true;
            break;
        }
    }
    
    QLabel* fileLabel = new QLabel("Sheet File:");
    if (!hasSheet) {
        fileLabel->hide();
        m_fileEdit->hide();
    }
    generalLayout->addRow(fileLabel, m_fileEdit);

    tabs->addTab(generalTab, "General");

    // --- SIMULATION Tab ---
    QWidget* simTab = new QWidget();
    QVBoxLayout* simLayout = new QVBoxLayout(simTab);
    simLayout->setContentsMargins(20, 20, 20, 20);
    simLayout->setSpacing(15);
    
    QLabel* simTitle = new QLabel("SPICE SIMULATION MAPPING");
    simTitle->setStyleSheet("font-weight: bold; color: #ffffff;");
    simLayout->addWidget(simTitle);
    
    QLabel* simDesc = new QLabel("Map symbol pins to SPICE model nodes for accurate simulation (especially for transistors and op-amps).");
    simDesc->setWordWrap(true);
    simLayout->addWidget(simDesc);
    
    m_spiceInfoLabel = new QLabel("No mapping defined. Defaulting to alphabetical pin order.");
    m_spiceInfoLabel->setStyleSheet("color: #ffcc00; font-size: 11px; font-style: italic;");
    simLayout->addWidget(m_spiceInfoLabel);
    
    m_spiceMapperBtn = new QPushButton("Launch Visual SPICE Mapper...");
    m_spiceMapperBtn->setIcon(QIcon(":/icons/tool_probe.svg"));
    m_spiceMapperBtn->setStyleSheet("background: #007acc; color: white; border-radius: 4px; padding: 12px; font-weight: bold;");
    simLayout->addWidget(m_spiceMapperBtn);
    
    simLayout->addStretch();
    
    tabs->addTab(simTab, "Simulation");

    contentLayout->addWidget(tabs);
    scrollArea->setWidget(contentWidget);
    mainLayout->addWidget(scrollArea);

    // Buttons
    QFrame* separator = new QFrame();
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Plain);
    separator->setStyleSheet("color: #333;");
    mainLayout->addWidget(separator);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->setContentsMargins(20, 15, 20, 20);
    btnLayout->setSpacing(12);
    btnLayout->addStretch();
    
    m_okBtn = new QPushButton("Apply Changes");
    m_okBtn->setObjectName("okButton");
    m_cancelBtn = new QPushButton("Cancel");
    
    btnLayout->addWidget(m_cancelBtn);
    btnLayout->addWidget(m_okBtn);
    
    mainLayout->addLayout(btnLayout);

    connect(m_okBtn, &QPushButton::clicked, this, &ComponentPropertiesDialog::onAccept);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_spiceMapperBtn, &QPushButton::clicked, this, &ComponentPropertiesDialog::onEditSpiceMapping);
}

void ComponentPropertiesDialog::loadFromItem() {
    auto getCommonValue = [&](auto getter) -> QString {
        if (m_items.isEmpty()) return "";
        QString val = getter(m_items.first());
        for (int i = 1; i < m_items.size(); ++i) {
            if (getter(m_items[i]) != val) return "<multiple>";
        }
        return val;
    };

    m_refEdit->setText(getCommonValue([](SchematicItem* it){ return it->reference(); }));
    m_valEdit->setText(getCommonValue([](SchematicItem* it){ return it->value(); }));
    m_nameEdit->setText(getCommonValue([](SchematicItem* it){ return it->name(); }));
    
    m_fileEdit->setText(getCommonValue([](SchematicItem* it){
        if (auto* sheet = dynamic_cast<SchematicSheetItem*>(it)) return sheet->fileName();
        return QString("");
    }));
    
    // Exclude flags
    auto getCommonBool = [&](auto getter) -> bool {
        if (m_items.isEmpty()) return false;
        bool val = getter(m_items.first());
        for (int i = 1; i < m_items.size(); ++i) {
            if (getter(m_items[i]) != val) return false; // Indeterminate or mixed
        }
        return val;
    };
    m_excludeSimCheck->setChecked(getCommonBool([](SchematicItem* it){ return it->excludeFromSimulation(); }));

    // Check for existing SPICE mapping
    QString symName = getCommonValue([](SchematicItem* it){ return it->name(); });
    if (!symName.isEmpty() && symName != "<multiple>") {
        SymbolDefinition* sym = SymbolLibraryManager::instance().findSymbol(symName);
        if (sym) {
            if (!sym->spiceNodeMapping().isEmpty()) {
                m_spiceInfoLabel->setText(QString("Custom SPICE mapping: %1 nodes mapped.").arg(sym->spiceNodeMapping().size()));
                m_spiceInfoLabel->setStyleSheet("color: #569cd6; font-size: 11px; font-style: italic;");
            }
        }
    }

    if (m_items.size() > 1) {
        m_refEdit->setPlaceholderText("Multiple values");
        m_valEdit->setPlaceholderText("Multiple values");
    }
}


void ComponentPropertiesDialog::onEditSpiceMapping() {
    if (m_items.isEmpty()) return;
    
    QString symName = m_items.first()->name();
    if (symName.isEmpty()) return;
    
    SymbolDefinition* sym = SymbolLibraryManager::instance().findSymbol(symName);
    if (!sym) {
        QMessageBox::warning(this, "SPICE Mapping", "Could not find symbol definition in library.");
        return;
    }
    
    VisualSpiceMapperDialog dialog(*sym, this);
    if (dialog.exec() == QDialog::Accepted) {
        // Save back to symbol
        sym->setSpiceNodeMapping(dialog.nodeMapping());
        sym->setSpiceModelName(dialog.spiceModelName());
        
        // Refresh UI
        m_spiceInfoLabel->setText(QString("Custom SPICE mapping saved: %1 nodes mapped.").arg(dialog.nodeMapping().size()));
        m_spiceInfoLabel->setStyleSheet("color: #569cd6; font-size: 11px; font-style: italic;");
    }
}

void ComponentPropertiesDialog::onAccept() {
    // Basic validation or bulk apply logic
    accept();
}

QString ComponentPropertiesDialog::reference() const { return m_refEdit->text(); }
QString ComponentPropertiesDialog::value() const { return m_valEdit->text(); }
QString ComponentPropertiesDialog::fileName() const { return m_fileEdit->text(); }
bool ComponentPropertiesDialog::excludeFromSim() const { return m_excludeSimCheck->isChecked(); }
