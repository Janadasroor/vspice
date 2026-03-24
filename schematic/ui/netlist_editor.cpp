#include "netlist_editor.h"
#include "../../core/simulation_manager.h"
#include "../../core/theme_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSplitter>
#include <QPainter>
#include <QFileDialog>
#include <QTemporaryFile>
#include <QFile>
#include <QTextStream>
#include <QAction>
#include <QLabel>
#include <QScrollBar>
#include <QDir>

NetlistEditor::NetlistEditor(QWidget* parent)
    : QWidget(parent), m_activeTempFile(nullptr) {
    setupUI();
    
    m_highlighter = new SpiceHighlighter(m_editor->document());
    m_logHighlighter = new SpiceHighlighter(m_logArea->document());
    
    connect(&SimulationManager::instance(), &SimulationManager::outputReceived, this, &NetlistEditor::onOutputReceived);
    connect(&SimulationManager::instance(), qOverload<>(&SimulationManager::simulationFinished), this, &NetlistEditor::onSimulationFinished);

    applyTheme();
}

NetlistEditor::~NetlistEditor() {
    if (m_activeTempFile) {
        m_activeTempFile->remove();
        delete m_activeTempFile;
    }
}

void NetlistEditor::setupUI() {
    setWindowTitle("Netlist Editor");
    resize(900, 700);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // --- Toolbar ---
    m_toolbar = new QToolBar(this);
    m_toolbar->setToolButtonStyle(Qt::ToolButtonStyle::ToolButtonTextBesideIcon);
    
    auto getIcon = [&](const QString& path) {
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
    };

    QAction* runAct = m_toolbar->addAction(getIcon(":/icons/tool_run.svg"), "Run Simulation");
    connect(runAct, &QAction::triggered, this, &NetlistEditor::onRun);

    m_toolbar->addSeparator();

    QAction* saveAct = m_toolbar->addAction(getIcon(":/icons/check.svg"), "Save As...");
    connect(saveAct, &QAction::triggered, this, &NetlistEditor::onSaveAs);

    m_toolbar->addSeparator();

    QAction* clearAct = m_toolbar->addAction(getIcon(":/icons/toolbar_erase.png"), "Clear Log");
    connect(clearAct, &QAction::triggered, this, &NetlistEditor::onClearLog);

    mainLayout->addWidget(m_toolbar);

    // --- Splitter ---
    auto* splitter = new QSplitter(Qt::Vertical, this);
    splitter->setHandleWidth(1);

    // Editor Area
    m_editor = new QPlainTextEdit(this);
    m_editor->setPlaceholderText("* SPICE Netlist\nV1 1 0 5\nR1 1 0 1k\n.op\n.end");
    
    // Log Area
    m_logArea = new QPlainTextEdit(this);
    m_logArea->setReadOnly(true);
    m_logArea->hide();

    splitter->addWidget(m_editor);
    splitter->addWidget(m_logArea);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);

    mainLayout->addWidget(splitter);
}

void NetlistEditor::applyTheme() {
    PCBTheme* theme = ThemeManager::theme();
    if (!theme) return;

    QString bg = theme->windowBackground().name();
    QString panelBg = theme->panelBackground().name();
    QString fg = theme->textColor().name();
    QString border = theme->panelBorder().name();
    QString accent = theme->accentColor().name();
    QString inputBg = (theme->type() == PCBTheme::Light) ? "#f8fafc" : "#1e1e1e";
    QString logBg = (theme->type() == PCBTheme::Light) ? "#f1f5f9" : "#0c0c0c";

    m_toolbar->setStyleSheet(theme->toolbarStylesheet());

    m_editor->setStyleSheet(QString(
        "QPlainTextEdit { "
        "   background-color: %1; color: %2; "
        "   font-family: 'JetBrains Mono', 'Fira Code', monospace; font-size: 14px; "
        "   border: none; padding: 15px; "
        "   selection-background-color: %3; "
        "}"
    ).arg(inputBg, fg, accent));

    m_logArea->setStyleSheet(QString(
        "QPlainTextEdit { "
        "   background-color: %1; color: %2; "
        "   font-family: 'JetBrains Mono', monospace; font-size: 12px; "
        "   border-top: 1px solid %3; padding: 10px; "
        "}"
    ).arg(logBg, theme->textSecondary().name(), border));

    if (m_highlighter) m_highlighter->updateColors();
    if (m_logHighlighter) m_logHighlighter->updateColors();
}

void NetlistEditor::setNetlist(const QString& netlist) {
    m_editor->setPlainText(netlist);
}

void NetlistEditor::loadFile(const QString& path) {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_editor->setPlainText(file.readAll());
        m_currentFilePath = path;
        setWindowTitle("Netlist: " + QFileInfo(path).fileName());
    }
}

QString NetlistEditor::netlist() const {
    return m_editor->toPlainText();
}

void NetlistEditor::onRun() {
    QString content = m_editor->toPlainText();
    if (content.trimmed().isEmpty()) return;

    onClearLog();
    m_logArea->show();
    m_logArea->appendPlainText("Starting simulation...");

    // Clean up previous temp file if any
    if (m_activeTempFile) {
        m_activeTempFile->remove();
        delete m_activeTempFile;
        m_activeTempFile = nullptr;
    }

    m_activeTempFile = new QTemporaryFile(this);
    m_activeTempFile->setFileTemplate(QDir::tempPath() + "/viospice_XXXXXX.cir");
    if (m_activeTempFile->open()) {
        QTextStream out(m_activeTempFile);
        out << content;
        m_activeTempFile->flush();
        
        // Pass the absolute path to SimulationManager
        SimulationManager::instance().runSimulation(m_activeTempFile->fileName());
    }
}

void NetlistEditor::onClearLog() {
    m_logArea->clear();
}

void NetlistEditor::onSaveAs() {
    QString path = QFileDialog::getSaveFileName(this, "Save Netlist", "", "SPICE Netlist (*.cir *.sp *.txt)");
    if (!path.isEmpty()) {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << m_editor->toPlainText();
            file.close();
            m_currentFilePath = path;
        }
    }
}

void NetlistEditor::onOutputReceived(const QString& msg) {
    m_logArea->appendPlainText(msg);
    // Auto-scroll to bottom
    m_logArea->verticalScrollBar()->setValue(m_logArea->verticalScrollBar()->maximum());
}

void NetlistEditor::onSimulationFinished() {
    m_logArea->appendPlainText("\n--- Simulation Finished ---");
}
