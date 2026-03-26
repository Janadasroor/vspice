#include "project_manager.h"
#include "help_window.h"
#include "developer_help_window.h"
#include "../core/ui/project_audit_dialog.h"
#include "schematic_editor.h"
#include "symbol_editor.h"
#include "calculator_dialog.h"
#include "spice_model_manager_dialog.h"
#include "plugin_manager_dialog.h"
#include "../schematic/ui/netlist_editor.h"
#include "csv_viewer.h"
#include "../core/config_manager.h"
#include "../core/settings_dialog.h"
#include "../core/recent_workspaces.h"
#include "../symbols/ltspice_symbol_importer.h"
#include "../simulator/bridge/model_library_manager.h"
#include "../symbols/symbol_library.h"
#include <QPainter>
#include <QPixmap>
#include "project.h"
#include "theme_manager.h"
#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QInputDialog>
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>
#include <QHeaderView>
#include <QStyle>
#include <QScrollArea>
#include <QGridLayout>
#include <QMouseEvent>
#include <QSplitter>
#include <QPainter>
#include <cmath>
#include <QTabWidget>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QTimer>
#include <QResizeEvent>
#include <QDirIterator>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QVector>
#include <QtCore/QSet>
#include <QJsonArray>

#include <cstring>

namespace {
struct PassivePartEntry {
    QString value;
    QString manufacturer;
    QString mpn;
    QString description;
};

QString formatEngineering(double value) {
    struct Unit { double scale; const char* suffix; };
    static const Unit units[] = {
        {1e9, "G"}, {1e6, "Meg"}, {1e3, "k"}, {1.0, ""}, {1e-3, "m"}, {1e-6, "u"}, {1e-9, "n"}, {1e-12, "p"}
    };
    const double a = std::fabs(value);
    for (const auto& u : units) {
        if (a >= u.scale * 0.999 || u.scale == 1e-12) {
            const double scaled = value / u.scale;
            return QString::number(scaled, 'g', 4) + u.suffix;
        }
    }
    return QString::number(value, 'g', 4);
}

QStringList extractAsciiTokens(const QByteArray& raw, int minLen = 2) {
    QStringList out;
    QString cur;
    for (unsigned char c : raw) {
        if (c >= 32 && c <= 126) {
            cur.append(QChar(c));
        } else {
            if (cur.size() >= minLen) out.append(cur);
            cur.clear();
        }
    }
    if (cur.size() >= minLen) out.append(cur);
    return out;
}

QString capValueFromMpn(const QString& mpn) {
    QRegularExpression codeRe("(\\d{3})");
    auto m = codeRe.match(mpn);
    if (!m.hasMatch()) return QString();
    const QString d = m.captured(1);
    const int sig = d.left(2).toInt();
    const int pwr = d.mid(2, 1).toInt();
    const double pf = sig * std::pow(10.0, pwr);
    const double f = pf * 1e-12;
    if (!std::isfinite(f) || f <= 0.0) return QString();
    return formatEngineering(f) + "F";
}

QString indValueFromMpn(const QString& mpn) {
    QRegularExpression rCode("(\\d+)R(\\d)");
    auto r = rCode.match(mpn);
    if (r.hasMatch()) {
        const double uH = QString(r.captured(1) + "." + r.captured(2)).toDouble();
        return formatEngineering(uH * 1e-6) + "H";
    }
    QRegularExpression codeRe("(\\d{3})");
    auto m = codeRe.match(mpn);
    if (!m.hasMatch()) return QString();
    const QString d = m.captured(1);
    const int sig = d.left(2).toInt();
    const int pwr = d.mid(2, 1).toInt();
    const double uH = sig * std::pow(10.0, pwr);
    const double h = uH * 1e-6;
    if (!std::isfinite(h) || h <= 0.0) return QString();
    return formatEngineering(h) + "H";
}

QList<PassivePartEntry> parseStandardRes(const QByteArray& raw) {
    QList<PassivePartEntry> out;
    if (raw.size() < 32) return out;

    const auto readU16Le = [&](int off) -> int {
        if (off + 1 >= raw.size()) return 0;
        const unsigned char b0 = static_cast<unsigned char>(raw[off]);
        const unsigned char b1 = static_cast<unsigned char>(raw[off + 1]);
        return static_cast<int>(b0) | (static_cast<int>(b1) << 8);
    };

    const int clsLen = readU16Le(6);
    int p = 8 + clsLen;
    if (p < 16 || p >= raw.size()) return out;

    // Records are fixed-size blocks with resistance float at +2.
    for (; p + 8 <= raw.size(); p += 16) {
        float rv = 0.0f;
        std::memcpy(&rv, raw.constData() + p + 2, sizeof(float));
        if (!std::isfinite(rv) || rv <= 0.0f || rv > 1e12f) continue;
        PassivePartEntry e;
        const QString valueCode = formatEngineering(rv);
        e.value = valueCode + "Ohm";
        QString mpnCode = valueCode;
        mpnCode.replace('.', 'p');
        mpnCode.replace('-', 'm');
        mpnCode.replace('+', '_');
        e.mpn = "R_STD_" + mpnCode.toUpper();
        e.manufacturer = "LTspice Standard";
        e.description = "LTspice standard resistor";
        out.append(e);
    }
    return out;
}

QList<PassivePartEntry> parseStandardCap(const QByteArray& raw) {
    QList<PassivePartEntry> out;
    QStringList tokens = extractAsciiTokens(raw, 2);
    QString vendor = "";
    QSet<QString> seen;
    QRegularExpression mpnRe("^[A-Za-z0-9][A-Za-z0-9._+-]{3,}$");
    for (int i = 0; i < tokens.size(); ++i) {
        const QString t = tokens[i].trimmed();
        if (t.compare("CCapacitorRec", Qt::CaseInsensitive) == 0) continue;
        if (t.size() <= 6 && t.toUpper() == t && t.contains(QRegularExpression("[A-Z]")) && !t.contains(QRegularExpression("\\d"))) {
            vendor = t;
            continue;
        }
        if (!mpnRe.match(t).hasMatch() || !t.contains(QRegularExpression("\\d"))) continue;
        const QString key = t.toLower();
        if (seen.contains(key)) continue;
        seen.insert(key);
        PassivePartEntry e;
        e.mpn = t;
        e.manufacturer = vendor;
        e.value = capValueFromMpn(t);
        if (i + 1 < tokens.size() && tokens[i + 1].contains(QRegularExpression("[A-Za-z]")) && !tokens[i + 1].contains(QRegularExpression("\\d"))) {
            e.description = tokens[i + 1].trimmed();
        }
        out.append(e);
    }
    return out;
}

QList<PassivePartEntry> parseStandardInd(const QByteArray& raw) {
    QList<PassivePartEntry> out;
    QStringList tokens = extractAsciiTokens(raw, 2);
    QString vendor;
    QSet<QString> seen;
    QRegularExpression mpnRe("^[A-Za-z0-9][A-Za-z0-9._+-]{3,}$");
    for (const QString& tok : tokens) {
        const QString t = tok.trimmed();
        if (t.compare("CInductorRec", Qt::CaseInsensitive) == 0) continue;
        if (t.contains(",") || t.contains("Inc", Qt::CaseInsensitive) || t.contains("Corp", Qt::CaseInsensitive) || t.contains("Ltd", Qt::CaseInsensitive)) {
            vendor = t;
            continue;
        }
        if (!mpnRe.match(t).hasMatch() || !t.contains(QRegularExpression("\\d"))) continue;
        const QString key = t.toLower();
        if (seen.contains(key)) continue;
        seen.insert(key);
        PassivePartEntry e;
        e.mpn = t;
        e.manufacturer = vendor;
        e.value = indValueFromMpn(t);
        e.description = "LTspice standard inductor";
        out.append(e);
    }
    return out;
}

bool importStandardPassiveCatalog(QWidget* parent,
                                  const QString& srcFile,
                                  const QString& kind,
                                  QString* errorOut = nullptr) {
    QFile file(srcFile);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorOut) *errorOut = "Cannot open file";
        return false;
    }
    const QByteArray raw = file.readAll();
    file.close();

    QList<PassivePartEntry> entries;
    if (kind == "resistor") entries = parseStandardRes(raw);
    else if (kind == "capacitor") entries = parseStandardCap(raw);
    else if (kind == "inductor") entries = parseStandardInd(raw);

    if (entries.isEmpty()) {
        if (errorOut) *errorOut = "No passive entries parsed";
        return false;
    }

    QJsonArray arr;
    QSet<QString> dedup;
    for (const auto& e : entries) {
        const QString k = (e.mpn + "|" + e.value + "|" + e.manufacturer).toLower();
        if (dedup.contains(k)) continue;
        dedup.insert(k);
        QJsonObject o;
        o["value"] = e.value;
        o["manufacturer"] = e.manufacturer;
        o["mpn"] = e.mpn;
        o["description"] = e.description;
        arr.append(o);
    }

    const QString dstDir = QDir::homePath() + "/ViospiceLib/lib";
    QDir().mkpath(dstDir);
    const QString dst = QDir(dstDir).filePath(QString("passive_catalog_%1.json").arg(kind));

    QFile out(dst);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorOut) *errorOut = "Cannot write catalog json";
        return false;
    }
    QJsonObject root;
    root["kind"] = kind;
    root["source"] = srcFile;
    root["entries"] = arr;
    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    out.close();

    QMessageBox::information(parent,
                             "Passive Catalog Import",
                             QString("Imported %1 %2 entries from:\n%3\n\nCatalog:\n%4")
                                 .arg(arr.size())
                                 .arg(kind)
                                 .arg(srcFile)
                                 .arg(dst));
    return true;
}

QString decodeSpiceText(const QByteArray& raw) {
    if (raw.isEmpty()) return QString();

    auto decodeUtf16Le = [](const QByteArray& bytes, int start) {
        QVector<ushort> u16;
        u16.reserve((bytes.size() - start) / 2);
        for (int i = start; i + 1 < bytes.size(); i += 2) {
            const ushort ch = static_cast<ushort>(static_cast<unsigned char>(bytes[i])) |
                              (static_cast<ushort>(static_cast<unsigned char>(bytes[i + 1])) << 8);
            u16.push_back(ch);
        }
        return QString::fromUtf16(u16.constData(), u16.size());
    };

    auto decodeUtf16Be = [](const QByteArray& bytes, int start) {
        QVector<ushort> u16;
        u16.reserve((bytes.size() - start) / 2);
        for (int i = start; i + 1 < bytes.size(); i += 2) {
            const ushort ch = (static_cast<ushort>(static_cast<unsigned char>(bytes[i])) << 8) |
                               static_cast<ushort>(static_cast<unsigned char>(bytes[i + 1]));
            u16.push_back(ch);
        }
        return QString::fromUtf16(u16.constData(), u16.size());
    };

    if (raw.size() >= 2) {
        const unsigned char b0 = static_cast<unsigned char>(raw[0]);
        const unsigned char b1 = static_cast<unsigned char>(raw[1]);
        if (b0 == 0xFF && b1 == 0xFE) return decodeUtf16Le(raw, 2);
        if (b0 == 0xFE && b1 == 0xFF) return decodeUtf16Be(raw, 2);
    }

    int oddZeros = 0;
    int evenZeros = 0;
    const int n = raw.size();
    for (int i = 0; i < n; ++i) {
        if (raw[i] == '\0') {
            if (i % 2 == 0) ++evenZeros;
            else ++oddZeros;
        }
    }
    if (oddZeros > n / 8) return decodeUtf16Le(raw, 0);
    if (evenZeros > n / 8) return decodeUtf16Be(raw, 0);

    return QString::fromUtf8(raw);
}

bool importStandardPassiveModelFile(QWidget* parent,
                                    const QString& srcFile,
                                    const QString& title,
                                    const QString& outputLibName,
                                    const QSet<QString>& acceptedTypeTokens,
                                    const QString& readableTypeName) {
    QFile file(srcFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(parent, title, "Cannot open file:\n" + srcFile);
        return false;
    }
    const QString content = decodeSpiceText(file.readAll());
    file.close();

    QStringList logicalLines;
    for (const QString& raw : content.split('\n')) {
        const QString t = raw.trimmed();
        if (t.isEmpty()) continue;
        if (!logicalLines.isEmpty() && t.startsWith('+')) {
            logicalLines.last().append(' ' + t.mid(1).trimmed());
        } else {
            logicalLines.append(raw);
        }
    }

    const QRegularExpression modelHeadRe(
        R"(^\s*[\x{FEFF}]*\.model\s+(\S+)\s+([^\s(]+))",
        QRegularExpression::CaseInsensitiveOption);

    QStringList models;
    int modelLines = 0;
    for (const QString& line : logicalLines) {
        const QString trimmed = line.trimmed();
        const QRegularExpressionMatch headMatch = modelHeadRe.match(trimmed);
        if (!headMatch.hasMatch()) continue;
        modelLines++;
        const QString typeToken = headMatch.captured(2).trimmed().toLower();
        if (!acceptedTypeTokens.contains(typeToken)) continue;
        models.append(trimmed);
    }

    if (models.isEmpty()) {
        const QString lowerName = QFileInfo(srcFile).fileName().toLower();
        const bool isLtspiceComponentDb =
            (lowerName == "standard.res" || lowerName == "standard.cap" || lowerName == "standard.ind");

        QString kindKey = readableTypeName.trimmed().toLower();
        if (isLtspiceComponentDb && (kindKey == "resistor" || kindKey == "capacitor" || kindKey == "inductor")) {
            QString importErr;
            if (importStandardPassiveCatalog(parent, srcFile, kindKey, &importErr)) {
                return true;
            }
        }

        QString msg = QString("No %1 .model lines found in file.\n\nDetected .model lines: %2")
                          .arg(readableTypeName)
                          .arg(modelLines);
        if (isLtspiceComponentDb) {
            msg += "\n\nNote: LTspice standard.res/standard.cap/standard.ind are component database files, "
                   "not SPICE .model libraries.\n"
                   "Choose a SPICE model text file (e.g. .lib/.mod/.inc) that contains .model lines.";
        }
        QMessageBox::information(
            parent,
            title,
            msg);
        return false;
    }

    const QString defaultDst = QDir::homePath() + "/ViospiceLib/lib";
    QDir().mkpath(defaultDst);
    const QString outputPath = QDir(defaultDst).filePath(outputLibName);

    QFile out(outputPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QMessageBox::warning(parent, title, "Cannot write file:\n" + outputPath);
        return false;
    }

    QTextStream ts(&out);
    ts << "* " << outputLibName << " - Imported from " << QFileInfo(srcFile).fileName() << "\n";
    ts << "* Type: " << readableTypeName << "\n";
    ts << "* Count: " << models.size() << " models\n";
    ts << "*\n";
    for (const QString& model : models) {
        ts << model << "\n";
    }
    out.close();

    ModelLibraryManager::instance().reload();

    QMessageBox::information(
        parent,
        title,
        QString("Imported %1 models from:\n%2\n\nInto:\n%3")
            .arg(models.size())
            .arg(srcFile)
            .arg(outputPath));
    return true;
}
}

ProjectManager::ProjectManager(QWidget* parent)
    : QMainWindow(parent), m_workspaceDirty(false), m_workspaceFilePath(QString()) {
    setWindowTitle("viospice");
    setMinimumSize(900, 600);
    
    applyKiCadStyle();
    setupUI();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &ProjectManager::updateThemeStyle);

    // Enable drag & drop
    setAcceptDrops(true);

    // Try to load last project or show empty
    updateRecentProjectsMenu();

    // Load saved workspace folders or default to Documents
    QStringList savedFolders = ConfigManager::instance().workspaceFolders();
    if (!savedFolders.isEmpty()) {
        for (const QString& folder : savedFolders) {
            if (QDir(folder).exists() && !m_workspaceFolders.contains(folder)) {
                m_workspaceFolders.append(folder);
            }
        }
    }
    
    // If no folders loaded, default to Documents
    if (m_workspaceFolders.isEmpty()) {
        QString docPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (QDir(docPath).exists()) {
            m_workspaceFolders.append(docPath);
        }
    }
    
    refreshProjectTree();

    // Restore UI State
    QByteArray geom = ConfigManager::instance().windowGeometry("ProjectManager");
    QByteArray state = ConfigManager::instance().windowState("ProjectManager");
    if (!geom.isEmpty()) restoreGeometry(geom);
    if (!state.isEmpty()) restoreState(state);
}

ProjectManager::~ProjectManager() {}

void ProjectManager::closeEvent(QCloseEvent* event) {
    if (m_workspaceDirty && (!m_workspaceFilePath.isEmpty() || m_workspaceFolders.size() > 1)) {
        int ret = QMessageBox::question(this, "Save Workspace",
            "Would you like to save the workspace before closing?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (ret == QMessageBox::Save) {
            saveWorkspace();
        } else if (ret == QMessageBox::Cancel) {
            event->ignore();
            return;
        }
    }
    
    ConfigManager::instance().saveWindowState("ProjectManager", saveGeometry(), saveState());
    ConfigManager::instance().setWorkspaceFolders(m_workspaceFolders);
    event->accept();
}

void ProjectManager::setupUI() {
    createMenuBar();
    
    m_centralWidget = new QWidget;
    setCentralWidget(m_centralWidget);

    QHBoxLayout* mainLayout = new QHBoxLayout(m_centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Splitter for project files and launcher area
    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->setChildrenCollapsible(false);
    
    // Project Files Panel
    m_projectPanel = createProjectFilesPanel();
    m_splitter->addWidget(m_projectPanel);
    
    // Launcher Area
    m_launcherArea = createLauncherArea();
    m_splitter->addWidget(m_launcherArea);
    
    // Set initial sizes (250px for tree, rest for launcher)
    m_splitter->setSizes({250, 650});
    
    mainLayout->addWidget(m_splitter);

    // Status bar
    statusBar()->showMessage("Project: (no project loaded)");
}

QWidget* ProjectManager::createProjectFilesPanel() {
    QWidget* panel = new QWidget;
    panel->setObjectName("ProjectFilesPanel");
    panel->setMinimumWidth(220);
    
    QVBoxLayout* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Header
    QLabel* header = new QLabel("  EXPLORER");
    header->setObjectName("PanelHeader");
    layout->addWidget(header);

    // Search field
    auto* searchField = new QLineEdit();
    searchField->setPlaceholderText("  Search files...");
    searchField->setClearButtonEnabled(true);
    searchField->setObjectName("ProjectSearch");
    layout->addWidget(searchField);

    // Tree widget
    m_projectTree = new QTreeWidget;
    m_projectTree->setHeaderHidden(true);
    m_projectTree->setRootIsDecorated(true);
    m_projectTree->setObjectName("ProjectTree");
    m_projectTree->setAnimated(true);
    m_projectTree->setIndentation(16);
    m_projectTree->setContextMenuPolicy(Qt::CustomContextMenu);
    
    connect(m_projectTree, &QTreeWidget::customContextMenuRequested,
            this, &ProjectManager::onProjectTreeContextMenu);
    
    connect(m_projectTree, &QTreeWidget::itemDoubleClicked,
            this, &ProjectManager::onProjectTreeItemDoubleClicked);

    // Search filtering
    connect(searchField, &QLineEdit::textChanged, this, [this](const QString& text) {
        std::function<bool(QTreeWidgetItem*)> filterItem = [&](QTreeWidgetItem* item) -> bool {
            bool childVisible = false;
            for (int i = 0; i < item->childCount(); ++i) {
                if (filterItem(item->child(i))) childVisible = true;
            }
            bool match = text.isEmpty() || item->text(0).contains(text, Qt::CaseInsensitive);
            item->setHidden(!match && !childVisible);
            return match || childVisible;
        };
        for (int i = 0; i < m_projectTree->topLevelItemCount(); ++i) {
            filterItem(m_projectTree->topLevelItem(i));
        }
    });
    
    layout->addWidget(m_projectTree);

    return panel;
}

// Helper to add a launcher tile to the grid
void ProjectManager::addLauncherTile(QGridLayout* grid, int row, int col, 
                                     const QString& title, const QString& desc, 
                                     const QString& iconPath, 
                                     void (ProjectManager::*slot)()) {
    LauncherTile* tile = new LauncherTile("", title, desc);
    tile->setIcon(QIcon(iconPath));
    connect(tile, &LauncherTile::clicked, this, slot);
    grid->addWidget(tile, row, col);
    m_launcherTiles.append(tile);
}

QWidget* ProjectManager::createLauncherArea() {
    QScrollArea* scroll = new QScrollArea;
    scroll->setObjectName("LauncherScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("QScrollArea { background-color: #0a0a0c; border: none; }");

    m_launcherScrollContent = new QWidget;
    m_launcherScrollContent->setObjectName("LauncherArea");
    
    QVBoxLayout* layout = new QVBoxLayout(m_launcherScrollContent);
    layout->setContentsMargins(40, 30, 40, 20);
    layout->setSpacing(24);

    // Welcome header
    QLabel* welcomeLabel = new QLabel("viospice");
    welcomeLabel->setObjectName("WelcomeTitle");
    layout->addWidget(welcomeLabel);

    QLabel* subtitleLabel = new QLabel("Professional Electronic Design Automation");
    subtitleLabel->setObjectName("WelcomeSubtitle");
    layout->addWidget(subtitleLabel);

    // Thin accent line
    QFrame* accentLine = new QFrame();
    accentLine->setFixedHeight(2);
    accentLine->setStyleSheet("background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #007acc, stop:0.5 #00c8ff, stop:1 transparent);");
    layout->addWidget(accentLine);

    // Grid layout for launcher tiles (will be managed by updateLauncherLayout)
    m_launcherGrid = new QGridLayout;
    m_launcherGrid->setSpacing(24);
    m_launcherGrid->setContentsMargins(0, 10, 0, 10);
    m_launcherGrid->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    // Initialize tiles list if not already
    m_launcherTiles.clear();

    auto createAndStoreTile = [&](const QString& title, const QString& desc, const QString& iconPath, void (ProjectManager::*slot)()) {
        LauncherTile* tile = new LauncherTile("", title, desc);
        tile->setIcon(QIcon(iconPath));
        connect(tile, &LauncherTile::clicked, this, slot);
        m_launcherTiles.append(tile);
    };

    createAndStoreTile("Schematic Editor", "Capture and edit circuit schematics", ":/icons/schematic_editor.png", &ProjectManager::openSchematicEditor);
    createAndStoreTile("Symbol Editor", "Create and manage schematic symbol libraries", ":/icons/symbol_editor.png", &ProjectManager::openSymbolEditor);
    createAndStoreTile("LTspice Batch Import", "Import LTspice .asy symbols to .viosym", ":/icons/toolbar_file.png", &ProjectManager::importLtspiceBatch);
    createAndStoreTile("Diode/JFET Import", "Import LTspice .dio/.jft model libraries by type", ":/icons/toolbar_netlist.png", &ProjectManager::importLtspiceDiodeModels);
    createAndStoreTile("JFET Model Import", "Import LTspice .jft model libraries (NJF/PJF)", ":/icons/toolbar_netlist.png", &ProjectManager::importLtspiceJfetModels);
    createAndStoreTile("BJT Model Import", "Import LTspice .bjt model libraries (NPN/PNP)", ":/icons/toolbar_netlist.png", &ProjectManager::importLtspiceBjtModels);
    createAndStoreTile("MOS Model Import", "Import LTspice .mos model libraries (NMOS/PMOS)", ":/icons/toolbar_netlist.png", &ProjectManager::importLtspiceMosModels);
    createAndStoreTile("Resistor Model Import", "Import LTspice standard.res resistor models", ":/icons/toolbar_netlist.png", &ProjectManager::importLtspiceResistorModels);
    createAndStoreTile("Capacitor Model Import", "Import LTspice standard.cap capacitor models", ":/icons/toolbar_netlist.png", &ProjectManager::importLtspiceCapacitorModels);
    createAndStoreTile("Inductor Model Import", "Import LTspice standard.ind inductor models", ":/icons/toolbar_netlist.png", &ProjectManager::importLtspiceInductorModels);
    createAndStoreTile("Import Standard Passives", "Import standard.res, standard.cap, and standard.ind in one run", ":/icons/toolbar_netlist.png", &ProjectManager::importLtspiceStandardPassiveModels);
    createAndStoreTile("SPICE Model Manager", "Manage simulation models and subcircuit libraries", ":/icons/toolbar_netlist.png", &ProjectManager::openSpiceModelManager);
    createAndStoreTile("Calculator Tools", "Resistance, trace width, and impedance calculators", ":/icons/calculator_tools.png", &ProjectManager::openCalculatorTools);
    createAndStoreTile("Plugins Manager", "Manage extensions, importers, and add-ons", ":/icons/plugins_manager.png", &ProjectManager::openPluginsManager);
    createAndStoreTile("Help Documentation", "Software guides, tutorials, and documentation", ":/icons/tool_search.svg", &ProjectManager::showHelp);

    layout->addLayout(m_launcherGrid);
    layout->addStretch();

    // Version footer
    QLabel* versionLabel = new QLabel("viospice v0.2.0  ·  Modern Engineering Suite");
    versionLabel->setObjectName("VersionFooter");
    versionLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(versionLabel);

    scroll->setWidget(m_launcherScrollContent);
    
    // Trigger initial layout
    QTimer::singleShot(0, this, &ProjectManager::updateLauncherLayout);

    return scroll;
}

void ProjectManager::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    updateLauncherLayout();
}

void ProjectManager::updateLauncherLayout() {
    if (!m_launcherGrid || m_launcherTiles.isEmpty()) return;

    // Calculate available width for the grid
    int availableWidth = m_launcherScrollContent->width() - 80; // Accounting for margins
    int tileMinWidth = 320; // 300px min-width + 20px spacing buffer
    
    int columns = qMax(1, availableWidth / tileMinWidth);
    
    // Clear current grid (without deleting tiles)
    while (m_launcherGrid->count() > 0) {
        m_launcherGrid->takeAt(0);
    }

    // Re-populate grid based on columns
    for (int i = 0; i < m_launcherTiles.size(); ++i) {
        int row = i / columns;
        int col = i % columns;
        m_launcherGrid->addWidget(m_launcherTiles[i], row, col);
    }
}

void ProjectManager::refreshProjectTree() {
    m_projectTree->clear();
    
    if (m_workspaceFolders.isEmpty()) {
       statusBar()->showMessage("No folders in workspace");
       return;
    }

    QTreeWidgetItem* parentItem = nullptr;
    bool isWorkspace = !m_workspaceFilePath.isEmpty() || m_workspaceFolders.size() > 1;
    
    if (isWorkspace) {
        QString workspaceName = "Untitled";
        if (!m_workspaceFilePath.isEmpty()) {
            workspaceName = QFileInfo(m_workspaceFilePath).completeBaseName();
        } else if (!m_workspaceFolders.isEmpty()) {
            workspaceName = QDir(m_workspaceFolders.first()).dirName();
        }
        
        parentItem = new QTreeWidgetItem(m_projectTree);
        parentItem->setText(0, workspaceName + " (Workspace)");
        parentItem->setIcon(0, createFolderIcon(true)); // Optional, add icon for workspace
        parentItem->setExpanded(true);
    }

    for (const QString& path : m_workspaceFolders) {
        addFolderToTree(path, parentItem);
    }
    
    if (m_workspaceFolders.size() == 1) {
       statusBar()->showMessage("Workspace: " + m_workspaceFolders.first());
    } else {
       statusBar()->showMessage(QString("%1 folders in workspace").arg(m_workspaceFolders.size()));
    }
}

void ProjectManager::addFolderToTree(const QString& folderPath, QTreeWidgetItem* parent) {
    QDir dir(folderPath);
    if (!dir.exists()) return;

    // Create root item for folder
    QTreeWidgetItem* folderRoot = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_projectTree);
    folderRoot->setText(0, dir.dirName());
    folderRoot->setIcon(0, createFolderIcon(true));
    folderRoot->setData(0, Qt::UserRole, folderPath);
    folderRoot->setExpanded(true);

    QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    QStringList allFiles = dir.entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    
    // Folders to skip (common source repo folders to avoid clutter)
    QStringList skipFolders = {
        "build", "core", "ui", "schematic", "symbols", "scripts", "tests", "include", 
        "cmake", ".git", "python", "cli", "linux", "resources", "CMakeFiles", "_deps",
        "venv", ".qt", "footprints", "pcb", "reverse_engineering", "simulator"
    };

    // Add subdirectories under folderRoot
    for (const QString& subdir : subdirs) {
        if (skipFolders.contains(subdir)) continue;
        
        QTreeWidgetItem* item = new QTreeWidgetItem(folderRoot);
        item->setText(0, subdir);
        item->setIcon(0, createFolderIcon(false));
        item->setData(0, Qt::UserRole, dir.absoluteFilePath(subdir));
    }

    // Identify Flux Projects
    QStringList fluxFiles;
    for (const QString& f : allFiles) {
        if (f.endsWith(".flux", Qt::CaseInsensitive))
             fluxFiles << f;
    }
    
    QMap<QString, QTreeWidgetItem*> projectMap;
    
    // Add Flux Projects under folderRoot
    for (const QString& f : fluxFiles) {
        QTreeWidgetItem* item = new QTreeWidgetItem(folderRoot);
        QString absPath = dir.absoluteFilePath(f);
        item->setData(0, Qt::UserRole, absPath);
        item->setIcon(0, createDocumentIcon(QColor("#10b981"), "F"));
        item->setText(0, f);
        item->setExpanded(true);
        projectMap[QFileInfo(f).completeBaseName()] = item;
    }
    
    // Add Files Hierarchically
    QTreeWidgetItem* miscRoot = nullptr;
    
    QStringList projectExtensions = {
        "flux", "sch", "kicad_sch", "SchDoc", "flxsch",            // Schematics
        "sym", "sclib", "lib", "model", "viosym",                 // Symbols / Models
        "cir", "net", "sp", "spice"                               // Netlists
    };

    for (const QString& f : allFiles) {
        // Skip hidden or system files explicitly
        if (f.startsWith(".")) continue;
        if (f.endsWith(".flux", Qt::CaseInsensitive)) continue; // Already handled
        
        QFileInfo fi(f);
        QString ext = fi.suffix().toLower();
        
        // Filter: only show project-related files (EXCLUDE source code and build configs)
        if (!projectExtensions.contains(ext)) continue;

        QTreeWidgetItem* parentItem = nullptr;
        
        // Try to associate file with a project
        if (projectMap.contains(fi.completeBaseName())) {
            parentItem = projectMap[fi.completeBaseName()];
        } else if (projectMap.contains(fi.baseName())) {
            parentItem = projectMap[fi.baseName()];
        }
        
        // If no project association found, put in Miscellaneous or root
        if (!parentItem) {
             if (!fluxFiles.isEmpty()) {
                 if (!miscRoot) {
                     miscRoot = new QTreeWidgetItem(folderRoot);
                     miscRoot->setText(0, "Project Assets");
                     miscRoot->setIcon(0, createFolderIcon(true));
                     miscRoot->setExpanded(true);
                 }
                 parentItem = miscRoot;
             } else {
                 parentItem = folderRoot; 
             }
        }
        
        QTreeWidgetItem* item = new QTreeWidgetItem(parentItem);
        item->setText(0, f);
        item->setData(0, Qt::UserRole, dir.absoluteFilePath(f));
        item->setIcon(0, getProjectFileIcon(f));
    }
}

QIcon ProjectManager::createFolderIcon(bool open) const {
    const int size = 20;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    
    QColor folderColor = QColor("#d4d4d8"); // Modern Zinc/Slate color
    
    if (open) {
        // Draw an open folder profile
        QPainterPath path;
        path.moveTo(2, 4);
        path.lineTo(7, 4);
        path.lineTo(9, 6);
        path.lineTo(18, 6);
        path.lineTo(18, 16);
        path.lineTo(2, 16);
        path.closeSubpath();
        
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#3f3f46"));
        painter.drawPath(path);
        
        QPainterPath front;
        front.moveTo(2, 8);
        front.lineTo(18, 8);
        front.lineTo(16, 17);
        front.lineTo(4, 17);
        front.closeSubpath();
        
        QLinearGradient grad(0, 8, 0, 17);
        grad.setColorAt(0, folderColor);
        grad.setColorAt(1, QColor("#a1a1aa"));
        
        painter.setBrush(grad);
        painter.drawPath(front);
    } else {
        // Draw a closed folder profile
        QPainterPath path;
        path.moveTo(2, 4);
        path.lineTo(7, 4);
        path.lineTo(9, 6);
        path.lineTo(18, 6);
        path.lineTo(18, 15);
        path.lineTo(2, 15);
        path.closeSubpath();
        
        QLinearGradient grad(0, 4, 0, 15);
        grad.setColorAt(0, folderColor);
        grad.setColorAt(1, QColor("#a1a1aa"));
        
        painter.setPen(Qt::NoPen);
        painter.setBrush(grad);
        painter.drawPath(path);
        
        // Tab highlight
        painter.setBrush(folderColor);
        painter.drawRect(2, 4, 5, 2);
    }
    
    return QIcon(pixmap);
}

QIcon ProjectManager::createDocumentIcon(const QColor& accentColor, const QString& label) const {
    const int size = 20;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Draw document base (rounded rect with fold)
    QRectF docRect(2, 2, 16, 16);
    QPainterPath path;
    path.addRoundedRect(docRect, 3, 3);
    
    // Fill with a dark base and light top border
    QLinearGradient grad(0, 0, 0, size);
    grad.setColorAt(0, QColor("#22242a"));
    grad.setColorAt(1, QColor("#1a1c22"));
    
    painter.setPen(Qt::NoPen);
    painter.setBrush(grad);
    painter.drawPath(path);
    
    // Draw accent bar or left border
    painter.setBrush(accentColor);
    painter.drawRoundedRect(2, 2, 3, 16, 2, 2);
    
    painter.setPen(QPen(QColor(255, 255, 255, 20), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
    
    // Draw label
    painter.setPen(QColor("#a0a0a0"));
    QFont font = painter.font();
    font.setPointSize(8);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(docRect.adjusted(3, 0, 0, 0), Qt::AlignCenter, label);
    
    return QIcon(pixmap);
}

QIcon ProjectManager::getProjectFileIcon(const QString& fileName) const {
    if (fileName.endsWith(".flux", Qt::CaseInsensitive)) {
        return createDocumentIcon(QColor("#10b981"), "F");
    } else if (fileName.endsWith(".pcb", Qt::CaseInsensitive)) {
        return createDocumentIcon(QColor("#8b5cf6"), "P");
    } else if (fileName.endsWith(".sch", Qt::CaseInsensitive) || fileName.endsWith(".kicad_sch", Qt::CaseInsensitive) || fileName.endsWith(".SchDoc", Qt::CaseInsensitive) || fileName.endsWith(".flxsch", Qt::CaseInsensitive)) {
        return createDocumentIcon(QColor("#3b82f6"), "S");
    } else if (fileName.endsWith(".sym", Qt::CaseInsensitive) || fileName.endsWith(".sclib", Qt::CaseInsensitive) || fileName.endsWith(".lib", Qt::CaseInsensitive) || fileName.endsWith(".viosym", Qt::CaseInsensitive)) {
        return createDocumentIcon(QColor("#f59e0b"), "L"); // Library / Symbol
    } else if (fileName.endsWith(".cir", Qt::CaseInsensitive) || fileName.endsWith(".net", Qt::CaseInsensitive) || fileName.endsWith(".sp", Qt::CaseInsensitive) || fileName.endsWith(".model", Qt::CaseInsensitive)) {
        return createDocumentIcon(QColor("#ec4899"), "N"); // Netlist / Model
    } 
    return createDocumentIcon(QColor("#64748b"), "D");
}

void ProjectManager::addFolderToWorkspace() {
    QString folder = QFileDialog::getExistingDirectory(this, "Add Folder to Workspace", 
                                                     QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
    if (!folder.isEmpty() && !m_workspaceFolders.contains(folder)) {
        m_workspaceFolders.append(folder);
        m_workspaceDirty = true;
        refreshProjectTree();
    }
}

void ProjectManager::saveWorkspace() {
    // Only save workspace file if multiple folders
    if (m_workspaceFolders.size() <= 1) {
        m_workspaceDirty = false;
        return;
    }
    
    QString filePath = m_workspaceFilePath;
    if (filePath.isEmpty()) {
        filePath = QFileDialog::getSaveFileName(this, "Save Workspace",
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/untitled.viospice-workspace",
            "viospice Workspace (*.viospice-workspace)");
        if (filePath.isEmpty()) return;
        if (!filePath.endsWith(".viospice-workspace")) {
            filePath += ".viospice-workspace";
        }
    }
    
    QJsonObject root;
    QJsonArray folders;
    for (const QString& folder : m_workspaceFolders) {
        QJsonObject folderObj;
        folderObj["path"] = folder;
        folders.append(folderObj);
    }
    root["folders"] = folders;
    root["settings"] = QJsonObject();
    
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
        m_workspaceFilePath = filePath;
        m_workspaceDirty = false;
        RecentWorkspaces::instance().addWorkspace(filePath);
        updateRecentProjectsMenu();
        setWindowTitle("viospice - " + QFileInfo(filePath).fileName());
    }
}

void ProjectManager::loadWorkspace(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return;
    
    QJsonObject root = doc.object();
    QJsonArray folders = root["folders"].toArray();
    
    m_workspaceFolders.clear();
    QString workspaceDir = QFileInfo(path).absolutePath();
    
    for (const QJsonValue& val : folders) {
        QString folderPath = val.toObject()["path"].toString();
        if (!folderPath.isEmpty()) {
            // Handle relative paths
            if (QFileInfo(folderPath).isRelative()) {
                folderPath = workspaceDir + "/" + folderPath;
            }
            if (QDir(folderPath).exists() && !m_workspaceFolders.contains(folderPath)) {
                m_workspaceFolders.append(folderPath);
            }
        }
    }
    
    m_workspaceFilePath = path;
    m_workspaceDirty = false;
    RecentWorkspaces::instance().addWorkspace(path);
    updateRecentProjectsMenu();
    setWindowTitle("viospice - " + QFileInfo(path).fileName());
    refreshProjectTree();
}

void ProjectManager::onProjectTreeItemDoubleClicked(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column)
    QString path = item->data(0, Qt::UserRole).toString();
    if (path.isEmpty()) return;

    QFileInfo info(path);
    if (info.isDir()) return;

    if (path.endsWith(".sch", Qt::CaseInsensitive) || 
        path.endsWith(".kicad_sch", Qt::CaseInsensitive) || 
        path.endsWith(".SchDoc", Qt::CaseInsensitive) ||
        path.endsWith(".flxsch", Qt::CaseInsensitive)) {
        launchSchematicEditor(path);
    } else if (path.endsWith(".flux", Qt::CaseInsensitive)) {
        openProject(path);
    } else if (path.endsWith(".sym", Qt::CaseInsensitive) || 
               path.endsWith(".sclib", Qt::CaseInsensitive) || 
               path.endsWith(".viosym", Qt::CaseInsensitive)) {
        SymbolEditor* editor = new SymbolEditor(nullptr);
        editor->setAttribute(Qt::WA_DeleteOnClose);
        // Basic fallback until loadLibrary is fully implemented
        editor->loadLibrary(path); 
        editor->show();
    } else if (path.endsWith(".cir", Qt::CaseInsensitive) || 
               path.endsWith(".net", Qt::CaseInsensitive) || 
               path.endsWith(".sp", Qt::CaseInsensitive) ||
               path.endsWith(".spice", Qt::CaseInsensitive) ||
               path.endsWith(".model", Qt::CaseInsensitive) ||
               path.endsWith(".lib", Qt::CaseInsensitive) ||
               path.endsWith(".txt", Qt::CaseInsensitive)) {
        NetlistEditor* editor = new NetlistEditor();
        editor->setAttribute(Qt::WA_DeleteOnClose);
        editor->loadFile(path);
        editor->show();
    } else if (path.endsWith(".csv", Qt::CaseInsensitive)) {
        CsvViewer* viewer = new CsvViewer();
        viewer->setAttribute(Qt::WA_DeleteOnClose);
        viewer->loadFile(path);
        viewer->show();
    }
}

void ProjectManager::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void ProjectManager::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void ProjectManager::dropEvent(QDropEvent* event) {
    const QMimeData* mimeData = event->mimeData();
    if (mimeData->hasUrls()) {
        QList<QUrl> urlList = mimeData->urls();
        for (const QUrl& url : urlList) {
            QString path = url.toLocalFile();
            if (QFileInfo(path).isDir()) {
                if (!m_workspaceFolders.contains(path)) {
                    m_workspaceFolders.append(path);
                    m_workspaceDirty = true;
                }
            } else if (path.endsWith(".viospice-workspace", Qt::CaseInsensitive)) {
                loadWorkspace(path);
            }
        }
        refreshProjectTree();
        event->acceptProposedAction();
    }
}

void ProjectManager::onProjectTreeContextMenu(const QPoint& pos) {
    QTreeWidgetItem* item = m_projectTree->itemAt(pos);
    if (!item) return;

    QMenu menu(this);
    
    QString path = item->data(0, Qt::UserRole).toString();
    QFileInfo info(path);

    // Identify Project Context
    QTreeWidgetItem* root = item;
    while (root->parent()) {
        root = root->parent();
    }
    QString projectRootPath = root->data(0, Qt::UserRole).toString();
    
    if (m_workspaceFolders.contains(projectRootPath)) {
        QString projectName;
        QDir d(projectRootPath);
        QStringList flux = d.entryList(QStringList() << "*.flux", QDir::Files);
        if (!flux.empty()) projectName = QFileInfo(flux.first()).completeBaseName();
        else projectName = d.dirName();
        
        QString schPath = projectRootPath + "/" + projectName + ".sch";
        
        bool missingSch = !QFile::exists(schPath);
        
        if (missingSch) {
             QAction* addSch = menu.addAction("Add Schematic");
             addSch->setIcon(QIcon(":/icons/file_flux_sch.png"));
             connect(addSch, &QAction::triggered, [this, schPath]() {
                 QFile f(schPath);
                 if (f.open(QIODevice::WriteOnly)) { f.write("{}"); f.close(); }
                 refreshProjectTree();
             });
             menu.addSeparator();
        }
    }
    
    // Add Remove from Workspace for workspace folders (roots)
    if (m_workspaceFolders.contains(path)) {
        QAction* removeAction = menu.addAction("Remove from Workspace");
        removeAction->setIcon(QIcon(":/icons/close.png"));
        connect(removeAction, &QAction::triggered, [this, path](){
             m_workspaceFolders.removeAll(path);
             refreshProjectTree();
        });
        menu.addSeparator();
    }

    QAction* openAction = menu.addAction("Open");
    connect(openAction, &QAction::triggered, [=]() {
        onProjectTreeItemDoubleClicked(item, 0);
    });

    // New File sub-menu
    if (info.isDir()) {
        menu.addSeparator();
        QMenu* newMenu = menu.addMenu("New");
        
        QAction* newSch = newMenu->addAction("Schematic (.flxsch)");
        newSch->setIcon(getProjectFileIcon("a.flxsch"));
        connect(newSch, &QAction::triggered, [this, path]() {
            QString name = QInputDialog::getText(this, "New Schematic", "File name:");
            if (name.isEmpty()) return;
            if (!name.endsWith(".flxsch")) name += ".flxsch";
            QFile f(path + "/" + name);
            if (f.open(QIODevice::WriteOnly)) { f.write("{}"); f.close(); }
            refreshProjectTree();
        });

        QAction* newSym = newMenu->addAction("Symbol Library (.viosym)");
        newSym->setIcon(getProjectFileIcon("a.viosym"));
        connect(newSym, &QAction::triggered, [this, path]() {
            QString name = QInputDialog::getText(this, "New Symbol Library", "File name:");
            if (name.isEmpty()) return;
            if (!name.endsWith(".viosym")) name += ".viosym";
            QFile f(path + "/" + name);
            if (f.open(QIODevice::WriteOnly)) { f.write("{\"symbols\": []}"); f.close(); }
            refreshProjectTree();
        });

        QAction* newNet = newMenu->addAction("SPICE Netlist (.net)");
        newNet->setIcon(getProjectFileIcon("a.net"));
        connect(newNet, &QAction::triggered, [this, path]() {
            QString name = QInputDialog::getText(this, "New Netlist", "File name:");
            if (name.isEmpty()) return;
            if (!name.endsWith(".net")) name += ".net";
            QFile f(path + "/" + name);
            if (f.open(QIODevice::WriteOnly)) { f.write("* New SPICE Netlist\n"); f.close(); }
            refreshProjectTree();
        });

        QAction* newLib = newMenu->addAction("Model Library (.lib)");
        newLib->setIcon(getProjectFileIcon("a.lib"));
        connect(newLib, &QAction::triggered, [this, path]() {
            QString name = QInputDialog::getText(this, "New Model Library", "File name:");
            if (name.isEmpty()) return;
            if (!name.endsWith(".lib")) name += ".lib";
            QFile f(path + "/" + name);
            if (f.open(QIODevice::WriteOnly)) { f.write("* New Model Library\n"); f.close(); }
            refreshProjectTree();
        });
    }

    menu.addSeparator();

    // Common file/folder operations
    if (!m_workspaceFolders.contains(path)) {
        QAction* renameAction = menu.addAction("Rename...");
        connect(renameAction, &QAction::triggered, [this, path, info]() {
            QString newName = QInputDialog::getText(this, "Rename", "New name:", QLineEdit::Normal, info.fileName());
            if (newName.isEmpty() || newName == info.fileName()) return;
            
            QString newPath = info.absolutePath() + "/" + newName;
            if (QFile::rename(path, newPath)) {
                refreshProjectTree();
            } else {
                QMessageBox::warning(this, "Rename Error", "Could not rename file. Ensure it is not open or restricted.");
            }
        });

        QAction* duplicateAction = menu.addAction("Duplicate");
        connect(duplicateAction, &QAction::triggered, [this, path, info]() {
            QString newPath = info.absolutePath() + "/Copy_of_" + info.fileName();
            bool success = false;
            if (info.isDir()) {
                // Simplistic dir copy (Qt doesn't have a direct recursive copy in QFile)
                // For now just handle files to be safe, or use a helper
                QMessageBox::information(this, "Duplicate", "Duplicating folders is not yet supported.");
                return;
            } else {
                success = QFile::copy(path, newPath);
            }
            if (success) refreshProjectTree();
        });

        menu.addSeparator();
    }

    QAction* showInExplorer = menu.addAction("Show in File Manager");
    connect(showInExplorer, &QAction::triggered, [=]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
    });

    if (!m_workspaceFolders.contains(path)) {
        menu.addSeparator();
        QAction* deleteAction = menu.addAction("Delete");
        deleteAction->setIcon(QIcon(":/icons/tool_delete.svg"));
        connect(deleteAction, &QAction::triggered, [=]() {
             if (QMessageBox::question(this, "Delete", "Are you sure you want to delete " + info.fileName() + "?\nThis cannot be undone.") == QMessageBox::Yes) {
                 if (info.isDir()) QDir(path).removeRecursively();
                 else QFile::remove(path);
                 refreshProjectTree();
             }
        });
    }

    menu.exec(m_projectTree->mapToGlobal(pos));
}

void ProjectManager::applyKiCadStyle() {
    updateThemeStyle();
}

void ProjectManager::updateThemeStyle() {
    auto* theme = ThemeManager::theme();
    if (!theme) return;

    QString windowBg = theme->windowBackground().name();
    QString panelBg = theme->panelBackground().name();
    QString textColor = theme->textColor().name();
    QString textSecondary = theme->textSecondary().name();
    QString border = theme->panelBorder().name();
    QString accent = theme->accentColor().name();
    QString accentHover = theme->accentHover().name();

    bool isLight = theme->type() == PCBTheme::Light;
    QString toolbarBg = isLight ? "#f8fafc" : "#0a0a0c";
    QString headerBg = isLight ? "#f1f5f9" : "#121214";
    QString inputBg = isLight ? "#ffffff" : "#121214";
    QString treeHover = isLight ? "#f1f5f9" : "#1f1f23";

    QString pmStyle = QString(
        "QMainWindow { background-color: %1; color: %3; font-family: 'Inter', 'Segoe UI', sans-serif; }"
        "QMenuBar { background-color: %9; color: %4; border-bottom: 1px solid %5; padding: 4px; }"
        "QMenuBar::item:selected { background-color: %6; color: white; border-radius: 4px; }"
        
        "QToolBar#VerticalToolbar {"
        "   background-color: %8;"
        "   border-right: 1px solid %5;"
        "   padding: 15px 8px;"
        "   spacing: 12px;"
        "}"
        "QToolBar#VerticalToolbar QToolButton {"
        "   background: transparent;"
        "   border: none;"
        "   padding: 10px;"
        "   color: %4;"
        "   transition: color 0.2s;"
        "}"
        "QToolBar#VerticalToolbar QToolButton:hover { color: %6; }"
        "QToolBar#VerticalToolbar QToolButton:checked { color: %7; }"

        "QWidget#ProjectFilesPanel { background-color: %2; border-right: 1px solid %5; }"
        "QLabel#PanelHeader {"
        "   color: %6; font-size: 10px; font-weight: 800; padding: 6px 12px; "
        "   letter-spacing: 2px; text-transform: uppercase; background: %9;"
        "}"

        "QLineEdit#ProjectSearch {"
        "   background-color: %10; color: %3; border: none; border-bottom: 1px solid %5;"
        "   padding: 6px 12px; margin: 0; font-size: 12px;"
        "}"
        "QLineEdit#ProjectSearch:focus { border-bottom: 2px solid %6; background: %10; }"

        "QTreeWidget#ProjectTree { background-color: transparent; border: none; outline: 0; color: %4; }"
        "QTreeWidget#ProjectTree::item { padding: 6px 10px; border-radius: 4px; margin: 1px 5px; }"
        "QTreeWidget#ProjectTree::item:hover { background-color: %11; color: %3; }"
        "QTreeWidget#ProjectTree::item:selected { background-color: %6; color: white; }"

        "QWidget#LauncherArea { background-color: %1; }"
        "QLabel#WelcomeTitle { color: %3; font-size: 36px; font-weight: 900; letter-spacing: -1px; }"
        "QLabel#WelcomeSubtitle { color: %4; font-size: 16px; font-weight: 400; margin-top: -5px; }"

        "LauncherTile {"
        "   background: %10;"
        "   border: 1px solid %5;"
        "   border-radius: 14px;"
        "   min-width: 280px;"
        "   min-height: 100px;"
        "   margin: 4px;"
        "}"
        "LauncherTile:hover {"
        "   border: 1px solid %6;"
        "}"
        "QLabel#TileTitle { color: %3; font-size: 16px; font-weight: 700; }"
        "QLabel#TileDesc { color: %4; font-size: 13px; line-height: 1.5; }"
        
        "QStatusBar { background-color: %9; color: %4; border-top: 1px solid %5; padding: 5px; }"
        "QSplitter::handle { background-color: %5; width: 1px; }"
    )
    .arg(windowBg)       // 1
    .arg(panelBg)        // 2
    .arg(textColor)      // 3
    .arg(textSecondary)  // 4
    .arg(border)         // 5
    .arg(accent)         // 6
    .arg(accentHover)    // 7
    .arg(toolbarBg)      // 8
    .arg(headerBg)       // 9
    .arg(inputBg)        // 10
    .arg(treeHover);     // 11

    setStyleSheet(pmStyle);
    
    // Also update tiles manually since they use some hardcoded styles in their constructor
    for (auto* tile : m_launcherTiles) {
        tile->setStyleSheet(QString(
            "LauncherTile { background: %1; border: 1px solid %2; border-radius: 14px; }"
            "LauncherTile:hover { border: 1px solid %3; }"
            "QLabel#TileTitle { color: %4; }"
            "QLabel#TileDesc { color: %5; }"
        ).arg(inputBg, border, accent, textColor, textSecondary));
    }
}


void ProjectManager::createMenuBar() {
    QMenu* fileMenu = menuBar()->addMenu("&File");
    m_newProjectAction = fileMenu->addAction("&New Project...", this, &ProjectManager::createNewProject);
    m_newProjectAction->setShortcut(QKeySequence::New);
    m_openProjectAction = fileMenu->addAction("&Open...", this, &ProjectManager::openExistingProject);
    m_openProjectAction->setShortcut(QKeySequence::Open);
    
    QAction* openWorkspaceAction = fileMenu->addAction("&Open Workspace...", this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, "Open Workspace",
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            "viospice Workspace (*.viospice-workspace)");
        if (!path.isEmpty()) {
            loadWorkspace(path);
        }
    });

    fileMenu->addSeparator();
    m_addFolderAction = fileMenu->addAction("Add Folder to &Workspace...", this, &ProjectManager::addFolderToWorkspace);
    m_addFolderAction->setShortcut(QKeySequence("Ctrl+Shift+O"));
    
    QAction* saveWorkspaceAction = fileMenu->addAction("&Save Workspace", this, &ProjectManager::saveWorkspace);
    saveWorkspaceAction->setShortcut(QKeySequence("Ctrl+Shift+S"));

    m_recentProjectsMenu = fileMenu->addMenu("Open &Recent");
    updateRecentProjectsMenu();
    
    fileMenu->addSeparator();
    m_exitAction = fileMenu->addAction("E&xit", qApp, &QApplication::quit);
    m_exitAction->setShortcut(QKeySequence::Quit);

    QMenu* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction("Refresh", this, &ProjectManager::refreshProjectTree);

    QMenu* toolsMenu = menuBar()->addMenu("&Tools");
    toolsMenu->addAction("Schematic Editor", this, &ProjectManager::openSchematicEditor);
    toolsMenu->addAction("Symbol Editor", this, &ProjectManager::openSymbolEditor);
    toolsMenu->addSeparator();
    QMenu* importersMenu = toolsMenu->addMenu("Importers");
    importersMenu->addAction("LTspice Batch Symbols...", this, &ProjectManager::importLtspiceBatch);
    importersMenu->addAction("Diode/JFET Models...", this, &ProjectManager::importLtspiceDiodeModels);
    importersMenu->addAction("JFET Models...", this, &ProjectManager::importLtspiceJfetModels);
    importersMenu->addAction("BJT Models...", this, &ProjectManager::importLtspiceBjtModels);
    importersMenu->addAction("MOS Models...", this, &ProjectManager::importLtspiceMosModels);
    importersMenu->addAction("Resistor Models (standard.res)", this, &ProjectManager::importLtspiceResistorModels);
    importersMenu->addAction("Capacitor Models (standard.cap)", this, &ProjectManager::importLtspiceCapacitorModels);
    importersMenu->addAction("Inductor Models (standard.ind)", this, &ProjectManager::importLtspiceInductorModels);
    importersMenu->addAction("Import Standard Passive Models", this, &ProjectManager::importLtspiceStandardPassiveModels);

    QMenu* prefsMenu = menuBar()->addMenu("&Preferences");
    prefsMenu->addAction("Settings", this, &ProjectManager::onSettings);

    QMenu* helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("&Help & Guides", this, &ProjectManager::showHelp, QKeySequence::HelpContents);
    helpMenu->addAction("&Developer Documentation", this, &ProjectManager::showDeveloperHelp, QKeySequence("Ctrl+Shift+F1"));
    helpMenu->addAction("Project &Health Audit...", this, &ProjectManager::onProjectAudit);
    m_aboutAction = helpMenu->addAction("&About viospice", this, &ProjectManager::showAbout);
}

void ProjectManager::createNewProject() {
    QString projectName = QInputDialog::getText(this, "New Project", "Project Name:");
    if (projectName.isEmpty()) return;

    QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QDir().mkpath(defaultPath);
    QString projectPath = QFileDialog::getExistingDirectory(this, "Location", defaultPath);
    if (projectPath.isEmpty()) return;

    QString fullProjectPath = projectPath + "/" + projectName;
    Project* project = new Project(projectName, fullProjectPath);
    if (project->createNew()) {
        RecentProjects::instance().addProject(project->projectFilePath());
        updateRecentProjectsMenu();
        openProject(project->projectFilePath());
    } else {
        QMessageBox::critical(this, "Error", "Failed to create project");
    }
    delete project;
}

void ProjectManager::openExistingProject() {
    QString path = QFileDialog::getOpenFileName(this, "Open Folder or Workspace",
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
        "viospice Workspace (*.viospice-workspace);;All Files (*)");
    
    if (!path.isEmpty()) {
        if (path.endsWith(".viospice-workspace", Qt::CaseInsensitive)) {
            loadWorkspace(path);
        } else {
            RecentProjects::instance().addProject(path);
            updateRecentProjectsMenu();
            openProject(path);
        }
    }
}

void ProjectManager::openProject(const QString& path) {
    QFileInfo info(path);
    QString dirPath;

    if (info.isFile()) {
        dirPath = info.absolutePath();
    } else {
        dirPath = path;
    }
    
    // Normalize path
    dirPath = QDir(dirPath).absolutePath();
    
    // Only show this folder in explorer (replace workspace)
    m_workspaceFolders.clear();
    m_workspaceFolders.append(dirPath);
    
    refreshProjectTree();
}

void ProjectManager::updateRecentProjectsMenu() {
    if (!m_recentProjectsMenu) return;

    m_recentProjectsMenu->clear();
    
    // Add Recent Workspaces section
    const QStringList workspaces = RecentWorkspaces::instance().workspaces();
    bool hasWorkspaces = false;
    for (const QString& path : workspaces) {
        if (!QFile::exists(path)) continue;
        if (!hasWorkspaces) {
            m_recentProjectsMenu->addSection("Workspaces");
            hasWorkspaces = true;
        }
        QAction* action = m_recentProjectsMenu->addAction(QFileInfo(path).fileName());
        action->setData(path);
        connect(action, &QAction::triggered, [this, path]() {
            loadWorkspace(path);
        });
    }
    
    // Add Recent Projects section
    const QStringList projects = RecentProjects::instance().projects();
    bool hasProjects = false;
    for (const QString& path : projects) {
        if (!QFile::exists(path) && !QDir(path).exists()) continue;
        if (!hasProjects) {
            m_recentProjectsMenu->addSection("Folders");
            hasProjects = true;
        }
        QAction* action = m_recentProjectsMenu->addAction(QFileInfo(path).fileName());
        action->setData(path);
        connect(action, &QAction::triggered, [this, path]() {
            openProject(path); 
        });
    }
    
    if (!hasWorkspaces && !hasProjects) {
        m_recentProjectsMenu->addAction("(No recent items)")->setEnabled(false);
    } else {
        m_recentProjectsMenu->addSeparator();
        QAction* clear = m_recentProjectsMenu->addAction("Clear Recent Items");
        connect(clear, &QAction::triggered, [this]() {
            RecentProjects::instance().clear();
            RecentWorkspaces::instance().clear();
            updateRecentProjectsMenu();
        });
    }
}

void ProjectManager::openSchematicEditor() { 
    if (m_workspaceFolders.isEmpty()) {
        int ret = QMessageBox::question(this, "No Folders in Workspace", 
            "No folders are currently in the workspace. Do you want to add a folder?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (ret == QMessageBox::Yes) {
             addFolderToWorkspace();
        }
        if (m_workspaceFolders.isEmpty()) return;
    }
    
    launchSchematicEditor();
}

void ProjectManager::openSymbolEditor() {
    SymbolEditor* editor = new SymbolEditor(nullptr);
    editor->setAttribute(Qt::WA_DeleteOnClose);
    editor->show();
}

void ProjectManager::openCalculatorTools() {
    CalculatorDialog* dlg = new CalculatorDialog(this);
    dlg->show();
}

void ProjectManager::showAbout() {
    QMessageBox::about(this, "About viospice",
        "viospice v0.1.0\n\n"
        "Professional Electronic Design Automation\n\n"
        "Open-source PCB design software");
}

void ProjectManager::showHelp() {
    HelpWindow* help = new HelpWindow();
    help->setAttribute(Qt::WA_DeleteOnClose);
    help->show();
}

void ProjectManager::showDeveloperHelp() {
    DeveloperHelpWindow* devHelp = new DeveloperHelpWindow();
    devHelp->setAttribute(Qt::WA_DeleteOnClose);
    devHelp->show();
}

void ProjectManager::onProjectAudit() {
    ProjectAuditDialog dlg(this);
    dlg.exec();
}

void ProjectManager::openPluginsManager() {
    PluginManagerDialog dlg(this);
    dlg.exec();
}

void ProjectManager::openSpiceModelManager() {
    SpiceModelManagerDialog dlg(this);
    dlg.exec();
}

void ProjectManager::importLtspiceBatch() {
    const QString defaultSrc = QDir::homePath() + "/Documents/ltspice/sym";
    QString srcDir = QFileDialog::getExistingDirectory(this, "Select LTspice Symbol Folder", defaultSrc);
    if (srcDir.isEmpty()) return;

    const QString defaultDst = QDir::homePath() + "/ViospiceLib/sym/LTspice";
    QString dstDir = QFileDialog::getExistingDirectory(this, "Select Destination Viospice Symbol Folder", defaultDst);
    if (dstDir.isEmpty()) return;

    QDir().mkpath(dstDir);

    QList<QString> failures;
    int total = 0;
    int imported = 0;

    QProgressDialog progress("Importing LTspice symbols...", "Cancel", 0, 0, this);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(300);
    progress.show();

    QDirIterator it(srcDir, QStringList() << "*.asy", QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        if (progress.wasCanceled()) break;
        const QString path = it.next();
        total++;

        const auto result = LtspiceSymbolImporter::importSymbolDetailed(path);
        if (!result.success || !result.symbol.isValid()) {
            QString err = result.errorMessage.trimmed();
            if (err.isEmpty()) err = "import failed";
            failures.append(QFileInfo(path).fileName() + ": " + err);
            continue;
        }

        SymbolDefinition def = result.symbol;
        const QString rel = QDir(srcDir).relativeFilePath(path);
        const QString relDir = QFileInfo(rel).path();
        QString outDir = dstDir;
        if (!relDir.isEmpty() && relDir != ".") {
            outDir = QDir(dstDir).filePath(relDir);
            QDir().mkpath(outDir);
            def.setCategory(QFileInfo(relDir).fileName());
        }

        QString name = def.name().trimmed();
        if (name.isEmpty()) name = QFileInfo(path).baseName();
        def.setName(name);

        const QString outPath = QDir(outDir).filePath(name + ".viosym");
        QFile file(outPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            failures.append(QFileInfo(path).fileName() + ": write failed");
            continue;
        }
        QJsonDocument doc(def.toJson());
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();

        // Sub-model import logic
        const QString modelName = def.modelName();
        const QString modelPath = def.modelPath();
        if (!modelName.isEmpty()) {
            const bool found = (ModelLibraryManager::instance().findModel(modelName) != nullptr ||
                                ModelLibraryManager::instance().findSubcircuit(modelName) != nullptr);
            
            if (!found && !modelPath.isEmpty()) {
                // Not in our library yet, try to find the source file.
                QFileInfo asyInfo(path);
                QString sourcePath = asyInfo.dir().filePath(modelPath);
                
                if (!QFileInfo::exists(sourcePath)) {
                    // Try to find the LTspice 'lib' or root directory by walking up from srcDir
                    QDir checkDir(asyInfo.dir());
                    bool foundLib = false;
                    for (int i = 0; i < 5; ++i) {
                        if (checkDir.exists("sub") || checkDir.exists("lib/sub") || 
                            checkDir.exists("cmp") || checkDir.exists("lib/cmp")) {
                            foundLib = true;
                            break;
                        }
                        if (!checkDir.cdUp()) break;
                    }
                    
                    if (foundLib) {
                        QString base = checkDir.absolutePath();
                        QString trySub = QDir(base).filePath("sub/" + modelPath);
                        if (!QFileInfo::exists(trySub)) trySub = QDir(base).filePath("lib/sub/" + modelPath);
                        
                        if (QFileInfo::exists(trySub)) {
                            sourcePath = trySub;
                        } else {
                            QString tryCmp = QDir(base).filePath("cmp/" + modelPath);
                            if (!QFileInfo::exists(tryCmp)) tryCmp = QDir(base).filePath("lib/cmp/" + modelPath);
                            if (QFileInfo::exists(tryCmp)) sourcePath = tryCmp;
                        }
                    }
                }
                
                if (QFileInfo::exists(sourcePath)) {
                    const QString targetSubDir = QDir::homePath() + "/ViospiceLib/sub";
                    const QString targetPath = QDir(targetSubDir).filePath(modelPath);
                    if (!QFileInfo::exists(targetPath)) {
                        QDir().mkpath(QFileInfo(targetPath).path());
                        QFile::copy(sourcePath, targetPath);
                    }
                }
            }
        }

        imported++;
    }

    SymbolLibraryManager::instance().reloadUserLibraries();
    ModelLibraryManager::instance().reload();

    QString msg = QString("Imported %1 / %2 symbols into:\n%3")
                      .arg(imported)
                      .arg(total)
                      .arg(dstDir);
    if (!failures.isEmpty()) {
        const int shown = qMin(10, failures.size());
        msg += QString("\n\nFailures (%1):\n").arg(failures.size());
        for (int i = 0; i < shown; ++i) msg += " - " + failures[i] + "\n";
        if (failures.size() > shown) msg += " - ...\n";
    }

    QMessageBox::information(this, "LTspice Import", msg);
}

void ProjectManager::importLtspiceDiodeModels() {
    const QString defaultSrc = QDir::homePath() + "/Documents/ltspice/cmp";
    QString srcFile = QFileDialog::getOpenFileName(this, "Select LTspice Diode/JFET Model File", defaultSrc, "LTspice Model Files (*.dio *.jft *.lib);;All Files (*)");
    if (srcFile.isEmpty()) return;

    const QString defaultDst = QDir::homePath() + "/ViospiceLib/lib";
    QDir().mkpath(defaultDst);

    // Read source file
    QFile file(srcFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Import Error", "Cannot open file:\n" + srcFile);
        return;
    }
    const QString content = decodeSpiceText(file.readAll());
    file.close();

    // Build logical lines with SPICE continuation support ('+' at line start)
    QStringList logicalLines;
    for (const QString& raw : content.split('\n')) {
        const QString t = raw.trimmed();
        if (!logicalLines.isEmpty() && t.startsWith('+')) {
            logicalLines.last().append(' ' + t.mid(1).trimmed());
        } else {
            logicalLines.append(raw);
        }
    }

    // Parse models grouped by type
    QMap<QString, QStringList> typeToModels;
    QMap<QString, int> typeCounts;
    int total = 0;

    const QRegularExpression modelHeadRe(
        R"(^\s*[\x{FEFF}]*\.model\s+(\S+)\s+([^\s(]+))",
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression diodeTypeParamRe(R"(\btype\s*=\s*(\w+))", QRegularExpression::CaseInsensitiveOption);

    for (const QString& line : logicalLines) {
        QString trimmed = line.trimmed();

        QString type = "uncategorized";

        const QRegularExpressionMatch headMatch = modelHeadRe.match(trimmed);
        if (headMatch.hasMatch()) {
            QString modelTypeToken = headMatch.captured(2).trimmed().toLower();

            if (modelTypeToken == "njf" || modelTypeToken == "pjf") {
                type = modelTypeToken;
            } else if (modelTypeToken == "d") {
                const QRegularExpressionMatch m = diodeTypeParamRe.match(trimmed);
                if (m.hasMatch()) {
                    type = m.captured(1).toLower();
                } else {
                    type = "silicon";
                }
            }
        }

        // Normalize names
        if (type == "fastrecovery") type = "fast_recovery";
        else if (type == "switching") type = "switching";
        else if (type == "njf") type = "njf";
        else if (type == "pjf") type = "pjf";

        typeToModels[type].append(trimmed);
        typeCounts[type]++;
        total++;
    }

    if (total == 0) {
        QMessageBox::information(this, "Diode/JFET Model Import", "No .model lines found in file.");
        return;
    }

    // Write .lib files per type
    QStringList written;
    QProgressDialog progress("Importing diode/JFET models...", "Cancel", 0, typeToModels.size(), this);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(200);

    int step = 0;
    for (auto it = typeToModels.constBegin(); it != typeToModels.constEnd(); ++it) {
        if (progress.wasCanceled()) break;
        progress.setValue(step++);
        progress.setLabelText(QString("Writing %1 models (%2)...").arg(it.key()).arg(it.value().size()));

        const bool isJfetType = (it.key() == "njf" || it.key() == "pjf");
        const QString libName = isJfetType
            ? QString("jfets_%1.lib").arg(it.key())
            : QString("diodes_%1.lib").arg(it.key());
        const QString libPath = QDir(defaultDst).filePath(libName);

        QFile out(libPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            continue;
        }

        QTextStream ts(&out);
        ts << "* " << libName << " - Imported from " << QFileInfo(srcFile).fileName() << "\n";
        ts << "* Type: " << it.key() << "\n";
        ts << "* Count: " << it.value().size() << " models\n";
        ts << "*\n";
        for (const QString& model : it.value()) {
            ts << model << "\n";
        }
        out.close();
        written.append(QString("%1 (%2 models)").arg(libName).arg(it.value().size()));
    }
    progress.setValue(typeToModels.size());

    // Reload model library
    ModelLibraryManager::instance().reload();

    // Report
    QString msg = QString("Imported %1 models from:\n%2\n\nInto:\n%3\n\nFiles created:\n")
        .arg(total).arg(QFileInfo(srcFile).fileName()).arg(defaultDst);
    for (const QString& w : written) msg += "  " + w + "\n";

    QMessageBox::information(this, "Diode/JFET Model Import", msg);
}

void ProjectManager::importLtspiceJfetModels() {
    const QString defaultSrc = QDir::homePath() + "/Documents/ltspice/cmp";
    QString srcFile = QFileDialog::getOpenFileName(this, "Select LTspice JFET Model File", defaultSrc, "LTspice Model Files (*.jft *.lib);;All Files (*)");
    if (srcFile.isEmpty()) return;

    const QString defaultDst = QDir::homePath() + "/ViospiceLib/lib";
    QDir().mkpath(defaultDst);

    QFile file(srcFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Import Error", "Cannot open file:\n" + srcFile);
        return;
    }
    const QString content = decodeSpiceText(file.readAll());
    file.close();

    QStringList logicalLines;
    for (const QString& raw : content.split('\n')) {
        const QString t = raw.trimmed();
        if (!logicalLines.isEmpty() && t.startsWith('+')) {
            logicalLines.last().append(' ' + t.mid(1).trimmed());
        } else {
            logicalLines.append(raw);
        }
    }

    QMap<QString, QStringList> typeToModels;
    int total = 0;
    int modelLines = 0;
    const QRegularExpression modelHeadRe(
        R"(^\s*[\x{FEFF}]*\.model\s+(\S+)\s+([^\s(]+))",
        QRegularExpression::CaseInsensitiveOption);

    for (const QString& line : logicalLines) {
        const QString trimmed = line.trimmed();

        const QRegularExpressionMatch headMatch = modelHeadRe.match(trimmed);
        if (!headMatch.hasMatch()) continue;
        modelLines++;

        QString modelTypeToken = headMatch.captured(2).trimmed().toLower();

        if (modelTypeToken != "njf" && modelTypeToken != "pjf") continue;
        typeToModels[modelTypeToken].append(trimmed);
        total++;
    }

    if (total == 0) {
        QMessageBox::information(
            this,
            "JFET Model Import",
            QString("No NJF/PJF .model lines found in file.\n\nDetected .model lines: %1")
                .arg(modelLines));
        return;
    }

    QStringList written;
    QProgressDialog progress("Importing JFET models...", "Cancel", 0, typeToModels.size(), this);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(200);

    int step = 0;
    for (auto it = typeToModels.constBegin(); it != typeToModels.constEnd(); ++it) {
        if (progress.wasCanceled()) break;
        progress.setValue(step++);
        progress.setLabelText(QString("Writing %1 models (%2)...").arg(it.key().toUpper()).arg(it.value().size()));

        const QString libName = QString("jfets_%1.lib").arg(it.key());
        const QString libPath = QDir(defaultDst).filePath(libName);

        QFile out(libPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            continue;
        }

        QTextStream ts(&out);
        ts << "* " << libName << " - Imported from " << QFileInfo(srcFile).fileName() << "\n";
        ts << "* Type: " << it.key().toUpper() << "\n";
        ts << "* Count: " << it.value().size() << " models\n";
        ts << "*\n";
        for (const QString& model : it.value()) {
            ts << model << "\n";
        }
        out.close();
        written.append(QString("%1 (%2 models)").arg(libName).arg(it.value().size()));
    }
    progress.setValue(typeToModels.size());

    ModelLibraryManager::instance().reload();

    QString msg = QString("Imported %1 JFET models from:\n%2\n\nInto:\n%3\n\nFiles created:\n")
        .arg(total).arg(QFileInfo(srcFile).fileName()).arg(defaultDst);
    for (const QString& w : written) msg += "  " + w + "\n";

    QMessageBox::information(this, "JFET Model Import", msg);
}

void ProjectManager::importLtspiceBjtModels() {
    const QString defaultSrc = QDir::homePath() + "/Documents/ltspice/cmp";
    QString srcFile = QFileDialog::getOpenFileName(this, "Select LTspice BJT Model File", defaultSrc, "LTspice Model Files (*.bjt *.lib);;All Files (*)");
    if (srcFile.isEmpty()) return;

    const QString defaultDst = QDir::homePath() + "/ViospiceLib/lib";
    QDir().mkpath(defaultDst);

    QFile file(srcFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Import Error", "Cannot open file:\n" + srcFile);
        return;
    }
    const QString content = decodeSpiceText(file.readAll());
    file.close();

    QStringList logicalLines;
    for (const QString& raw : content.split('\n')) {
        const QString t = raw.trimmed();
        if (!logicalLines.isEmpty() && t.startsWith('+')) {
            logicalLines.last().append(' ' + t.mid(1).trimmed());
        } else {
            logicalLines.append(raw);
        }
    }

    QMap<QString, QStringList> typeToModels;
    int total = 0;
    int modelLines = 0;
    const QRegularExpression modelHeadRe(
        R"(^\s*[\x{FEFF}]*\.model\s+(\S+)\s+([^\s(]+))",
        QRegularExpression::CaseInsensitiveOption);

    for (const QString& line : logicalLines) {
        const QString trimmed = line.trimmed();
        const QRegularExpressionMatch headMatch = modelHeadRe.match(trimmed);
        if (!headMatch.hasMatch()) continue;
        modelLines++;

        const QString modelTypeToken = headMatch.captured(2).trimmed().toLower();
        if (modelTypeToken != "npn" && modelTypeToken != "pnp") continue;

        typeToModels[modelTypeToken].append(trimmed);
        total++;
    }

    if (total == 0) {
        QMessageBox::information(
            this,
            "BJT Model Import",
            QString("No NPN/PNP .model lines found in file.\n\nDetected .model lines: %1")
                .arg(modelLines));
        return;
    }

    QStringList written;
    QProgressDialog progress("Importing BJT models...", "Cancel", 0, typeToModels.size(), this);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(200);

    int step = 0;
    for (auto it = typeToModels.constBegin(); it != typeToModels.constEnd(); ++it) {
        if (progress.wasCanceled()) break;
        progress.setValue(step++);
        progress.setLabelText(QString("Writing %1 models (%2)...").arg(it.key().toUpper()).arg(it.value().size()));

        const QString libName = QString("bjts_%1.lib").arg(it.key());
        const QString libPath = QDir(defaultDst).filePath(libName);

        QFile out(libPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            continue;
        }

        QTextStream ts(&out);
        ts << "* " << libName << " - Imported from " << QFileInfo(srcFile).fileName() << "\n";
        ts << "* Type: " << it.key().toUpper() << "\n";
        ts << "* Count: " << it.value().size() << " models\n";
        ts << "*\n";
        for (const QString& model : it.value()) {
            ts << model << "\n";
        }
        out.close();
        written.append(QString("%1 (%2 models)").arg(libName).arg(it.value().size()));
    }
    progress.setValue(typeToModels.size());

    ModelLibraryManager::instance().reload();

    QString msg = QString("Imported %1 BJT models from:\n%2\n\nInto:\n%3\n\nFiles created:\n")
        .arg(total).arg(QFileInfo(srcFile).fileName()).arg(defaultDst);
    for (const QString& w : written) msg += "  " + w + "\n";

    QMessageBox::information(this, "BJT Model Import", msg);
}

void ProjectManager::importLtspiceMosModels() {
    const QString defaultSrc = QDir::homePath() + "/Documents/ltspice/cmp";
    QString srcFile = QFileDialog::getOpenFileName(this, "Select LTspice MOS Model File", defaultSrc, "LTspice Model Files (*.mos *.lib);;All Files (*)");
    if (srcFile.isEmpty()) return;

    const QString defaultDst = QDir::homePath() + "/ViospiceLib/lib";
    QDir().mkpath(defaultDst);

    QFile file(srcFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Import Error", "Cannot open file:\n" + srcFile);
        return;
    }
    const QString content = decodeSpiceText(file.readAll());
    file.close();

    QStringList logicalLines;
    for (const QString& raw : content.split('\n')) {
        const QString t = raw.trimmed();
        if (!logicalLines.isEmpty() && t.startsWith('+')) {
            logicalLines.last().append(' ' + t.mid(1).trimmed());
        } else {
            logicalLines.append(raw);
        }
    }

    QMap<QString, QStringList> typeToModels;
    int total = 0;
    int modelLines = 0;
    const QRegularExpression modelHeadRe(
        R"(^\s*[\x{FEFF}]*\.model\s+(\S+)\s+([^\s(]+))",
        QRegularExpression::CaseInsensitiveOption);

    for (const QString& line : logicalLines) {
        const QString trimmed = line.trimmed();
        const QRegularExpressionMatch headMatch = modelHeadRe.match(trimmed);
        if (!headMatch.hasMatch()) continue;
        modelLines++;

        const QString modelTypeToken = headMatch.captured(2).trimmed().toLower();
        QString bucket;
        if (modelTypeToken == "nmos") bucket = "nmos";
        else if (modelTypeToken == "pmos") bucket = "pmos";
        else if (modelTypeToken == "vdmos") {
            bucket = trimmed.contains(QRegularExpression(R"(\bpchan\b)", QRegularExpression::CaseInsensitiveOption)) ? "pmos" : "nmos";
        } else {
            continue;
        }

        typeToModels[bucket].append(trimmed);
        total++;
    }

    if (total == 0) {
        QMessageBox::information(
            this,
            "MOS Model Import",
            QString("No NMOS/PMOS/VDMOS .model lines found in file.\n\nDetected .model lines: %1")
                .arg(modelLines));
        return;
    }

    QStringList written;
    QProgressDialog progress("Importing MOS models...", "Cancel", 0, typeToModels.size(), this);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(200);

    int step = 0;
    for (auto it = typeToModels.constBegin(); it != typeToModels.constEnd(); ++it) {
        if (progress.wasCanceled()) break;
        progress.setValue(step++);
        progress.setLabelText(QString("Writing %1 models (%2)...").arg(it.key().toUpper()).arg(it.value().size()));

        const QString libName = QString("mosfets_%1.lib").arg(it.key());
        const QString libPath = QDir(defaultDst).filePath(libName);

        QFile out(libPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            continue;
        }

        QTextStream ts(&out);
        ts << "* " << libName << " - Imported from " << QFileInfo(srcFile).fileName() << "\n";
        ts << "* Type: " << it.key().toUpper() << "\n";
        ts << "* Count: " << it.value().size() << " models\n";
        ts << "*\n";
        for (const QString& model : it.value()) {
            ts << model << "\n";
        }
        out.close();
        written.append(QString("%1 (%2 models)").arg(libName).arg(it.value().size()));
    }
    progress.setValue(typeToModels.size());

    ModelLibraryManager::instance().reload();

    QString msg = QString("Imported %1 MOS models from:\n%2\n\nInto:\n%3\n\nFiles created:\n")
        .arg(total).arg(QFileInfo(srcFile).fileName()).arg(defaultDst);
    for (const QString& w : written) msg += "  " + w + "\n";

    QMessageBox::information(this, "MOS Model Import", msg);
}

void ProjectManager::importLtspiceResistorModels() {
    const QString defaultSrc = QDir::homePath() + "/Documents/ltspice/cmp";
    const QString srcFile = QFileDialog::getOpenFileName(
        this,
        "Select LTspice Resistor Model File",
        defaultSrc,
        "LTspice Resistor Models (*.res *.lib *.mod *.inc);;All Files (*)");
    if (srcFile.isEmpty()) return;

    importStandardPassiveModelFile(this,
                                   srcFile,
                                   "Resistor Model Import",
                                   "resistors_standard.lib",
                                   QSet<QString>{"r", "res", "resistor"},
                                   "Resistor");
}

void ProjectManager::importLtspiceCapacitorModels() {
    const QString defaultSrc = QDir::homePath() + "/Documents/ltspice/cmp";
    const QString srcFile = QFileDialog::getOpenFileName(
        this,
        "Select LTspice Capacitor Model File",
        defaultSrc,
        "LTspice Capacitor Models (*.cap *.lib *.mod *.inc);;All Files (*)");
    if (srcFile.isEmpty()) return;

    importStandardPassiveModelFile(this,
                                   srcFile,
                                   "Capacitor Model Import",
                                   "capacitors_standard.lib",
                                   QSet<QString>{"c", "cap", "capacitor"},
                                   "Capacitor");
}

void ProjectManager::importLtspiceInductorModels() {
    const QString defaultSrc = QDir::homePath() + "/Documents/ltspice/cmp";
    const QString srcFile = QFileDialog::getOpenFileName(
        this,
        "Select LTspice Inductor Model File",
        defaultSrc,
        "LTspice Inductor Models (*.ind *.lib *.mod *.inc);;All Files (*)");
    if (srcFile.isEmpty()) return;

    importStandardPassiveModelFile(this,
                                   srcFile,
                                   "Inductor Model Import",
                                   "inductors_standard.lib",
                                   QSet<QString>{"l", "ind", "inductor"},
                                   "Inductor");
}

void ProjectManager::importLtspiceStandardPassiveModels() {
    importLtspiceResistorModels();
    importLtspiceCapacitorModels();
    importLtspiceInductorModels();
}

void ProjectManager::onSettings() {
    SettingsDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        // Apply global changes if needed, though most are persisted in ConfigManager
        statusBar()->showMessage("Global settings updated.", 3000);
    }
}

void ProjectManager::launchSchematicEditor(const QString& projectPath) {
    qDebug() << "DBG: launchSchematicEditor start";
    SchematicEditor* editor = new SchematicEditor;
    editor->setAttribute(Qt::WA_DeleteOnClose);
    
    QString pFile = projectPath;
    if (projectPath.isEmpty() || QFileInfo(projectPath).isDir()) {
        pFile = resolveProjectPath(projectPath, "flxsch"); // Modern default for dirs
    }
    
    QString pDir, pName;
    if (!pFile.isEmpty()) {
        QFileInfo fi(pFile);
        pDir = fi.absolutePath();
        pName = fi.completeBaseName();
    } else if (!m_workspaceFolders.isEmpty()) {
        // Use first workspace folder as project directory
        pDir = m_workspaceFolders.first();
        QDir dir(pDir);
        pName = dir.dirName();
    }
    
    qDebug() << "DBG: before setProjectContext";
    editor->setProjectContext(pName, pDir, m_workspaceFolders);
    qDebug() << "DBG: after setProjectContext";

    if (QFile::exists(pFile)) {
        qDebug() << "DBG: before openFile";
        editor->openFile(pFile);
        qDebug() << "DBG: after openFile";
    }
    qDebug() << "DBG: before show";
    editor->show();
    qDebug() << "DBG: after show";
}

QString ProjectManager::resolveProjectPath(const QString& inputPath, const QString& extension) {
    if (inputPath.isEmpty()) return QString();

    QFileInfo info(inputPath);
    QString pDir, pName, pFile;

    if (info.isDir()) {
         pDir = info.absoluteFilePath();
         pName = info.fileName();
         
         QDir d(pDir);
         QStringList flux = d.entryList(QStringList() << "*.flux", QDir::Files);
         if (!flux.isEmpty()) pName = QFileInfo(flux.first()).completeBaseName();

         QStringList files = d.entryList(QStringList() << ("*." + extension), QDir::Files);
         if (!files.isEmpty()) pFile = d.absoluteFilePath(files.first());
         else pFile = pDir + "/" + pName + "." + extension;
         
    } else {
         pDir = info.absolutePath();
         pName = info.completeBaseName();
         
         if (inputPath.endsWith("." + extension, Qt::CaseInsensitive)) pFile = inputPath;
         else pFile = pDir + "/" + pName + "." + extension;
    }
    return pFile;
}

// ============ LauncherTile Implementation ============

LauncherTile::LauncherTile(const QString& iconPath, const QString& title, 
                           const QString& description, QWidget* parent)
    : QFrame(parent) {
    setObjectName("LauncherTile");
    setCursor(Qt::PointingHandCursor);

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(16);

    // Icon with subtle round background
    m_iconLabel = new QLabel;
    m_iconLabel->setObjectName("TileIcon");
    m_iconLabel->setFixedSize(56, 56);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setStyleSheet(
        "background-color: #1a1c22;"
        "border: 1px solid #2d2d30;"
        "border-radius: 16px;"
        "padding: 8px;"
    );
    layout->addWidget(m_iconLabel);

    // Text container
    QVBoxLayout* textLayout = new QVBoxLayout;
    textLayout->setSpacing(4);
    textLayout->setContentsMargins(0, 4, 0, 4);

    m_titleLabel = new QLabel(title);
    m_titleLabel->setObjectName("TileTitle");
    textLayout->addWidget(m_titleLabel);

    m_descLabel = new QLabel(description);
    m_descLabel->setObjectName("TileDesc");
    m_descLabel->setWordWrap(true);
    textLayout->addWidget(m_descLabel);

    textLayout->addStretch();
    layout->addLayout(textLayout, 1);

    // Small arrow indicator on the right
    QLabel* arrowLabel = new QLabel("›");
    arrowLabel->setStyleSheet("color: #3c3c3c; font-size: 22px; font-weight: bold; background: transparent;");
    arrowLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(arrowLabel);

    Q_UNUSED(iconPath)
}

void LauncherTile::setIcon(const QIcon& icon) {
    m_iconLabel->setPixmap(icon.pixmap(40, 40));
}

void LauncherTile::setIconFromChar(const QString& character, const QColor& bgColor) {
    m_iconLabel->setText(character);
    m_iconLabel->setStyleSheet(QString(
        "background-color: %1; "
        "border-radius: 12px; "
        "font-size: 22px; "
        "color: white;"
    ).arg(bgColor.name()));
}

void LauncherTile::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && isEnabled()) {
        emit clicked();
    }
}

void LauncherTile::enterEvent(QEnterEvent*) {
    // Hover effect handled by CSS
}

void LauncherTile::leaveEvent(QEvent*) {
    // Leave effect handled by CSS
}
