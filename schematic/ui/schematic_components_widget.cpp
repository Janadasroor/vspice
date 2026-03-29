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
#include <QPainterPath>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QSet>
#include <QCursor>
#include <QEvent>
#include <QTimer>

using Flux::Model::SymbolDefinition;
using Flux::Model::SymbolPrimitive;

namespace {
int pinCount(const SymbolDefinition& sym) {
    int count = 0;
    for (const auto& prim : sym.primitives()) {
        if (prim.type == SymbolPrimitive::Pin) ++count;
    }
    return count;
}

bool hasConcreteModelName(const SymbolDefinition& sym) {
    const QString modelName = sym.modelName().trimmed();
    if (modelName.isEmpty()) return false;
    const QString spiceDevice = sym.spiceModelName().trimmed();
    return spiceDevice.isEmpty() || modelName.compare(spiceDevice, Qt::CaseInsensitive) != 0;
}

bool isResolvableSubckt(const SymbolDefinition& sym) {
    if (!hasConcreteModelName(sym)) return false;
    const QString modelName = sym.modelName().trimmed();
    if (ModelLibraryManager::instance().findSubcircuit(modelName) != nullptr) return true;
    const QString modelPath = sym.modelPath().trimmed();
    return !modelPath.isEmpty() && QFileInfo::exists(modelPath);
}

bool isSimulatableLibrarySymbol(const SymbolDefinition& sym) {
    if (sym.name().trimmed().isEmpty()) return false;
    if (pinCount(sym) <= 0) return false;
    if (sym.isPowerSymbol()) return true;

    const QString spiceDevice = sym.spiceModelName().trimmed().toUpper();
    const QString modelPath = sym.modelPath().trimmed();
    const bool hasPath = !modelPath.isEmpty();
    const bool hasModel = hasConcreteModelName(sym);

    if (spiceDevice == "SUBCKT") {
        return hasPath || isResolvableSubckt(sym);
    }

    if (spiceDevice.isEmpty() && !hasPath && !hasModel) return false;

    // Generic subcircuit fallback: some symbols rely on modelName/modelPath without Sim.Device.
    if (spiceDevice.isEmpty() && (hasPath || hasModel)) return true;

    // Primitive/behavioral device classes that can be netlisted directly.
    static const QSet<QString> kPrimitiveDevices = {
        "R", "C", "L", "V", "I", "D", "Q", "M", "J",
        "E", "F", "G", "H", "B", "SW", "A"
    };
    if (kPrimitiveDevices.contains(spiceDevice)) return true;

    // If a device token is custom/non-standard, require explicit model linkage.
    return hasPath || hasModel;
}

bool isBundledKicadSymLibraryPath(const QString& p) {
    const QString np = QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(p).absoluteFilePath()));
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList roots = {
        QDir(appDir).absoluteFilePath("viospicelib"),
        QDir(appDir).absoluteFilePath("../viospicelib")
    };
    for (const QString& rootRaw : roots) {
        QString root = QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(rootRaw).absoluteFilePath()));
        if (!root.endsWith('/')) root += '/';
        if (np.startsWith(root) && np.contains("/symbols/kicad/")) return true;
    }
    return false;
}

bool isUserLibraryPath(const QString& p) {
    return !isBundledKicadSymLibraryPath(p);
}

} // namespace

// Replaced by SymbolPreviewWidget


// ─── Constructor ────────────────────────────────────────────────────────────
SchematicComponentsWidget::SchematicComponentsWidget(QWidget *parent)
    : QWidget(parent)
{
    // 1. Initialize data models first
    m_symbolListModel = new SymbolListModel(this);
    m_proxyModel = new QSortFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_symbolListModel);
    m_proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxyModel->setRecursiveFilteringEnabled(true);

    // 2. Setup Base UI
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
    QVBoxLayout* symbolLayout = new QVBoxLayout(m_symbolTab);
    symbolLayout->setContentsMargins(0, 0, 0, 0);
    symbolLayout->setSpacing(0);

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
    symbolLayout->addWidget(m_searchBox);

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

    symbolLayout->addWidget(actionContainer);

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
    symbolLayout->addWidget(listHeader);

    // ── Component Tree ──────────────────────────────────────────────────
    m_componentList = new QTreeView(this);
    m_componentList->setFrameShape(QFrame::NoFrame);
    m_componentList->setModel(m_proxyModel);
    m_componentList->setHeaderHidden(true);
    m_componentList->setRootIsDecorated(true);
    m_componentList->setIndentation(16);
    m_componentList->setAnimated(true);
    m_componentList->setUniformRowHeights(true);
    m_componentList->setIconSize(QSize(16, 16));
    m_componentList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_componentList->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_componentList->setSelectionMode(QAbstractItemView::SingleSelection);
    
    QString selBg = theme ? theme->accentColor().name() : "#007acc";
    QString treeHoverBg = (theme && theme->type() == PCBTheme::Light) ? "#f1f5f9" : "#2a2a2a";
    
    m_componentList->setStyleSheet(QString(
        "QTreeView {"
        "   background-color: %1;"
        "   border: none;"
        "   color: %2;"
        "   outline: none;"
        "   padding: 2px;"
        "   font-size: 12px;"
        "}"
        "QTreeView::item {"
        "   padding: 4px 6px;"
        "   border-radius: 4px;"
        "   margin: 1px 4px;"
        "   border: none;"
        "}"
        "QTreeView::item:hover {"
        "   background-color: %3;"
        "}"
        "QTreeView::item:selected {"
        "   background-color: %4;"
        "   color: #ffffff;"
        "}"
        "QTreeView::branch {"
        "   background: transparent;"
        "}"
    ).arg(bg, fg, treeHoverBg, selBg));

    m_componentList->setMouseTracking(true);
    m_componentList->installEventFilter(this);
    connect(m_componentList, &QTreeView::entered, this, &SchematicComponentsWidget::onItemHovered);
    connect(m_componentList, &QTreeView::clicked, this, &SchematicComponentsWidget::onItemClicked);
    
    m_previewPopup = new SymbolPreviewWidget(this, Qt::ToolTip | Qt::FramelessWindowHint);
    m_previewPopup->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_previewPopup->setAttribute(Qt::WA_NoSystemBackground);
    m_previewPopup->setStaticMode(true);
    m_previewPopup->setFixedSize(220, 220);
    
    symbolLayout->addWidget(m_componentList);

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

bool SchematicComponentsWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_componentList && event->type() == QEvent::Leave) {
        if (m_previewPopup) m_previewPopup->hide();
    }
    return QWidget::eventFilter(watched, event);
}

SchematicComponentsWidget::~SchematicComponentsWidget() {}

// ─── focusSearch ───────────────────────────────────────────────────────────
void SchematicComponentsWidget::focusSearch() {
    m_searchBox->setFocus();
    m_searchBox->selectAll();
}

// ─── Populate ───────────────────────────────────────────────────────────────
void SchematicComponentsWidget::populate() {
    // Only reload if we have no libraries
    if (SymbolLibraryManager::instance().libraries().isEmpty()) {
        SymbolLibraryManager::instance().reloadUserLibraries();
    }

    QVector<SymbolListModel::SymbolMetadata> builtIn;
    struct StdTool { QString name; QString category; };
    QList<StdTool> builtInTools = {
        {"Resistor", "Passives"}, {"Resistor (US)", "Passives"}, {"Resistor (IEC)", "Passives"},
        {"Capacitor", "Passives"}, {"Capacitor (Non-Polar)", "Passives"}, {"Capacitor (Polarized)", "Passives"},
        {"Inductor", "Passives"}, {"Transformer", "Passives"},
        {"Diode", "Semiconductors"}, {"Diode_Zener", "Semiconductors"}, {"Diode_Schottky", "Semiconductors"},
        {"NPN Transistor", "Semiconductors"}, {"PNP Transistor", "Semiconductors"},
        {"NMOS Transistor", "Semiconductors"}, {"PMOS Transistor", "Semiconductors"},
        {"IC", "Integrated Circuits"}, {"RAM", "Integrated Circuits"}, {"OpAmp", "Integrated Circuits"},
        {"Switch", "Interactive"}, {"Voltage Controlled Switch", "Interactive"}, {"Push Button", "Interactive"}, {"LED", "Interactive"},
        {"Blinking LED", "Interactive"},
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
        {"Voltage Source (Pulse)", "Simulation"},
        {"BV", "Simulation"},
        {"BI", "Simulation"},
        {"VCVS (E)", "Simulation"},
        {"VCCS (G)", "Simulation"},
        {"CCCS (F)", "Simulation"},
        {"CCVS (H)", "Simulation"}
    };

    builtIn.reserve(builtInTools.size());
    for (const auto& t : builtInTools) {
        builtIn.append({t.name, t.category, ""});
    }

    QMap<QString, QStringList> libs;
    int totalLibrarySymbols = 0;
    int simulatableLibrarySymbols = 0;
    auto libraries = SymbolLibraryManager::instance().libraries();
    for (auto* lib : libraries) {
        // KiCad merged libraries are loaded as stubs and resolving every symbol here
        // blocks editor startup. Keep launch responsive by skipping stub-only libs
        // in this eager population path.
        if (lib &&
            lib->path().endsWith(".kicad_sym", Qt::CaseInsensitive) &&
            isBundledKicadSymLibraryPath(lib->path())) {
            continue;
        }

        QStringList accepted;
        const QStringList names = lib->symbolNames();
        totalLibrarySymbols += names.size();

        for (const QString& symName : names) {
            SymbolDefinition* sym = lib->findSymbol(symName);
            if (!sym) continue;
            
            // For user libraries (viosym/local sclib), show all symbols.
            // For bundled libraries (KiCad), keep the simulatable filter to avoid noise.
            if (isBundledKicadSymLibraryPath(lib->path())) {
                if (!isSimulatableLibrarySymbol(*sym)) continue;
            }
            
            accepted.append(symName);
        }

        accepted.sort(Qt::CaseInsensitive);
        if (!accepted.isEmpty()) {
            libs[lib->name()] = accepted;
            simulatableLibrarySymbols += accepted.size();
        }
    }

    qInfo() << "SchematicComponentsWidget: simulatable symbol filter kept"
            << simulatableLibrarySymbols << "of" << totalLibrarySymbols
            << "library symbols.";

    m_symbolListModel->setSymbols(builtIn, libs);
    
    // Expand top-level built-in categories by default
    for (int i = 0; i < m_proxyModel->rowCount(); ++i) {
        QModelIndex idx = m_proxyModel->index(i, 0);
        if (m_proxyModel->data(idx, SymbolListModel::LibraryRole).toString().isEmpty()) {
            m_componentList->expand(idx);
        }
    }
}

// ─── Slots ──────────────────────────────────────────────────────────────────
void SchematicComponentsWidget::onSearchTextChanged(const QString &text) {
    m_proxyModel->setFilterFixedString(text);
    if (!text.isEmpty()) {
        m_componentList->expandAll();
    }
}

void SchematicComponentsWidget::onItemClicked(const QModelIndex& index) {
    if (!index.isValid()) return;

    QModelIndex sourceIndex = m_proxyModel->mapToSource(index);
    if (m_symbolListModel->data(sourceIndex, SymbolListModel::IsCategoryRole).toBool()) {
        return;
    }

    const auto& sym = m_symbolListModel->symbolDefinition(sourceIndex);
    m_selectedSymbol = sym;
    emit toolSelected(sym.name());
    
    m_previewPopup->hide();
}

void SchematicComponentsWidget::onItemHovered(const QModelIndex& index) {
    if (!index.isValid()) {
        m_previewPopup->hide();
        return;
    }

    QModelIndex sourceIndex = m_proxyModel->mapToSource(index);
    if (m_symbolListModel->data(sourceIndex, SymbolListModel::IsCategoryRole).toBool()) {
        m_previewPopup->hide();
        return;
    }

    const auto& sym = m_symbolListModel->symbolDefinition(sourceIndex);
    if (sym.name().isEmpty()) {
        m_previewPopup->hide();
        return;
    }

    m_previewPopup->setSymbol(sym);
    
    // Position to the right of the cursor
    QPoint pos = QCursor::pos() + QPoint(20, -10);
    m_previewPopup->move(pos);
    m_previewPopup->show();
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
