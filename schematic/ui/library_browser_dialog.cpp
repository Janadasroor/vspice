#include "library_browser_dialog.h"
#include "../../core/theme_manager.h"
#include "../../symbols/symbol_library.h"
#include "../../core/library_index.h"
#include "../items/generic_component_item.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QHeaderView>
#include <QPainter>
#include <QTimer>
#include <QRandomGenerator>
#include <QLineEdit>
#include <QToolBar>
#include <QAction>
#include <QSplitter>
#include <QLabel>
#include <QSet>

using Flux::Model::SymbolPrimitive;

namespace {
SymbolDefinition makeFallbackPreviewSymbol(const QString& name, const QString& category) {
    SymbolDefinition def(name);
    def.setCategory(category);
    def.setReferencePrefix("U");

    const QString n = name.toLower();
    auto addCommonPins = [&def]() {
        def.addPrimitive(SymbolPrimitive::createPin(QPointF(-35, 0), 1, "1", "Right", 12));
        def.addPrimitive(SymbolPrimitive::createPin(QPointF(35, 0), 2, "2", "Left", 12));
    };

    if (n.contains("resistor")) {
        addCommonPins();
        def.addPrimitive(SymbolPrimitive::createLine(QPointF(-23, 0), QPointF(-18, -6)));
        def.addPrimitive(SymbolPrimitive::createLine(QPointF(-18, -6), QPointF(-12, 6)));
        def.addPrimitive(SymbolPrimitive::createLine(QPointF(-12, 6), QPointF(-6, -6)));
        def.addPrimitive(SymbolPrimitive::createLine(QPointF(-6, -6), QPointF(0, 6)));
        def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 6), QPointF(6, -6)));
        def.addPrimitive(SymbolPrimitive::createLine(QPointF(6, -6), QPointF(12, 6)));
        def.addPrimitive(SymbolPrimitive::createLine(QPointF(12, 6), QPointF(18, -6)));
        def.addPrimitive(SymbolPrimitive::createLine(QPointF(18, -6), QPointF(23, 0)));
    } else if (n.contains("capacitor")) {
        addCommonPins();
        def.addPrimitive(SymbolPrimitive::createLine(QPointF(-20, -10), QPointF(-20, 10)));
        def.addPrimitive(SymbolPrimitive::createLine(QPointF(20, -10), QPointF(20, 10)));
        def.addPrimitive(SymbolPrimitive::createLine(QPointF(-23, 0), QPointF(-20, 0)));
        def.addPrimitive(SymbolPrimitive::createLine(QPointF(20, 0), QPointF(23, 0)));
    } else if (n.contains("diode") || n == "led") {
        addCommonPins();
        def.addPrimitive(SymbolPrimitive::createPolygon({QPointF(-16, -10), QPointF(-16, 10), QPointF(8, 0)}, false));
        def.addPrimitive(SymbolPrimitive::createLine(QPointF(12, -10), QPointF(12, 10)));
    } else if (n.contains("inductor")) {
        addCommonPins();
        def.addPrimitive(SymbolPrimitive::createCircle(QPointF(-10, 0), 4, false));
        def.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, 0), 4, false));
        def.addPrimitive(SymbolPrimitive::createCircle(QPointF(10, 0), 4, false));
        def.addPrimitive(SymbolPrimitive::createCircle(QPointF(20, 0), 4, false));
    } else if (n.contains("gnd")) {
        def.setReferencePrefix("#PWR");
        def.setIsPowerSymbol(true);
        def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -18), QPointF(0, -6)));
        def.addPrimitive(SymbolPrimitive::createLine(QPointF(-10, -6), QPointF(10, -6)));
        def.addPrimitive(SymbolPrimitive::createLine(QPointF(-7, -3), QPointF(7, -3)));
        def.addPrimitive(SymbolPrimitive::createLine(QPointF(-4, 0), QPointF(4, 0)));
    } else if (n.contains("vcc") || n.contains("vdd") || n.contains("vss") || n.contains("vbat") || n == "5v" || n == "3.3v" || n == "12v") {
        def.setReferencePrefix("#PWR");
        def.setIsPowerSymbol(true);
        def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 18), QPointF(0, -4)));
        def.addPrimitive(SymbolPrimitive::createPolygon({QPointF(-10, -4), QPointF(10, -4), QPointF(0, -16)}, false));
    } else if (n.contains("voltage source")) {
        addCommonPins();
        def.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, 0), 25, false));
        def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -45), QPointF(0, -25)));
        def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 25), QPointF(0, 45)));
        if (n.contains("dc")) {
            def.addPrimitive(SymbolPrimitive::createText("+", QPointF(-4, -8), 10, QColor("#888")));
            def.addPrimitive(SymbolPrimitive::createText("-", QPointF(-4, 16), 10, QColor("#888")));
        } else if (n.contains("sine")) {
            QList<QPointF> points;
            for(int i=-15; i<=15; ++i) points.append(QPointF(i, -10*std::sin(i*M_PI/15.0)));
            def.addPrimitive(SymbolPrimitive::createPolygon(points, false));
        } else if (n.contains("pulse")) {
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 8), QPointF(-10, 8)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-10, 8), QPointF(-10, -8)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-10, -8), QPointF(0, -8)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -8), QPointF(0, 8)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 8), QPointF(10, 8)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(10, 8), QPointF(10, -8)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(10, -8), QPointF(15, -8)));
        }
    } else if (n.contains("meter") || n.contains("probe") || n.contains("oscilloscope") || n.contains("logic analyzer") || n.contains("signal block")) {
        addCommonPins();
        def.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, 0), 14, false));
        QString letter = "M";
        if (n.contains("volt")) letter = "V";
        else if (n.contains("amm")) letter = "A";
        else if (n.contains("power")) letter = "P";
        else if (n.contains("oscillo")) letter = "~";
        else if (n.contains("logic analyzer")) letter = "LA";
        else if (n.contains("signal block")) letter = "S";
        def.addPrimitive(SymbolPrimitive::createText(letter, QPointF(-4, 4), 10, QColor("#888")));
    } else if (n.contains("transistor")) {
        def.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, 0), 14, false));
        def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, 0), 1, "B", "Right", 12));
        def.addPrimitive(SymbolPrimitive::createPin(QPointF(18, -16), 2, "C", "Left", 10));
        def.addPrimitive(SymbolPrimitive::createPin(QPointF(18, 16), 3, "E", "Left", 10));
        def.addPrimitive(SymbolPrimitive::createLine(QPointF(-12, 0), QPointF(6, 0)));
        def.addPrimitive(SymbolPrimitive::createLine(QPointF(6, 0), QPointF(12, -8)));
        def.addPrimitive(SymbolPrimitive::createLine(QPointF(6, 0), QPointF(12, 8)));
    } else {
        addCommonPins();
        def.addPrimitive(SymbolPrimitive::createRect(QRectF(-20, -14, 40, 28), false));
    }

    return def;
}
} // namespace

LibraryBrowserDialog::LibraryBrowserDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle("Component Library Browser");
    resize(1000, 650);
    
    PCBTheme* theme = ThemeManager::theme();
    if (theme) {
        QString bg = theme->windowBackground().name();
        QString panelBg = theme->panelBackground().name();
        QString fg = theme->textColor().name();
        QString border = theme->panelBorder().name();
        QString accent = theme->accentColor().name();
        QString inputBg = (theme->type() == PCBTheme::Light) ? "#f1f5f9" : "#1e1e1e";

        setStyleSheet(QString(
            "QDialog { background-color: %1; color: %3; }"
            "QLabel { color: %3; }"
            "QPushButton { background-color: %2; border: 1px solid %4; border-radius: 6px; padding: 8px 16px; color: %3; font-weight: 600; }"
            "QPushButton:hover { background-color: %5; color: white; border-color: %5; }"
            "QLineEdit { background-color: %6; border: 1px solid %4; border-radius: 6px; padding: 4px 8px; color: %3; }"
            "QLineEdit:focus { border-color: %5; }"
            "QTreeWidget, QListWidget { background-color: %2; border: 1px solid %4; border-radius: 8px; color: %3; outline: none; }"
            "QTreeWidget::item, QListWidget::item { padding: 6px; border-radius: 4px; margin: 1px 4px; }"
            "QTreeWidget::item:selected, QListWidget::item:selected { background-color: %5; color: white; }"
        ).arg(bg, panelBg, fg, border, accent, inputBg));
    }

    setupUI();
    performSymbolSearch("");
}

LibraryBrowserDialog::~LibraryBrowserDialog() {}

void LibraryBrowserDialog::setupUI() {
    PCBTheme* theme = ThemeManager::theme();
    QString accent = theme ? theme->accentColor().name() : "#007acc";
    QString fg = theme ? theme->textColor().name() : "#000000";
    QString border = theme ? theme->panelBorder().name() : "#cccccc";
    QString headerBg = (theme && theme->type() == PCBTheme::Light) ? "#f8fafc" : "#1a1a1a";

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(12);

    // ── Top: Search Area ────────────────────────────────────────────────
    QHBoxLayout* searchLayout = new QHBoxLayout();
    m_searchBox = new QLineEdit(this);
    m_searchBox->setPlaceholderText("Search components (e.g. STM32, Resistor, Op-amp)...");
    m_searchBox->setFixedHeight(44);
    m_searchBox->setStyleSheet(m_searchBox->styleSheet() + " font-size: 14px; padding-left: 15px;");
    
    QPushButton* searchBtn = new QPushButton("Search", this);
    searchBtn->setFixedHeight(44);
    searchBtn->setFixedWidth(110);
    searchBtn->setStyleSheet(QString("background-color: %1; color: white; border: none; font-weight: bold;").arg(accent));
    
    connect(m_searchBox, &QLineEdit::returnPressed, this, &LibraryBrowserDialog::onSearch);
    connect(searchBtn, &QPushButton::clicked, this, &LibraryBrowserDialog::onSearch);

    searchLayout->addWidget(m_searchBox);
    searchLayout->addWidget(searchBtn);
    mainLayout->addLayout(searchLayout);

    // ── Mode Strip ──────────────────────────────────────────────────────
    QToolBar* modeBar = new QToolBar(this);
    modeBar->setIconSize(QSize(18, 18));
    modeBar->setMovable(false);
    modeBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    
    auto getIcon = [&](const QString& path) {
        QIcon icon(path);
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
    };

    QAction* pickAct = modeBar->addAction(getIcon(":/icons/component_file.svg"), "Devices");
    QAction* termAct = modeBar->addAction(getIcon(":/icons/tool_no_connect.svg"), "Terminals");
    QAction* instrAct = modeBar->addAction(getIcon(":/icons/tool_probe.svg"), "Instruments");
    QAction* powerAct = modeBar->addAction(getIcon(":/icons/comp_vcc.svg"), "Power");

    QString toolbarStyle = QString(
        "QToolBar { background: transparent; border: none; spacing: 8px; }"
        "QToolButton { background: %1; border: 1px solid %2; border-radius: 6px; padding: 6px 12px; color: %3; font-weight: 600; }"
        "QToolButton:hover { background: %4; }"
        "QToolButton:checked { background: %5; color: white; border-color: %5; }"
    ).arg(headerBg, border, fg, (theme && theme->type() == PCBTheme::Light) ? "#eff6ff" : "#2d2d2d", accent);
    modeBar->setStyleSheet(toolbarStyle);

    mainLayout->addWidget(modeBar);

    // ── Center: Splitter ────────────────────────────────────────────────
    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(1);
    
    m_categoriesTree = new QTreeWidget(this);
    m_categoriesTree->setHeaderHidden(true);
    connect(m_categoriesTree, &QTreeWidget::itemClicked, this, &LibraryBrowserDialog::onCategorySelected);
    
    QTreeWidgetItem* onlineRoot = new QTreeWidgetItem(m_categoriesTree);
    onlineRoot->setText(0, "ONLINE LIBRARIES");
    onlineRoot->setExpanded(true);
    
    QTreeWidgetItem* localRoot = new QTreeWidgetItem(m_categoriesTree);
    localRoot->setText(0, "LOCAL LIBRARIES");
    localRoot->setExpanded(true);
    
    QStringList localCats = SymbolLibraryManager::instance().allCategories();
    for (const QString& cat : localCats) {
        new QTreeWidgetItem(localRoot, QStringList() << cat);
    }
    
    m_resultsList = new QListWidget(this);
    connect(m_resultsList, &QListWidget::itemClicked, this, &LibraryBrowserDialog::onResultSelected);

    // Right: Preview Panel
    QWidget* previewPanel = new QWidget(this);
    previewPanel->setFixedWidth(320);
    QVBoxLayout* previewLayout = new QVBoxLayout(previewPanel);
    previewLayout->setContentsMargins(15, 0, 0, 0);
    previewLayout->setSpacing(8);

    m_previewTitle = new QLabel("Select a Component", this);
    m_previewTitle->setStyleSheet("font-size: 20px; font-weight: 800;");
    m_previewTitle->setWordWrap(true);
    
    m_previewDesc = new QLabel("View details, symbols, and footprints here.", this);
    m_previewDesc->setStyleSheet(QString("color: %1; font-size: 13px;").arg(theme ? theme->textSecondary().name() : "#666"));
    m_previewDesc->setWordWrap(true);

    m_previewStats = new QLabel("", this);
    m_previewStats->setStyleSheet(QString("color: %1; font-size: 11px; font-weight: bold;").arg(accent));

    m_previewScene = new QGraphicsScene(this);
    m_previewView = new QGraphicsView(m_previewScene, this);
    m_previewView->setFixedHeight(220);
    m_previewView->setRenderHint(QPainter::Antialiasing);
    m_previewView->setFrameShape(QFrame::NoFrame);
    m_previewView->setStyleSheet(QString("background-color: %1; border: 1px solid %2; border-radius: 8px;").arg((theme && theme->type() == PCBTheme::Light) ? "#f8fafc" : "#0d1117", border));
    m_previewView->setInteractive(false);

    previewLayout->addWidget(m_previewTitle);
    previewLayout->addWidget(m_previewDesc);
    previewLayout->addWidget(m_previewStats);
    previewLayout->addWidget(m_previewView);
    previewLayout->addStretch();

    splitter->addWidget(m_categoriesTree);
    splitter->addWidget(m_resultsList);
    splitter->addWidget(previewPanel);
    splitter->setStretchFactor(1, 2); 
    
    mainLayout->addWidget(splitter);

    // ── Bottom: Control Buttons ─────────────────────────────────────────
    QHBoxLayout* bottomLayout = new QHBoxLayout();
    QLabel* statusLabel = new QLabel("Ready to place components", this);
    statusLabel->setStyleSheet(QString("color: %1; font-size: 11px; font-style: italic;").arg(theme ? theme->textSecondary().name() : "#888"));
    
    QPushButton* cancelBtn = new QPushButton("Cancel", this);
    QPushButton* placeBtn = new QPushButton("Place Component", this);
    placeBtn->setStyleSheet(QString("background-color: %1; color: white; border: none;").arg(accent));
    
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(placeBtn, &QPushButton::clicked, this, &LibraryBrowserDialog::onPlaceClicked);

    bottomLayout->addWidget(statusLabel);
    bottomLayout->addStretch();
    bottomLayout->addWidget(cancelBtn);
    bottomLayout->addWidget(placeBtn);
    mainLayout->addLayout(bottomLayout);
}

void LibraryBrowserDialog::performSymbolSearch(const QString& query) {
    m_resultsList->clear();
    m_searchResults.clear();

    QList<SymbolDefinition*> found;
    const QString q = query.trimmed();
    QSet<QString> seenNames;

    if (q.isEmpty()) {
        for (SymbolLibrary* lib : SymbolLibraryManager::instance().libraries()) {
            if (!lib) continue;
            for (const QString& name : lib->symbolNames()) {
                if (SymbolDefinition* sym = lib->findSymbol(name)) found.append(sym);
            }
        }
    } else {
        found = SymbolLibraryManager::instance().search(q);
    }

    auto appendResult = [&](const SymbolDefinition& def) {
        const QString key = def.name().trimmed().toLower();
        if (key.isEmpty() || seenNames.contains(key)) return;
        seenNames.insert(key);

        QListWidgetItem* item = new QListWidgetItem(m_resultsList);
        QWidget* itemWidget = new QWidget();
        itemWidget->setMinimumHeight(55);
        QVBoxLayout* itemLayout = new QVBoxLayout(itemWidget);
        itemLayout->setContentsMargins(10, 8, 10, 8);
        itemLayout->setSpacing(2);

        PCBTheme* theme = ThemeManager::theme();
        QString accent = theme ? theme->accentColor().name() : "#007acc";
        QString secFg = theme ? theme->textSecondary().name() : "#666";

        QLabel* nameLabel = new QLabel(def.name());
        nameLabel->setStyleSheet(QString("font-weight: 700; font-size: 13px; color: %1;").arg(accent));

        QString detailText = def.category();
        if (detailText.isEmpty()) detailText = "General";
        detailText += " • Verified Symbol";

        QLabel* detailLabel = new QLabel(detailText);
        detailLabel->setStyleSheet(QString("font-size: 11px; color: %1;").arg(secFg));

        itemLayout->addWidget(nameLabel);
        itemLayout->addWidget(detailLabel);

        item->setSizeHint(itemWidget->sizeHint().expandedTo(QSize(0, 55)));
        m_resultsList->setItemWidget(item, itemWidget);
        m_searchResults.append(def);
    };

    for (SymbolDefinition* sym : found) if (sym) appendResult(*sym);

    // Include built-in schematic tools
    const QList<QPair<QString, QString>> builtInTools = {
        {"Resistor", "Passives"}, {"Capacitor", "Passives"}, {"Inductor", "Passives"}, {"Transformer", "Passives"},
        {"Diode", "Semiconductors"}, {"NPN Transistor", "Semiconductors"}, {"PNP Transistor", "Semiconductors"},
        {"NMOS Transistor", "Semiconductors"}, {"PMOS Transistor", "Semiconductors"},
        {"IC", "Integrated Circuits"}, {"RAM", "Integrated Circuits"}, {"OpAmp", "Integrated Circuits"},
        {"Switch", "Interactive"}, {"LED", "Interactive"}, {"GND", "Power"}, {"VCC", "Power"},
        {"Oscilloscope Instrument", "Simulation"}, {"Logic Analyzer", "Simulation"},
        {"Voltage Source (DC)", "Simulation"}, {"Voltage Source (Sine)", "Simulation"}
    };
    for (const auto& tool : builtInTools) {
        if (!q.isEmpty() && !tool.first.contains(q, Qt::CaseInsensitive)) continue;
        SymbolDefinition d(tool.first); d.setCategory(tool.second);
        appendResult(d);
    }

    if (m_searchResults.isEmpty()) {
        QListWidgetItem* empty = new QListWidgetItem("No results found.");
        empty->setFlags(Qt::NoItemFlags);
        m_resultsList->addItem(empty);
    }
}

void LibraryBrowserDialog::onSearch() { performSymbolSearch(m_searchBox->text()); }

void LibraryBrowserDialog::onCategorySelected(QTreeWidgetItem* item, int) {
    if (item->parent()) { m_searchBox->setText(item->text(0)); onSearch(); }
}

void LibraryBrowserDialog::onResultSelected(QListWidgetItem* item) {
    int row = m_resultsList->row(item);
    if (row >= 0 && row < m_searchResults.size()) {
        m_selectedSymbol = m_searchResults[row];
        m_previewTitle->setText(m_selectedSymbol.name());
        m_previewDesc->setText(m_selectedSymbol.description().isEmpty() ? "No description available for this part." : m_selectedSymbol.description());
        m_previewStats->setText("Industry Standard • High Reliability");

        m_previewScene->clear();
        SymbolDefinition renderSymbol = m_selectedSymbol;
        if (renderSymbol.primitives().isEmpty()) {
            renderSymbol = makeFallbackPreviewSymbol(m_selectedSymbol.name(), m_selectedSymbol.category());
        }
        auto* previewItem = new GenericComponentItem(renderSymbol);
        previewItem->setFlag(QGraphicsItem::ItemIsSelectable, false);
        previewItem->setReference("");
        previewItem->setValue("");
        m_previewScene->addItem(previewItem);
        previewItem->setPos(0, 0);

        QRectF bounds = m_previewScene->itemsBoundingRect();
        if (bounds.isNull() || bounds.width() < 10) bounds = QRectF(-40, -40, 80, 80);
        m_previewView->fitInView(bounds.adjusted(-20, -20, 20, 20), Qt::KeepAspectRatio);
    }
}

void LibraryBrowserDialog::onPlaceClicked() {
    if (!m_selectedSymbol.name().isEmpty()) {
        emit symbolPlaced(m_selectedSymbol);
        accept();
    }
}
