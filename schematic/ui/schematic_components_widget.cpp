#include "schematic_components_widget.h"
#include "model_browser_widget.h"
#include "../../core/theme_manager.h"
#include "../../symbols/symbol_library.h"
#include "../../symbols/symbol_editor.h"
#include "../../symbols/models/symbol_definition.h"
#include "library_browser_dialog.h"
#include <QPushButton>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QTabWidget>

// ─── Icon helper ────────────────────────────────────────────────────────────
QIcon SchematicComponentsWidget::createComponentIcon(const QString& name) {
    QString n = name.toLower();
    QString path = ":/icons/component_file.svg";
    if (n.contains("resistor")) path = ":/icons/comp_resistor.svg";
    else if (n.contains("capacitor")) path = ":/icons/comp_capacitor.svg";
    else if (n.contains("inductor")) path = ":/icons/comp_inductor.svg";
    else if (n.contains("diode")) path = ":/icons/comp_diode.svg";
    else if (n.contains("transistor")) path = ":/icons/comp_transistor.svg";
    else if (n.contains("opamp")) path = ":/icons/comp_opamp.svg";
    else if (n.contains("voltmeter")) path = ":/icons/tool_voltmeter.svg";
    else if (n.contains("ammeter") || n.contains("current probe") || n.contains("current_probe")) path = ":/icons/tool_ammeter.svg";
    else if (n.contains("oscilloscope") || n.contains("logic analyzer")) path = ":/icons/tool_oscilloscope.svg";
    else if (n.contains("power meter") || n.contains("power_probe") || n.contains("power probe")) path = ":/icons/tool_power_meter.svg";
    else if (n.contains("probe")) path = ":/icons/tool_probe.svg";
    else if (n.contains("gate")) path = ":/icons/comp_logic.svg";
    else if (n.contains("ic") || n.contains("ram") || n.contains("smart signal block")) path = ":/icons/comp_ic.svg";
    else if (n.contains("gnd")) path = ":/icons/comp_gnd.svg";
    else if (n.contains("vcc") || n.contains("vdd") || n.contains("5v") || n.contains("3.3v") || n.contains("12v") || n.contains("vbat")) path = ":/icons/comp_vcc.svg";
    
    QIcon icon(path);
    PCBTheme* theme = ThemeManager::theme();
    if (theme && theme->type() == PCBTheme::Light) {
        QPixmap pixmap = icon.pixmap(QSize(32, 32));
        if (!pixmap.isNull()) {
            QPainter p(&pixmap);
            p.setCompositionMode(QPainter::CompositionMode_SourceIn);
            p.fillRect(pixmap.rect(), theme->textColor());
            p.end();
            return QIcon(pixmap);
        }
    }
    return icon;
}

// ─── Constructor ────────────────────────────────────────────────────────────
SchematicComponentsWidget::SchematicComponentsWidget(QWidget *parent)
    : QWidget(parent)
{
    PCBTheme* theme = ThemeManager::theme();
    QString bg = theme ? theme->panelBackground().name() : "#ffffff";
    QString fg = theme ? theme->textColor().name() : "#000000";
    QString border = theme ? theme->panelBorder().name() : "#cccccc";
    QString inputBg = (theme && theme->type() == PCBTheme::Light) ? "#f1f5f9" : "#1a1a1a";
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_tabs = new QTabWidget(this);
    m_tabs->setStyleSheet(QString(
        "QTabWidget::pane { border-top: 1px solid %1; background: %2; }"
        "QTabBar::tab { background: %2; color: %3; padding: 10px 15px; font-weight: 600; font-size: 11px; text-transform: uppercase; letter-spacing: 0.5px; }"
        "QTabBar::tab:selected { background: %2; color: #3b82f6; border-bottom: 2px solid #3b82f6; }"
        "QTabBar::tab:hover:!selected { background: %4; }"
    ).arg(border, bg, fg, (theme && theme->type() == PCBTheme::Light) ? "#f1f5f9" : "#2a2a2a"));

    // --- Tab 1: Symbols ---
    m_symbolTab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(m_symbolTab);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ── Search bar ──────────────────────────────────────────────────────
    m_searchBox = new QLineEdit(m_symbolTab);
    m_searchBox->setPlaceholderText("Search components...");
    m_searchBox->setClearButtonEnabled(true);
    
    m_searchBox->setStyleSheet(QString(
        "QLineEdit {"
        "   background-color: %1;"
        "   border: none;"
        "   border-bottom: 1px solid %2;"
        "   border-radius: 0px;"
        "   padding: 10px 12px 10px 32px;"
        "   color: %3;"
        "   font-size: 13px;"
        "   background-image: url(:/icons/tool_search.svg);"
        "   background-repeat: no-repeat;"
        "   background-position: left 10px center;"
        "}"
        "QLineEdit:focus {"
        "   background-color: %4;"
        "}"
    ).arg(inputBg, border, fg, bg));
    
    connect(m_searchBox, &QLineEdit::textChanged, this, &SchematicComponentsWidget::onSearchTextChanged);
    layout->addWidget(m_searchBox);

    // ── Action Cards Container ──────────────────────────────────────────
    QWidget* actionContainer = new QWidget(m_symbolTab);
    QVBoxLayout* actionLayout = new QVBoxLayout(actionContainer);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(0); 

    auto createActionCard = [this, actionLayout, theme, bg, fg, border](const QString& title, const QString& subTitle, const char* slot) {
        QPushButton* btn = new QPushButton(m_symbolTab);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(65);
        
        QVBoxLayout* btnLayout = new QVBoxLayout(btn);
        btnLayout->setContentsMargins(15, 0, 15, 0);
        btnLayout->setSpacing(2);
        
        QLabel* titleLabel = new QLabel(title);
        titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        titleLabel->setStyleSheet(QString("color: %1; font-weight: 600; font-size: 13px; border: none; background: transparent;").arg(fg));
        
        QLabel* descLabel = new QLabel(subTitle);
        descLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        descLabel->setStyleSheet(QString("color: %1; font-size: 11px; border: none; background: transparent;").arg(theme ? theme->textSecondary().name() : "#888"));
        
        btnLayout->addWidget(titleLabel);
        btnLayout->addWidget(descLabel);
        
        QString hoverBg = (theme && theme->type() == PCBTheme::Light) ? "#f8fafc" : "#2d2d2d";
        btn->setStyleSheet(QString(
            "QPushButton {"
            "   background-color: %1;"
            "   border: none;"
            "   border-bottom: 1px solid %2;"
            "   text-align: left;"
            "}"
            "QPushButton:hover {"
            "   background-color: %3;"
            "}"
        ).arg(bg, border, hoverBg));
        
        connect(btn, SIGNAL(clicked()), this, slot);
        actionLayout->addWidget(btn);
        return btn;
    };

    createActionCard("Create Custom Symbol", "Open symbol editor to draw new parts", SLOT(onCreateSymbol()));
    createActionCard("Browse Libraries", "Search millions of symbols and footprints", SLOT(onOpenLibraryBrowser()));

    layout->addWidget(actionContainer);

    // ── Section Header ──────────────────────────────────────────────────
    QLabel* listHeader = new QLabel("   STANDARD COMPONENTS", m_symbolTab);
    listHeader->setFixedHeight(28);
    QString headerBg = (theme && theme->type() == PCBTheme::Light) ? "#f1f5f9" : "#1a1a1a";
    listHeader->setStyleSheet(QString(
        "background-color: %1;"
        "color: %2;"
        "font-size: 10px;"
        "font-weight: 700;"
        "border-bottom: 1px solid %3;"
    ).arg(headerBg, theme ? theme->textSecondary().name() : "#555", border));
    layout->addWidget(listHeader);

    // ── Component Tree ──────────────────────────────────────────────────
    m_componentList = new QTreeWidget(this);
    m_componentList->setFrameShape(QFrame::NoFrame);
    m_componentList->setHeaderHidden(true);
    m_componentList->setRootIsDecorated(true);
    m_componentList->setIndentation(16);
    m_componentList->setAnimated(true);
    m_componentList->setExpandsOnDoubleClick(true);
    m_componentList->setUniformRowHeights(true);
    m_componentList->setIconSize(QSize(16, 16));
    
    QString selBg = theme ? theme->accentColor().name() : "#007acc";
    QString treeHoverBg = (theme && theme->type() == PCBTheme::Light) ? "#f1f5f9" : "#2a2a2a";
    
    m_componentList->setStyleSheet(QString(
        "QTreeWidget {"
        "   background-color: %1;"
        "   border: none;"
        "   color: %2;"
        "   outline: none;"
        "   padding: 2px;"
        "   font-size: 12px;"
        "}"
        "QTreeWidget::item {"
        "   padding: 4px 6px;"
        "   border-radius: 4px;"
        "   margin: 1px 4px;"
        "   border: none;"
        "}"
        "QTreeWidget::item:hover {"
        "   background-color: %3;"
        "}"
        "QTreeWidget::item:selected {"
        "   background-color: %4;"
        "   color: #ffffff;"
        "}"
        "QTreeWidget::branch {"
        "   background: transparent;"
        "}"
        "QTreeWidget::branch:has-children:!has-siblings:closed,"
        "QTreeWidget::branch:closed:has-children:has-siblings {"
        "   image: url(:/icons/chevron_right.svg);"
        "}"
        "QTreeWidget::branch:open:has-children:!has-siblings,"
        "QTreeWidget::branch:open:has-children:has-siblings {"
        "   image: url(:/icons/chevron_down.svg);"
        "}"
    ).arg(bg, fg, treeHoverBg, selBg));

    connect(m_componentList, &QTreeWidget::itemClicked, this, &SchematicComponentsWidget::onItemClicked);
    connect(m_componentList, &QTreeWidget::itemExpanded, this, [](QTreeWidgetItem* item) {
        item->setIcon(0, QIcon(":/icons/folder_open.svg"));
    });
    connect(m_componentList, &QTreeWidget::itemCollapsed, this, [](QTreeWidgetItem* item) {
        item->setIcon(0, QIcon(":/icons/folder_closed.svg"));
    });
    layout->addWidget(m_componentList);

    m_tabs->addTab(m_symbolTab, "Symbols");

    // --- Tab 2: Models ---
    m_modelTab = new ModelBrowserWidget(this);
    connect(m_modelTab, &ModelBrowserWidget::applyModelRequested, this, &SchematicComponentsWidget::onApplyModelRequested);
    m_tabs->addTab(m_modelTab, "SPICE Models");

    mainLayout->addWidget(m_tabs);

    populate();
}

void SchematicComponentsWidget::onApplyModelRequested(const SpiceModelInfo& info) {
    emit modelAssignmentRequested(info.name);
}

SchematicComponentsWidget::~SchematicComponentsWidget() {}

// ─── focusSearch ───────────────────────────────────────────────────────────
void SchematicComponentsWidget::focusSearch() {
    m_searchBox->setFocus();
    m_searchBox->selectAll();
}

// ─── Populate ───────────────────────────────────────────────────────────────
void SchematicComponentsWidget::populate() {
    m_componentList->clear();
    
    QIcon folderIcon(":/icons/folder_open.svg");
    QIcon componentIcon(":/icons/component_file.svg");
    
    // Lambda to add a category (folder style)
    auto addCategory = [this, &folderIcon](const QString& name) -> QTreeWidgetItem* {
        // Check if category already exists
        for (int i = 0; i < m_componentList->topLevelItemCount(); ++i) {
            if (m_componentList->topLevelItem(i)->text(0) == name) {
                return m_componentList->topLevelItem(i);
            }
        }
        
        QTreeWidgetItem* category = new QTreeWidgetItem(m_componentList);
        category->setText(0, name);
        category->setExpanded(true);
        category->setIcon(0, folderIcon);
        QFont categoryFont = category->font(0);
        categoryFont.setBold(true);
        categoryFont.setPointSize(10);
        category->setFont(0, categoryFont);
        return category;
    };
    
    // Lambda to add component (file style)
    auto addComponent = [this](QTreeWidgetItem* parent, const QString& name, const QString& toolName, const QString& libName = "") -> void {
        QTreeWidgetItem* child = new QTreeWidgetItem(parent);
        child->setText(0, name);
        child->setData(0, Qt::UserRole, toolName);
        if (!libName.isEmpty()) {
            child->setData(0, Qt::UserRole + 1, libName);
            child->setToolTip(0, QString("Library: %1").arg(libName));
        }
        child->setIcon(0, createComponentIcon(name));
        QFont itemFont = child->font(0);
        itemFont.setPointSize(10);
        child->setFont(0, itemFont);
    };
    
    // Group symbols by category
    QMap<QString, QList<QPair<QString, QString>>> categorizedSymbols;
    QMap<QString, QList<QPair<QString, QString>>> userSymbols;
    
    // 1. Add standard built-in tools (hardcoded C++ items)
    struct StdTool { QString name; QString category; };
    QList<StdTool> builtInTools = {
        {"Resistor", "Passives"}, {"Resistor (US)", "Passives"}, {"Resistor (IEC)", "Passives"},
        {"Capacitor", "Passives"}, {"Capacitor (Non-Polar)", "Passives"}, {"Capacitor (Polarized)", "Passives"},
        {"Inductor", "Passives"}, {"Transformer", "Passives"},
        {"Diode", "Semiconductors"}, {"Diode_Zener", "Semiconductors"}, {"Diode_Schottky", "Semiconductors"},
        {"NPN Transistor", "Semiconductors"}, {"PNP Transistor", "Semiconductors"},
        {"NMOS Transistor", "Semiconductors"}, {"PMOS Transistor", "Semiconductors"},
        {"IC", "Integrated Circuits"}, {"RAM", "Integrated Circuits"}, {"OpAmp", "Integrated Circuits"},
        {"Switch", "Interactive"}, {"Push Button", "Interactive"}, {"LED", "Interactive"},
        {"Gate_AND", "Logic"}, {"Gate_OR", "Logic"}, {"Gate_XOR", "Logic"},
        {"Gate_NAND", "Logic"}, {"Gate_NOR", "Logic"}, {"Gate_NOT", "Logic"},
        {"VoltageRegulator", "Power Symbols"},
        {"GND", "Power Symbols"}, {"VCC", "Power Symbols"}, {"VDD", "Power Symbols"},
        {"VSS", "Power Symbols"}, {"VBAT", "Power Symbols"}, {"3.3V", "Power Symbols"},
        {"5V", "Power Symbols"}, {"12V", "Power Symbols"},
        {"Probe", "Simulation"},
        {"Voltage Probe", "Simulation"},
        {"Current Probe", "Simulation"},
        {"Power Probe", "Simulation"},
        {"Voltmeter (DC)", "Simulation"},
        {"Voltmeter (AC)", "Simulation"},
        {"Ammeter (DC)", "Simulation"},
        {"Ammeter (AC)", "Simulation"},
        {"Wattmeter", "Simulation"},
        {"Power Meter", "Simulation"},
        {"Frequency Counter", "Simulation"},
        {"Logic Probe", "Simulation"},
        {"Logic Analyzer", "Simulation"},
        {"Oscilloscope Instrument", "Simulation"},
        {"Smart Signal Block", "Simulation"},
        {"Voltage Source (DC)", "Simulation"},
        {"Voltage Source (Sine)", "Simulation"},
        {"Voltage Source (Pulse)", "Simulation"}
    };
    
    for (const auto& tool : builtInTools) {
        categorizedSymbols[tool.category].append({tool.name, ""}); // "" means built-in tool
    }

    // 2. Add symbols from libraries (Built-in and User)
    for (SymbolLibrary* lib : SymbolLibraryManager::instance().libraries()) {
        for (const QString& symName : lib->symbolNames()) {
            // Avoid adding symbols that are already covered by our premium built-in tools
            bool alreadyAdded = false;
            for (const auto& tool : builtInTools) {
                if (tool.name == symName) { alreadyAdded = true; break; }
            }
            if (alreadyAdded) continue;

            SymbolDefinition* sym = lib->findSymbol(symName);
            if (sym) {
                QString cat = sym->category();
                if (cat.isEmpty()) cat = "Uncategorized";
                
                if (!lib->isBuiltIn()) {
                    userSymbols[lib->name()].append({symName, lib->name()});
                } else {
                    categorizedSymbols[cat].append({symName, lib->name()});
                }
            }
        }
    }

    // Pass 1: User Libraries
    for (const QString& libName : userSymbols.keys()) {
        QTreeWidgetItem* userCat = addCategory(libName.toUpper());
        userCat->setForeground(0, QBrush(QColor("#000000")));
        for (const auto& symPair : userSymbols[libName]) {
            addComponent(userCat, symPair.first, symPair.first, symPair.second);
        }
    }
    
    // Sort categories with priority ones at the top
    QStringList sortedCats = categorizedSymbols.keys();
    sortedCats.sort();
    QStringList priority = {"Passives", "Semiconductors", "Integrated Circuits", "Logic", "Power Symbols", "Simulation"};
    for (int i = priority.size()-1; i >= 0; --i) {
        if (sortedCats.removeAll(priority[i])) sortedCats.prepend(priority[i]);
    }

    for (const QString& catName : sortedCats) {
        QTreeWidgetItem* category = addCategory(catName);
        for (const auto& symPair : categorizedSymbols[catName]) {
            addComponent(category, symPair.first, symPair.first, symPair.second);
        }
    }

    // Ensure categories are visible by default, even after previous filter use.
    m_componentList->expandAll();
    
    qDebug() << "Schematic components panel populated with" << categorizedSymbols.size() << "categories.";
}

// ─── Slots ──────────────────────────────────────────────────────────────────
void SchematicComponentsWidget::onSearchTextChanged(const QString &text) {
    if (!m_componentList) return;
    const QString needle = text.trimmed();
    
    for (int i = 0; i < m_componentList->topLevelItemCount(); ++i) {
        QTreeWidgetItem* category = m_componentList->topLevelItem(i);
        bool categoryHasMatch = false;
        
        for (int j = 0; j < category->childCount(); ++j) {
            QTreeWidgetItem* child = category->child(j);
            bool matches = child->text(0).contains(needle, Qt::CaseInsensitive);
            child->setHidden(!matches && !needle.isEmpty());
            if (matches) categoryHasMatch = true;
        }
        
        category->setHidden(!categoryHasMatch && !needle.isEmpty());
        if (categoryHasMatch && !needle.isEmpty()) {
            category->setExpanded(true);
        } else if (needle.isEmpty()) {
            category->setExpanded(true);
        }
    }
}

void SchematicComponentsWidget::onItemClicked(QTreeWidgetItem *item, int column) {
    Q_UNUSED(column);
    if (item->childCount() == 0) {
       QString toolName = item->data(0, Qt::UserRole).toString();
       QString libName = item->data(0, Qt::UserRole + 1).toString();
       
       if (!toolName.isEmpty()) {
           // Look up symbol to find default footprint
           SymbolDefinition* sym = nullptr;
           if (!libName.isEmpty()) {
               SymbolLibrary* lib = SymbolLibraryManager::instance().findLibrary(libName);
               if (lib) sym = lib->findSymbol(toolName);
           }
           
           if (!sym) {
               sym = SymbolLibraryManager::instance().findSymbol(toolName);
           }
           
           if (sym) {
               m_selectedSymbol = *sym;
           } else {
               m_selectedSymbol = SymbolDefinition(toolName);
           }

           emit toolSelected(toolName);
       }
    }
}

void SchematicComponentsWidget::onCreateSymbol() {
    auto* editor = new SymbolEditor(); // No parent for top-level window behavior
    editor->setAttribute(Qt::WA_DeleteOnClose);
    
    connect(editor, &SymbolEditor::symbolSaved, this, [this](const SymbolDefinition& symbol) {
        populate();
        emit symbolCreated(symbol.name());
    });

    connect(editor, &SymbolEditor::placeInSchematicRequested, this, [this](const SymbolDefinition& symbol) {
        emit symbolPlacementRequested(symbol);
    });

    editor->show();
}
void SchematicComponentsWidget::onOpenLibraryBrowser() {
    LibraryBrowserDialog dialog(this);
    connect(&dialog, &LibraryBrowserDialog::symbolPlaced, this, [this](const SymbolDefinition& symbol) {
        emit toolSelected(symbol.name());
    });
    dialog.exec();
}
