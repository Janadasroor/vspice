// schematic_editor_file.cpp
// File operations (new, open, save, export) for SchematicEditor

#include "schematic_editor.h"
#include "schematic_api.h"
#include "schematic_file_io.h"
#include "schematic_connectivity.h"
#include "schematic_page_item.h"
#include "../items/schematic_sheet_item.h"
#include "schematic_erc.h"

#include "netlist_generator.h"
#include "../ui/netlist_editor.h"
#include "../ui/bom_dialog.h"
#include "sync_manager.h"
#include "gemini_dialog.h"
#include "../../python/gemini_panel.h"
#include "../dialogs/bus_aliases_dialog.h"
#include "../../symbols/symbol_editor.h"
#include "../items/net_label_item.h"
#include "../../core/project.h"
#include "../../core/recent_projects.h"
#include "../analysis/spice_netlist_generator.h"
#include "../ui/simulation_panel.h"
#include "../../ui/source_control_manager.h"
#include "../../core/config_manager.h"
#include <QMessageBox>
#include <QRegularExpression>
#include "../../core/theme_manager.h"
#include <QInputDialog>
#include <QFileDialog>
#include <QStandardPaths>
#include <QFileInfo>
#include <QMessageBox>
#include <QFile>
#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>
#include <QPainter>
#include <QDir>
#include <QStatusBar>
#include <QApplication>
#include <QTimer>
#include <QJsonObject>
#include <QSet>
#include <QDate>
#include <QTextStream>
#include <cmath>
#include <algorithm>

namespace {
QString ercViolationKey(const ERCViolation& v) {
    const int px = int(std::round(v.position.x()));
    const int py = int(std::round(v.position.y()));
    return QString("%1|%2|%3|%4|%5")
        .arg(int(v.severity))
        .arg(int(v.category))
        .arg(v.netName.trimmed())
        .arg(px)
        .arg(py) + "|" + v.message.trimmed();
}

QStringList itemPinNames(const SchematicItem* item) {
    QStringList pins;
    if (!item) return pins;
    const QList<QPointF> points = item->connectionPoints();
    for (int i = 0; i < points.size(); ++i) {
        const QString pin = item->pinName(i).trimmed();
        pins.append(pin.isEmpty() ? QString::number(i + 1) : pin);
    }
    return pins;
}

bool isTextFile(const QString& path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    static const QSet<QString> exts = {
        "txt","log","md","json","json5","yaml","yml","xml",
        "html","htm","css","js","ts","jsx","tsx",
        "py","cpp","c","h","hpp","cmake","ini","cfg",
        "csv","tsv","net","cir","sp","sub","model"
    };
    return exts.contains(ext);
}

enum class HighlightMode {
    Plain,
    Json,
    Xml,
    Html,
    Yaml,
    Python,
    CLike,
    Script
};

class LineNumberTextEdit;

class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(QWidget* parent, LineNumberTextEdit* editor);
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    LineNumberTextEdit* m_editor;
};

class LineNumberTextEdit : public QPlainTextEdit {
public:
    explicit LineNumberTextEdit(QWidget* parent = nullptr) : QPlainTextEdit(parent) {
        m_lineNumberArea = new LineNumberArea(this, this);
        connect(this, &QPlainTextEdit::blockCountChanged, this, &LineNumberTextEdit::updateLineNumberAreaWidth);
        connect(this, &QPlainTextEdit::updateRequest, this, &LineNumberTextEdit::updateLineNumberArea);
        connect(this, &QPlainTextEdit::cursorPositionChanged, this, &LineNumberTextEdit::highlightCurrentLine);
        updateLineNumberAreaWidth(0);
        highlightCurrentLine();
    }

    int lineNumberAreaWidth() const {
        int digits = 1;
        int max = qMax(1, blockCount());
        while (max >= 10) {
            max /= 10;
            ++digits;
        }
        const int space = 6 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
        return space;
    }

    void lineNumberAreaPaintEvent(QPaintEvent* event) {
        QPainter painter(m_lineNumberArea);
        painter.fillRect(event->rect(), QColor(255, 255, 255));

        QTextBlock block = firstVisibleBlock();
        int blockNumber = block.blockNumber();
        int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        int bottom = top + qRound(blockBoundingRect(block).height());

        while (block.isValid() && top <= event->rect().bottom()) {
            if (block.isVisible() && bottom >= event->rect().top()) {
                const QString number = QString::number(blockNumber + 1);
                painter.setPen(QColor(60, 60, 60));
                painter.drawText(0, top, m_lineNumberArea->width() - 4, fontMetrics().height(),
                                 Qt::AlignRight, number);
            }

            block = block.next();
            top = bottom;
            bottom = top + qRound(blockBoundingRect(block).height());
            ++blockNumber;
        }
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QPlainTextEdit::resizeEvent(event);
        const QRect cr = contentsRect();
        m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
    }

private slots:
    void updateLineNumberAreaWidth(int) {
        setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
    }

    void updateLineNumberArea(const QRect& rect, int dy) {
        if (dy)
            m_lineNumberArea->scroll(0, dy);
        else
            m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());

        if (rect.contains(viewport()->rect()))
            updateLineNumberAreaWidth(0);
    }

    void highlightCurrentLine() {
        QList<QTextEdit::ExtraSelection> extraSelections;
        if (!isReadOnly()) {
            QTextEdit::ExtraSelection selection;
            selection.format.setBackground(QColor(232, 240, 254));
            selection.format.setProperty(QTextFormat::FullWidthSelection, true);
            selection.cursor = textCursor();
            selection.cursor.clearSelection();
            extraSelections.append(selection);
        }
        setExtraSelections(extraSelections);
    }

private:
    LineNumberArea* m_lineNumberArea = nullptr;
};

void LineNumberArea::paintEvent(QPaintEvent* event) {
    m_editor->lineNumberAreaPaintEvent(event);
}

LineNumberArea::LineNumberArea(QWidget* parent, LineNumberTextEdit* editor)
    : QWidget(parent), m_editor(editor) {
}

QSize LineNumberArea::sizeHint() const {
    return QSize(m_editor->lineNumberAreaWidth(), 0);
}

HighlightMode detectHighlightMode(const QString& path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "json" || ext == "json5") return HighlightMode::Json;
    if (ext == "xml") return HighlightMode::Xml;
    if (ext == "html" || ext == "htm") return HighlightMode::Html;
    if (ext == "yml" || ext == "yaml") return HighlightMode::Yaml;
    if (ext == "py") return HighlightMode::Python;
    if (ext == "c" || ext == "cpp" || ext == "h" || ext == "hpp") return HighlightMode::CLike;
    if (ext == "js" || ext == "ts" || ext == "jsx" || ext == "tsx") return HighlightMode::Script;
    if (ext == "css") return HighlightMode::Script;
    return HighlightMode::Plain;
}

class SimpleSyntaxHighlighter : public QSyntaxHighlighter {
public:
    SimpleSyntaxHighlighter(QTextDocument* doc, HighlightMode mode, bool lightTheme)
        : QSyntaxHighlighter(doc), m_mode(mode), m_lightTheme(lightTheme) {
        setupRules();
    }

protected:
    void highlightBlock(const QString& text) override {
        for (const auto& rule : m_rules) {
            QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
            while (it.hasNext()) {
                const auto match = it.next();
                setFormat(match.capturedStart(), match.capturedLength(), rule.format);
            }
        }

        if (m_mode == HighlightMode::CLike || m_mode == HighlightMode::Script) {
            setCurrentBlockState(0);
            int startIdx = 0;
            if (previousBlockState() != 1) {
                startIdx = text.indexOf("/*");
            }
            while (startIdx >= 0) {
                int endIdx = text.indexOf("*/", startIdx + 2);
                int length;
                if (endIdx == -1) {
                    setCurrentBlockState(1);
                    length = text.length() - startIdx;
                } else {
                    length = endIdx - startIdx + 2;
                }
                setFormat(startIdx, length, m_commentFormat);
                startIdx = text.indexOf("/*", startIdx + length);
            }
        } else if (m_mode == HighlightMode::Python) {
            setCurrentBlockState(0);
            if (previousBlockState() == 1) {
                int endIdx = text.indexOf("\"\"\"");
                if (endIdx == -1) {
                    setFormat(0, text.length(), m_stringFormat);
                    setCurrentBlockState(1);
                    return;
                }
                setFormat(0, endIdx + 3, m_stringFormat);
            }
            int startIdx = text.indexOf("\"\"\"");
            if (startIdx >= 0) {
                int endIdx = text.indexOf("\"\"\"", startIdx + 3);
                if (endIdx == -1) {
                    setFormat(startIdx, text.length() - startIdx, m_stringFormat);
                    setCurrentBlockState(1);
                } else {
                    setFormat(startIdx, endIdx - startIdx + 3, m_stringFormat);
                }
            }
        } else if (m_mode == HighlightMode::Xml || m_mode == HighlightMode::Html) {
            setCurrentBlockState(0);
            int startIdx = 0;
            if (previousBlockState() != 1) {
                startIdx = text.indexOf("<!--");
            }
            while (startIdx >= 0) {
                int endIdx = text.indexOf("-->", startIdx + 4);
                int length;
                if (endIdx == -1) {
                    setCurrentBlockState(1);
                    length = text.length() - startIdx;
                } else {
                    length = endIdx - startIdx + 3;
                }
                setFormat(startIdx, length, m_commentFormat);
                startIdx = text.indexOf("<!--", startIdx + length);
            }
        }
    }

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    void setupRules() {
        QTextCharFormat keywordFormat;
        keywordFormat.setForeground(m_lightTheme ? QColor("#1d4ed8") : QColor("#60a5fa"));
        keywordFormat.setFontWeight(QFont::DemiBold);

        QTextCharFormat typeFormat;
        typeFormat.setForeground(m_lightTheme ? QColor("#b45309") : QColor("#f59e0b"));

        QTextCharFormat numberFormat;
        numberFormat.setForeground(m_lightTheme ? QColor("#6d28d9") : QColor("#a78bfa"));

        m_stringFormat.setForeground(m_lightTheme ? QColor("#047857") : QColor("#34d399"));
        m_commentFormat.setForeground(m_lightTheme ? QColor("#6b7280") : QColor("#6b7280"));

        // Strings (single/double)
        m_rules.append({QRegularExpression(R"("([^"\\]|\\.)*")"), m_stringFormat});
        m_rules.append({QRegularExpression(R"('([^'\\]|\\.)*')"), m_stringFormat});

        // Numbers
        m_rules.append({QRegularExpression(R"(\b-?\d+(\.\d+)?([eE][+-]?\d+)?\b)"), numberFormat});

        if (m_mode == HighlightMode::Json) {
            QTextCharFormat keyFormat;
            keyFormat.setForeground(m_lightTheme ? QColor("#1d4ed8") : QColor("#93c5fd"));
            m_rules.append({QRegularExpression(R"("([^"\\]|\\.)*"(?=\s*:))"), keyFormat});
            QTextCharFormat literalFormat;
            literalFormat.setForeground(m_lightTheme ? QColor("#b91c1c") : QColor("#f87171"));
            m_rules.append({QRegularExpression(R"(\b(true|false|null)\b)"), literalFormat});
            return;
        }

        if (m_mode == HighlightMode::Yaml) {
            QTextCharFormat keyFormat;
            keyFormat.setForeground(m_lightTheme ? QColor("#1d4ed8") : QColor("#93c5fd"));
            m_rules.append({QRegularExpression(R"(^\s*[^:#\n]+(?=\s*:))"), keyFormat});
        }

        if (m_mode == HighlightMode::Xml || m_mode == HighlightMode::Html) {
            QTextCharFormat tagFormat;
            tagFormat.setForeground(m_lightTheme ? QColor("#1d4ed8") : QColor("#60a5fa"));
            m_rules.append({QRegularExpression(R"(<\/?[\w:-]+)"), tagFormat});
            QTextCharFormat attrFormat;
            attrFormat.setForeground(m_lightTheme ? QColor("#b45309") : QColor("#f59e0b"));
            m_rules.append({QRegularExpression(R"(\b[\w:-]+(?=\=))"), attrFormat});
        }

        if (m_mode == HighlightMode::Python) {
            m_rules.append({QRegularExpression(R"(\b(def|class|return|if|elif|else|for|while|try|except|finally|with|as|import|from|pass|break|continue|lambda|yield|True|False|None)\b)"), keywordFormat});
            m_rules.append({QRegularExpression(R"(#.*$)"), m_commentFormat});
            return;
        }

        if (m_mode == HighlightMode::CLike || m_mode == HighlightMode::Script) {
            m_rules.append({QRegularExpression(R"(\b(if|else|for|while|switch|case|break|continue|return|class|struct|public|private|protected|static|const|void|int|float|double|bool|char|new|delete|try|catch|throw|namespace|using|this|true|false|null)\b)"), keywordFormat});
            m_rules.append({QRegularExpression(R"(//.*$)"), m_commentFormat});
            return;
        }
    }

    HighlightMode m_mode;
    bool m_lightTheme = false;
    QVector<Rule> m_rules;
    QTextCharFormat m_stringFormat;
    QTextCharFormat m_commentFormat;
};

QPlainTextEdit* createTextEditor(QWidget* parent, const QString& content, const QString& filePath, bool readOnly) {
    auto* editor = new LineNumberTextEdit(parent);
    editor->setPlainText(content);
    editor->setReadOnly(readOnly);
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    bool light = ThemeManager::theme() && ThemeManager::theme()->type() == PCBTheme::Light;
    editor->setStyleSheet(QString(
        "QPlainTextEdit { background: %1; color: %2; border: none; font-family: monospace; font-size: 12px; selection-background-color: %3; selection-color: %2; }"
    ).arg(light ? "#f8fafc" : "#0f1115",
          light ? "#111827" : "#e5e7eb",
          light ? "#dbeafe" : "#1f2937"));
    new SimpleSyntaxHighlighter(editor->document(), detectHighlightMode(filePath), light);
    return editor;
}
}

void SchematicEditor::setProjectContext(const QString& projectName, const QString& projectDir, const QStringList& workspaceFolders) {
    m_projectName = projectName;
    m_projectDir = projectDir;

    if (m_projectExplorer && !projectDir.isEmpty()) {
        // Use the live workspace folders passed from ProjectManager (never stale)
        QStringList folders = workspaceFolders;
        
        // If no explicit folders provided, fall back to ConfigManager (disk read)
        if (folders.isEmpty()) {
            folders = ConfigManager::instance().workspaceFolders();
        }
        
        // Check if current project dir belongs to the workspace
        bool inWorkspace = false;
        for (const QString& wf : folders) {
            if (projectDir == wf || projectDir.startsWith(wf + "/")) {
                inWorkspace = true; break;
            }
        }
        
        if (inWorkspace && folders.size() > 1) {
            m_projectExplorer->setWorkspaceFolders(folders);
        } else if (!folders.isEmpty() && folders.size() == 1) {
            m_projectExplorer->setRootPath(folders.first());
        } else {
            m_projectExplorer->setRootPath(projectDir);
        }
    }

    // Initialize source control for this project
    if (!projectDir.isEmpty()) {
        SourceControlManager::instance().setProjectDir(projectDir);
    }



    // Auto-derive file path from project
    if (!projectName.isEmpty() && !projectDir.isEmpty()) {
        QString derivedPath = projectDir + "/" + projectName + ".flxsch";
        if (m_currentFilePath.isEmpty()) {
            m_currentFilePath = derivedPath;
            updateGeminiProjectEffect();
            setWindowTitle(QString("viospice - Schematic Editor [%1.flxsch]").arg(projectName));
        }
    }
}

void SchematicEditor::onNewSchematic() {
    addSchematicTab("New Schematic");
}

void SchematicEditor::onOpenSchematic() {
    if (m_isModified) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, "Unsaved Changes",
            "Do you want to save changes before opening another schematic?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel
        );
        if (reply == QMessageBox::Save) {
            onSaveSchematic();
            if (m_isModified) return;
        } else if (reply == QMessageBox::Cancel) {
            return;
        }
    }
    
    QString filePath = QFileDialog::getOpenFileName(
        this, "Open Schematic", QString(),
        "viospice Schematic (*.flxsch *.flux *.sch *.asc);;LTspice Schematic (*.asc);;FluxScript (*.flux);;KiCad Schematic (*.kicad_sch);;Altium Schematic (*.SchDoc);;All Files (*)"
    );
    if (filePath.isEmpty()) return;
    m_navigationStack.clear();
    openFile(filePath);
}

void SchematicEditor::onImportAscFile() {
    if (m_isModified) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, "Unsaved Changes",
            "Do you want to save changes before importing an ASC schematic?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel
        );
        if (reply == QMessageBox::Save) {
            onSaveSchematic();
            if (m_isModified) return;
        } else if (reply == QMessageBox::Cancel) {
            return;
        }
    }

    const QString filePath = QFileDialog::getOpenFileName(
        this,
        "Import LTspice ASC",
        QString(),
        "LTspice Schematic (*.asc);;All Files (*)"
    );
    if (filePath.isEmpty()) return;

    m_navigationStack.clear();
    openFile(filePath);
}

bool SchematicEditor::openFile(const QString& filePath) {
    if (filePath.isEmpty()) return false;

    // Detect and handle project files (.viospice) by opening their schematic instead
    if (filePath.endsWith(".viospice", Qt::CaseInsensitive)) {
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            QJsonObject obj = doc.object();
            QString schFile = obj["schematicFile"].toString();
            file.close();
            
            if (schFile.isEmpty()) {
                // Fallback: infer schematic filename from project name or file basename
                QString projName = obj["name"].toString();
                if (projName.isEmpty()) {
                    projName = QFileInfo(filePath).completeBaseName();
                }
                schFile = projName + ".flxsch";
            }
            
            // Resolve relative path based on project location
            QFileInfo schInfo(schFile);
            if (schInfo.isRelative()) {
                schFile = QFileInfo(filePath).absolutePath() + "/" + schFile;
            }
            
            if (QFile::exists(schFile)) {
                // Set project context before opening the schematic
                QFileInfo projInfo(filePath);
                setProjectContext(projInfo.completeBaseName(), projInfo.absolutePath());
                return openFile(schFile);
            }
        }
        statusBar()->showMessage("Failed to resolve schematic from project file.", 3000);
        return false; // MUST return here to prevent fallthrough to schematic loading
    }

    // Check if already open
    for (int i = 0; i < m_workspaceTabs->count(); ++i) {
        if (m_workspaceTabs->widget(i)->property("filePath").toString() == filePath) {
            m_workspaceTabs->setCurrentIndex(i);
            return true;
        }
    }

    if (isTextFile(filePath)) {
        // Check if already open
        for (int i = 0; i < m_workspaceTabs->count(); ++i) {
            if (m_workspaceTabs->widget(i)->property("filePath").toString() == filePath) {
                m_workspaceTabs->setCurrentIndex(i);
                return true;
            }
        }

        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::critical(this, "Open Error",
                QString("Failed to open file:\n%1").arg(file.errorString()));
            return false;
        }
        const QString content = QString::fromUtf8(file.readAll());
        file.close();

        auto* editor = createTextEditor(this, content, filePath, false);
        editor->setProperty("filePath", filePath);
        editor->setProperty("dirty", false);
        connect(editor, &QPlainTextEdit::textChanged, this, [this, editor]() {
            if (!editor->property("dirty").toBool()) {
                editor->setProperty("dirty", true);
                const int idx = m_workspaceTabs->indexOf(editor);
                if (idx >= 0) m_workspaceTabs->setTabText(idx, m_workspaceTabs->tabText(idx) + "*");
            }
        });
        const int idx = m_workspaceTabs->addTab(editor, getThemeIcon(":/icons/tool_search.svg"), QFileInfo(filePath).fileName());
        m_workspaceTabs->setCurrentIndex(idx);
        statusBar()->showMessage(QString("Opened: %1").arg(filePath), 3000);
        return true;
    }

    if (filePath.endsWith(".sym", Qt::CaseInsensitive)) {
        openSymbolEditorWindow(QFileInfo(filePath).baseName());
        // For now, assume it's a new symbol or handle loading logic in SymbolEditor
        return true;
    }

    // Create or reuse a schematic tab for this file
    bool reused = false;
    int currentIdx = m_workspaceTabs->currentIndex();
    if (canReuseTab(currentIdx)) {
        m_workspaceTabs->setTabText(currentIdx, QFileInfo(filePath).fileName());
        m_view->setProperty("filePath", filePath);
        reused = true;
    } else {
        addSchematicTab(QFileInfo(filePath).fileName());
        m_view->setProperty("filePath", filePath);
    }

    if (filePath.endsWith(".flux", Qt::CaseInsensitive)) {
        /* Legacy FluxScript file loading disabled
        QFile file(filePath);
        ...
        */
        QMessageBox::information(this, "Legacy Format", "FluxScript files (.flux) are currently disabled during the migration to Python.");
        return false;
    }

    QString loadedPageSize;
    QString embeddedScript;
    QMap<QString, QList<QString>> loadedBusAliases;
    QSet<QString> loadedErcExclusions;
    QJsonObject loadedSimulationSetup;
    if (SchematicFileIO::loadSchematic(m_scene, filePath, loadedPageSize, m_titleBlock, &embeddedScript, &loadedBusAliases, &loadedErcExclusions, &loadedSimulationSetup)) {
        m_currentFilePath = filePath;
        updateGeminiProjectEffect();
        m_currentPageSize = loadedPageSize;
        m_isModified = false;
        m_busAliases = loadedBusAliases;
        m_ercExclusions = loadedErcExclusions;
        if (m_netManager) m_netManager->setBusAliases(m_busAliases);

        if (!loadedSimulationSetup.isEmpty()) {
            m_simConfig = SimulationSetupDialog::Config::fromJson(loadedSimulationSetup);
        }
        if (m_simulationPanel) {
            SimulationPanel::AnalysisConfig pCfg;
            pCfg.type = m_simConfig.type;
            pCfg.stop = m_simConfig.stop;
            pCfg.step = m_simConfig.step;
            pCfg.fStart = m_simConfig.fStart;
            pCfg.fStop = m_simConfig.fStop;
            pCfg.pts = m_simConfig.pts;
            m_simulationPanel->setAnalysisConfig(pCfg);
            
            // Sync schematic directive with saved command text
            if (!m_simConfig.commandText.isEmpty()) {
                m_simulationPanel->updateSchematicDirectiveFromCommand(m_simConfig.commandText);
            }
        }

        if (!embeddedScript.isEmpty() && m_scriptPanel) {
            m_scriptPanel->setScript(embeddedScript);
        }
        
        updateGrid();
        updatePageFrame();
        
        // Sync hierarchical ports for any sheets in this file
        for (auto* item : m_scene->items()) {
            if (auto* sheet = dynamic_cast<SchematicSheetItem*>(item)) {
                sheet->updatePorts(QFileInfo(filePath).absolutePath());
            }
        }

        SchematicConnectivity::updateVisualConnections(m_scene);
        
        m_ercList->clear();
        m_ercDock->hide();

        QFileInfo fileInfo(filePath);
        setWindowTitle(QString("viospice - Schematic Editor [%1]").arg(fileInfo.fileName()));
        updateBreadcrumbs();
        statusBar()->showMessage(QString("Loaded: %1").arg(filePath), 5000);
        return true;
    } else {
        QMessageBox::critical(this, "Load Error",
            QString("Failed to load schematic:\n%1").arg(SchematicFileIO::lastError()));
        return false;
    }
}

void SchematicEditor::onSaveSchematic() {
    // If current tab is an unsaved schematic, force Save As to avoid overwriting another file
    if (m_view && m_view->property("filePath").toString().isEmpty()) {
        onSaveSchematicAs();
        return;
    }

    QWidget* current = m_workspaceTabs->currentWidget();
    if (auto* textEditor = qobject_cast<QPlainTextEdit*>(current)) {
        const QString textPath = textEditor->property("filePath").toString();
        if (!textPath.isEmpty() && isTextFile(textPath)) {
            QFile file(textPath);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QMessageBox::critical(this, "Save Error",
                    QString("Failed to save file:\n%1").arg(file.errorString()));
                return;
            }
            file.write(textEditor->toPlainText().toUtf8());
            file.close();

            textEditor->setProperty("dirty", false);
            updateCurrentTabTitleFromFilePath(textPath);
            statusBar()->showMessage(QString("Saved: %1").arg(textPath), 3000);
            return;
        }
    }

    // If we have a project context but no file path yet, derive it
    if (m_currentFilePath.isEmpty() && !m_projectName.isEmpty() && !m_projectDir.isEmpty()) {
        m_currentFilePath = m_projectDir + "/" + m_projectName + ".flxsch";
        updateGeminiProjectEffect();
    }
    
    if (m_currentFilePath.isEmpty()) {
        onSaveSchematicAs();
        return;
    }
    
    // Ensure directory exists
    QFileInfo fi(m_currentFilePath);
    QDir().mkpath(fi.absolutePath());
    
    bool success = false;
    if (m_currentFilePath.endsWith(".flux", Qt::CaseInsensitive)) {
        QString code = SchematicFileIO::convertToFluxScript(m_scene, m_netManager);
        QFile file(m_currentFilePath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(code.toUtf8());
            file.close();
            success = true;
        }
    } else {
        const QJsonObject simSetup = m_simConfig.toJson();
        success = SchematicFileIO::saveSchematic(m_scene, m_currentFilePath, m_currentPageSize, QString(), m_titleBlock, m_busAliases, m_ercExclusions, &simSetup);
    }

    if (success) {
        m_isModified = false;
        if (m_undoStack) m_undoStack->setClean();
        QFileInfo fileInfo(m_currentFilePath);
        setWindowTitle(QString("viospice - Schematic Editor [%1]").arg(fileInfo.fileName()));
        statusBar()->showMessage(QString("Saved: %1").arg(m_currentFilePath), 3000);
        if (m_view) m_view->setProperty("filePath", m_currentFilePath);
        updateCurrentTabTitleFromFilePath(m_currentFilePath);
        SourceControlManager::instance().scheduleRefresh();
    } else {
        QMessageBox::critical(this, "Save Error",
            QString("Failed to save schematic:\n%1").arg(SchematicFileIO::lastError()));
    }
}

void SchematicEditor::onSaveSchematicAs() {
    QWidget* current = m_workspaceTabs->currentWidget();
    if (auto* textEditor = qobject_cast<QPlainTextEdit*>(current)) {
        QString defaultPath = textEditor->property("filePath").toString();
        if (defaultPath.isEmpty()) {
            defaultPath = m_projectDir.isEmpty() ? QDir::homePath() : m_projectDir;
        }
        QString filePath = QFileDialog::getSaveFileName(
            this, "Save File As",
            defaultPath,
            "All Files (*)"
        );
        if (filePath.isEmpty()) return;

        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::critical(this, "Save Error",
                QString("Failed to save file:\n%1").arg(file.errorString()));
            return;
        }
        file.write(textEditor->toPlainText().toUtf8());
        file.close();

        textEditor->setProperty("filePath", filePath);
        textEditor->setProperty("dirty", false);
        updateCurrentTabTitleFromFilePath(filePath);
        statusBar()->showMessage(QString("Saved: %1").arg(filePath), 3000);
        SourceControlManager::instance().scheduleRefresh();
        return;
    }

    if (m_projectName.isEmpty()) {
        // First time saving, prompt to create a project
        QString projectName = QInputDialog::getText(this, "Save New Project", "Project Name:");
        if (projectName.isEmpty()) return;

        QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        QDir().mkpath(defaultPath);
        QString projectPath = QFileDialog::getExistingDirectory(this, "Choose Project Location", defaultPath);
        if (projectPath.isEmpty()) return;

        QString fullProjectPath = projectPath + "/" + projectName;
        
        Project* project = new Project(projectName, fullProjectPath);
        if (project->createNew()) {
            RecentProjects::instance().addProject(project->projectFilePath());
            setProjectContext(projectName, fullProjectPath);
            m_currentFilePath = fullProjectPath + "/" + projectName + ".flxsch";
            delete project;
            
            // Re-route to onSaveSchematic now that we have a context and path
            onSaveSchematic();
            return;
        } else {
            QMessageBox::critical(this, "Error", "Failed to create project");
            delete project;
            return;
        }
    }

    // Default to project-derived name if available
    QString defaultPath;
    if (!m_projectName.isEmpty() && !m_projectDir.isEmpty()) {
        defaultPath = m_projectDir + "/" + m_projectName + ".flxsch";
    } else if (!m_currentFilePath.isEmpty()) {
        defaultPath = m_currentFilePath;
    } else {
        defaultPath = "untitled.flxsch";
    }
    
    QString filePath = QFileDialog::getSaveFileName(
        this, "Save Schematic As",
        defaultPath,
        "viospice Schematic (*.flxsch);;FluxScript (*.flux);;KiCad Schematic (*.kicad_sch);;Altium Schematic (*.SchDoc);;All Files (*)"
    );
    if (filePath.isEmpty()) return;
    
    QFileInfo fi(filePath);
    if (fi.suffix().isEmpty())
        filePath += ".flxsch";
    
    const QJsonObject simSetup = m_simConfig.toJson();
    if (SchematicFileIO::saveSchematic(m_scene, filePath, m_currentPageSize, QString(), m_titleBlock, m_busAliases, m_ercExclusions, &simSetup)) {
        const QFileInfo savedInfo(filePath);
        const QString newProjectName = savedInfo.completeBaseName();
        const QString newProjectDir = savedInfo.absolutePath();
        const bool projectChanged = (newProjectName != m_projectName) || (newProjectDir != m_projectDir);

        if (projectChanged && !newProjectName.isEmpty() && !newProjectDir.isEmpty()) {
            QDir().mkpath(newProjectDir);
            Project project(newProjectName, newProjectDir);
            if (project.save()) {
                RecentProjects::instance().addProject(project.projectFilePath());
            }
            setProjectContext(newProjectName, newProjectDir);
        }

        m_currentFilePath = filePath;
        updateGeminiProjectEffect();
        m_isModified = false;
        if (m_undoStack) m_undoStack->setClean();
        QFileInfo fileInfo(filePath);
        setWindowTitle(QString("viospice - Schematic Editor [%1]").arg(fileInfo.fileName()));
        statusBar()->showMessage(QString("Saved: %1").arg(filePath), 3000);
        if (m_view) m_view->setProperty("filePath", filePath);
        updateCurrentTabTitleFromFilePath(filePath);
        SourceControlManager::instance().scheduleRefresh();
    } else {
        QMessageBox::critical(this, "Save Error",
            QString("Failed to save schematic:\n%1").arg(SchematicFileIO::lastError()));
    }
}

void SchematicEditor::updateCurrentTabTitleFromFilePath(const QString& filePath) {
    if (filePath.isEmpty()) return;
    const int idx = m_workspaceTabs->currentIndex();
    if (idx < 0) return;
    const QFileInfo info(filePath);
    const QString title = info.fileName();
    if (!title.isEmpty()) m_workspaceTabs->setTabText(idx, title);
    m_workspaceTabs->setTabToolTip(idx, filePath);
}

void SchematicEditor::onExportAISchematic() {
    if (!m_scene) return;

    QString defaultPath = m_projectDir.isEmpty() ? QDir::homePath() : m_projectDir;
    QString suggested = defaultPath + "/" + (m_projectName.isEmpty() ? "schematic" : m_projectName) + ".ai.json";
    QString filePath = QFileDialog::getSaveFileName(this, "Export AI Schematic", suggested, "AI Schematic JSON (*.ai.json);;All Files (*)");
    if (filePath.isEmpty()) return;

    QString script = SchematicFileIO::convertToFluxScript(m_scene, m_netManager);
    const QJsonObject simSetup = m_simConfig.toJson();
    bool success = SchematicFileIO::saveSchematicAI(m_scene, filePath, m_currentPageSize, script, m_titleBlock, m_busAliases, m_ercExclusions, &simSetup);

    if (success) {
        statusBar()->showMessage(QString("AI schematic exported: %1").arg(filePath), 3000);
        SourceControlManager::instance().scheduleRefresh();
    } else {
        QMessageBox::critical(this, "Export Error",
            QString("Failed to export AI schematic:\n%1").arg(SchematicFileIO::lastError()));
    }
}

#include "schematic_annotator.h"
#include "schematic_commands.h"

void SchematicEditor::onAnnotate() {
    if (!m_scene) return;
    
    // Check if we have any hierarchical sheets
    bool isHierarchical = false;
    for (auto* item : m_scene->items()) {
        if (dynamic_cast<class SchematicSheetItem*>(item)) {
            isHierarchical = true;
            break;
        }
    }

    if (isHierarchical && !m_currentFilePath.isEmpty()) {
        // Project-wide annotation (saves changes to files on disk)
        SchematicAnnotator::annotateProject(m_currentFilePath, m_projectDir, true);
        // Reload current sheet to show new references
        openFile(m_currentFilePath);
        statusBar()->showMessage("Project-wide annotation complete", 5000);
        return;
    }

    // 1. Capture old state (for single sheet undo support)
    QMap<SchematicItem*, QString> oldRefs;
    for (QGraphicsItem* gi : m_scene->items()) {
        if (auto* si = dynamic_cast<SchematicItem*>(gi)) {
            oldRefs[si] = si->reference();
        }
    }

    // 2. Perform annotation
    QMap<SchematicItem*, QString> newRefs = SchematicAnnotator::annotate(m_scene, true, SchematicAnnotator::TopToBottom);
    
    if (!newRefs.isEmpty()) {
        // Restore old values temporarily to let command handle redo properly
        for (auto it = newRefs.begin(); it != newRefs.end(); ++it) {
            it.key()->setReference(oldRefs[it.key()]);
        }
        
        m_undoStack->push(new SchematicAnnotateCommand(m_scene, oldRefs, newRefs));
        m_view->update();
        statusBar()->showMessage("Sheet annotated successfully", 5000);
    }
}

void SchematicEditor::onResetAnnotations() {
    if (!m_scene) return;

    QMap<SchematicItem*, QString> oldRefs;
    for (QGraphicsItem* gi : m_scene->items()) {
        if (auto* si = dynamic_cast<SchematicItem*>(gi)) {
            oldRefs[si] = si->reference();
        }
    }

    QMap<SchematicItem*, QString> newRefs = SchematicAnnotator::resetAnnotations(m_scene);
    
    if (!newRefs.isEmpty()) {
        for (auto it = newRefs.begin(); it != newRefs.end(); ++it) {
            it.key()->setReference(oldRefs[it.key()]);
        }
        
        m_undoStack->push(new SchematicAnnotateCommand(m_scene, oldRefs, newRefs));
        m_view->update();
        statusBar()->showMessage("All component annotations reset", 5000);
    }
}

#include "../items/erc_marker_item.h"
#include "../dialogs/bus_aliases_dialog.h"


void SchematicEditor::onRunERC() {
    // 1. Clear previous markers
    for (QGraphicsItem* item : m_scene->items()) {
        if (dynamic_cast<ERCMarkerItem*>(item)) {
            m_scene->removeItem(item);
            delete item;
        }
    }

    m_ercList->clear();
    QList<ERCViolation> violations = SchematicERC::run(m_scene, m_projectDir, m_netManager, m_ercRules);
    
    if (violations.isEmpty()) {
        statusBar()->showMessage("ERC Check Passed: No violations found. ✨", 5000);
        QMessageBox::information(this, "ERC Results", "No electrical rules violations found. Your schematic looks clean!");
        m_ercDock->hide();
    } else {
        m_ercDock->show();
        m_ercDock->raise();
        
        int errors = 0;
        int warnings = 0;
        int criticals = 0;

        int excludedCount = 0;
        for (const ERCViolation& v : violations) {
            const QString key = ercViolationKey(v);
            if (m_ercExclusions.contains(key)) {
                excludedCount++;
                continue;
            }
            // Add visual marker to scene
            m_scene->addItem(new ERCMarkerItem(v));

            QString typeStr;
            QColor color;
            if (v.severity == ERCViolation::Critical) {
                typeStr = "CRITICAL";
                color = Qt::magenta;
                criticals++;
            } else if (v.severity == ERCViolation::Error) {
                typeStr = "ERROR";
                color = Qt::red;
                errors++;
            } else {
                typeStr = "Warning";
                color = QColor("#d97706"); // Amber
                warnings++;
            }

            QString categoryPrefix;
            if (v.category == ERCViolation::Conflict) categoryPrefix = "[Conflict] ";
            else if (v.category == ERCViolation::Connectivity) categoryPrefix = "[Conn] ";
            else if (v.category == ERCViolation::Annotation) categoryPrefix = "[Ref] ";

            QString text = QString("[%1] %2%3").arg(typeStr, categoryPrefix, v.message);
            if (!v.netName.isEmpty()) text += QString(" (Net: %1)").arg(v.netName);

            QListWidgetItem* item = new QListWidgetItem(text);
            item->setData(Qt::UserRole, v.position);
            item->setData(Qt::UserRole + 1, key);
            item->setForeground(color);
            item->setFont(QFont("Inter", 9, v.severity == ERCViolation::Critical ? QFont::Bold : QFont::Normal));
            
            m_ercList->addItem(item);
        }

        if (m_ercList->count() == 0) {
            statusBar()->showMessage(QString("All %1 ERC violations are excluded.").arg(excludedCount), 8000);
        } else {
            statusBar()->showMessage(QString("ERC Results: %1 Critical, %2 Errors, %3 Warnings (%4 excluded)")
                                     .arg(criticals).arg(errors).arg(warnings).arg(excludedCount), 10000);
        }
    }
}

void SchematicEditor::onIgnoreSelectedErc() {
    if (!m_ercList) return;
    const QList<QListWidgetItem*> selected = m_ercList->selectedItems();
    if (selected.isEmpty()) return;
    for (QListWidgetItem* item : selected) {
        const QString key = item->data(Qt::UserRole + 1).toString();
        if (!key.isEmpty()) m_ercExclusions.insert(key);
    }
    m_isModified = true;
    onRunERC();
}

void SchematicEditor::onClearErcExclusions() {
    if (m_ercExclusions.isEmpty()) return;
    m_ercExclusions.clear();
    m_isModified = true;
    onRunERC();
}

void SchematicEditor::onGenerateNetlist() {
    QString initialPath = m_currentFilePath.isEmpty() ? "netlist.json" : QFileInfo(m_currentFilePath).absolutePath() + "/netlist.json";
    QString file = QFileDialog::getSaveFileName(this, "Save Netlist", initialPath, "Flux Netlist (*.json);;IPC-D-356 (*.ipc);;Protel (*.net)");
    if (file.isEmpty()) return;

    NetlistGenerator::Format format = NetlistGenerator::FluxJSON;
    if (file.endsWith(".ipc")) format = NetlistGenerator::IPC356;
    else if (file.endsWith(".net")) format = NetlistGenerator::Protel;

    QString content = NetlistGenerator::generate(m_scene, m_projectDir, format, m_netManager);
    
    QFile f(file);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        f.write(content.toUtf8());
        f.close();
        statusBar()->showMessage("Netlist saved to " + file, 5000);
    } else {
        QMessageBox::warning(this, "Error", "Could not save netlist file.");
    }
}

#include "../dialogs/symbol_field_editor_dialog.h"

void SchematicEditor::onOpenSymbolFieldEditor() {
    if (m_currentFilePath.isEmpty()) {
        QMessageBox::information(this, "Symbol Field Editor",
                                 "Please save the schematic once so the field editor can scan project sheets.");
        onSaveSchematicAs();
        if (m_currentFilePath.isEmpty()) return;
    }

    SymbolFieldEditorDialog dlg(m_currentFilePath, m_projectDir, this);
    if (dlg.exec() == QDialog::Accepted) {
        // Reload current file to see changes
        openFile(m_currentFilePath);
    }
}

void SchematicEditor::onOpenBusAliasesManager() {
    BusAliasesDialog dlg(m_busAliases, this);
    if (dlg.exec() != QDialog::Accepted) return;

    m_busAliases = dlg.aliases();
    if (m_netManager) {
        m_netManager->setBusAliases(m_busAliases);
        m_netManager->updateNets(m_scene);
    }
    m_isModified = true;
    statusBar()->showMessage(QString("Updated %1 bus alias(es)").arg(m_busAliases.size()), 3000);
}

void SchematicEditor::onOpenBOM() {
    ECOPackage pkg = NetlistGenerator::generateECOPackage(m_scene, m_projectDir, m_netManager);
    BOMDialog dlg(pkg, this);
    dlg.exec();
}

#include "../analysis/spice_netlist_generator.h"

void SchematicEditor::onOpenNetlistEditor() {
    NetlistEditor* editor = new NetlistEditor(this);
    
    // Pre-populate with current schematic netlist
    if (m_scene && m_netManager) {
        m_netManager->updateNets(m_scene);
        
        SpiceNetlistGenerator::SimulationParams params;
        switch (m_simConfig.type) {
            case SimAnalysisType::Transient: params.type = SpiceNetlistGenerator::Transient; break;
            case SimAnalysisType::AC:        params.type = SpiceNetlistGenerator::AC; break;
            case SimAnalysisType::OP:        params.type = SpiceNetlistGenerator::OP; break;
            default:                         params.type = SpiceNetlistGenerator::Transient; break;
        }
        
        params.stop = QString::number(m_simConfig.stop);
        params.step = QString::number(m_simConfig.step);
        params.start = QString::number(m_simConfig.start);
        
        QString netlist = SpiceNetlistGenerator::generate(m_scene, m_projectDir, m_netManager, params);
        editor->setNetlist(netlist);
    }
    
    int idx = m_workspaceTabs->addTab(editor, getThemeIcon(":/icons/tool_sheet.svg"), "Netlist Editor");
    m_workspaceTabs->setCurrentIndex(idx);
}

#include "../items/net_label_item.h"
#include "../items/generic_component_item.h"
#include "schematic_item.h"

void SchematicEditor::handleIncomingECO() {
    if (!SyncManager::instance().hasPendingECO()) return;
    const SyncManager::ECOTarget target = SyncManager::instance().pendingECOTarget();
    if (target == SyncManager::ECOTarget::PCB) return;

    ECOPackage pkg = SyncManager::instance().pendingECO();
    statusBar()->showMessage("🔄 Applying Netlist Sync...", 3000);

    // 1. Map existing components by reference
    QMap<QString, SchematicItem*> compMap;
    for (auto* item : m_scene->items()) {
        if (auto* si = dynamic_cast<SchematicItem*>(item)) {
            QString ref = si->reference();
            if (!ref.isEmpty()) {
                compMap[ref] = si;
            }
        }
    }

    // 2. Update components
    bool modified = false;
    for (const auto& ecoComp : pkg.components) {
        if (compMap.contains(ecoComp.reference)) {
            SchematicItem* item = compMap[ecoComp.reference];
            
            if (!ecoComp.value.isEmpty() && item->value() != ecoComp.value) {
                item->setValue(ecoComp.value);
                modified = true;
            }
        }
    }

    if (modified) {
        m_isModified = true;
        m_scene->update();
    }

    SyncManager::instance().clearPendingECO();
    statusBar()->showMessage("Synchronization complete", 3000);
}

void SchematicEditor::onOpenGeminiAI() {
    GeminiDialog* dialog = new GeminiDialog(m_scene, this);
    
    // Connect dialog signals too, using QueuedConnection for GUI safety
    connect(dialog->panel(), &GeminiPanel::fluxScriptGenerated, this, [this](const QString& code) {
        if (m_scriptPanel) {
            m_scriptPanel->setScript(code);
            onOpenFluxScript(); 
            statusBar()->showMessage("AI generated FluxScript is ready in the editor!", 5000);
        }
    }, Qt::QueuedConnection);

    dialog->show();
}

void SchematicEditor::updateGeminiProjectEffect() {
    if (m_geminiPanel) {
        m_geminiPanel->setProjectFilePath(m_currentFilePath);
    }
}

void SchematicEditor::onItemsHighlighted(const QStringList& references) {
    if (!m_scene) return;
    
    m_scene->clearSelection();
    QRectF totalRect;
    bool found = false;
    
    for (auto* item : m_scene->items()) {
        auto* sItem = dynamic_cast<SchematicItem*>(item);
        if (sItem && references.contains(sItem->reference(), Qt::CaseInsensitive)) {
            sItem->setSelected(true);
            if (!found) totalRect = sItem->sceneBoundingRect();
            else totalRect = totalRect.united(sItem->sceneBoundingRect());
            found = true;
        }
    }
    
    if (found && m_view) {
        m_view->fitInView(totalRect.adjusted(-100, -100, 100, 100), Qt::KeepAspectRatio);
    }
}

void SchematicEditor::onSnippetGenerated(const QString& jsonSnippet) {
    if (!m_scene || !m_api) return;

    QJsonDocument doc = QJsonDocument::fromJson(jsonSnippet.toUtf8());
    if (!doc.isArray() && !doc.isObject()) return;

    // Handle both direct arrays of items and command-style snippets
    if (doc.isObject() && doc.object().contains("commands")) {
        m_undoStack->beginMacro("AI Generated Commands");
        m_api->executeBatch(doc.object()["commands"].toArray());
        m_undoStack->endMacro();
        statusBar()->showMessage("Executed AI generated commands", 3000);
        return;
    }

    QJsonArray items = doc.isArray() ? doc.array() : doc.object()["items"].toArray();
    if (items.isEmpty()) return;

    m_undoStack->beginMacro("Place AI Snippet");

    // Calculate offset to place at center of current view
    QPointF center = m_view ? m_view->mapToScene(m_view->viewport()->rect().center()) : QPointF(0,0);

    for (const auto& val : items) {
        QJsonObject obj = val.toObject();
        QString type = obj["type"].toString();
        QPointF pos(obj["x"].toDouble() + center.x(), obj["y"].toDouble() + center.y());
        QString ref = obj["reference"].toString();

        m_api->addComponent(type, pos, ref, obj);
    }
    m_undoStack->endMacro();

    statusBar()->showMessage(QString("Placed AI Snippet with %1 items").arg(items.size()), 3000);
}
QList<ERCViolation> SchematicEditor::getErcViolations() const {
    if (!m_scene) return {};
    return SchematicERC::run(m_scene, m_projectDir, m_netManager, m_ercRules);
}

void SchematicEditor::onOpenSymbolEditor() {
    openSymbolEditorWindow("New Symbol");
}

void SchematicEditor::onPlaceSymbolInSchematic(const SymbolDefinition& symbol) {
    if (!m_scene || !m_view) return;

    // Calculate center of current view
    QPointF center = m_view->mapToScene(m_view->viewport()->rect().center());
    
    // Create the item
    auto* item = new GenericComponentItem(symbol);
    item->setPos(center);
    
    // Use undo stack for the addition
    m_undoStack->push(new AddItemCommand(m_scene, item));
    
    // Select the new item and focus back to schematic
    m_scene->clearSelection();
    item->setSelected(true);
    
    this->activateWindow();
    this->raise();
    m_view->setFocus();
    
    statusBar()->showMessage(QString("Placed symbol: %1").arg(symbol.name()), 3000);
}

// ─── Create New Symbol From Schematic ──────────────────────────────────────

#include "../items/generic_component_item.h"
#include "../../symbols/models/symbol_primitive.h"
#include "../../core/config_manager.h"

/**
 * @brief Analyse selected (or all) GenericComponentItem instances on the
 *        canvas and build a default SymbolDefinition with an LTspice-style
 *        IC body: input pins on the left, output pins on the right.
 *
 *        If the canvas contains no components, a generic 4-pin stub is
 *        returned (2 input / 2 output).
 */
SymbolDefinition SchematicEditor::buildSymbolFromSelection() const {
    using Flux::Model::SymbolPrimitive;

    // ── 1. Collect pins from selected (or all) GenericComponentItems ─────
    struct PinInfo {
        QString name;
        QString type;  // "Input", "Output", "BiDir", "Power", "Passive", ""
    };
    QList<PinInfo> inputPins, outputPins, otherPins;

    auto* scene = m_scene ? m_scene : nullptr;
    if (!scene) {
        // Return minimal stub if no scene
        SymbolDefinition stub("NewSymbol");
        return stub;
    }

    QList<QGraphicsItem*> candidates;
    QList<QGraphicsItem*> selected = scene->selectedItems();
    if (!selected.isEmpty()) {
        candidates = selected;
    } else {
        candidates = scene->items();
    }

    for (QGraphicsItem* gi : candidates) {
        auto* comp = dynamic_cast<GenericComponentItem*>(gi);
        if (!comp) continue;

        const SymbolDefinition& sym = comp->symbol();
        for (const SymbolPrimitive& prim : sym.primitives()) {
            if (prim.type != SymbolPrimitive::Pin) continue;

            PinInfo info;
            info.name = prim.data.value("name").toString();
            if (info.name.isEmpty()) info.name = prim.data.value("label").toString();
            if (info.name.isEmpty()) info.name = QString("P%1").arg(outputPins.size() + inputPins.size() + otherPins.size() + 1);
            info.type = prim.data.value("type").toString();

            const QString lo = info.type.toLower();
            if (lo == "input" || lo == "in") {
                inputPins.append(info);
            } else if (lo == "output" || lo == "out") {
                outputPins.append(info);
            } else {
                otherPins.append(info);
            }
        }
    }

    // ── 2. If no pin data found, use a generic 4-pin stub ────────────────
    if (inputPins.isEmpty() && outputPins.isEmpty() && otherPins.isEmpty()) {
        inputPins  = { {"IN1", "Input"}, {"IN2", "Input"} };
        outputPins = { {"OUT1", "Output"}, {"OUT2", "Output"} };
    }

    // Distribute otherPins evenly between left and right
    for (int i = 0; i < otherPins.size(); ++i) {
        if (i % 2 == 0) inputPins.append(otherPins[i]);
        else            outputPins.append(otherPins[i]);
    }

    // ── 3. Compute body geometry ─────────────────────────────────────────
    const int pinCount = qMax(inputPins.size(), outputPins.size());
    const qreal pinSpacing = 20.0;
    const qreal bodyH = qMax(60.0, pinCount * pinSpacing + pinSpacing);
    const qreal bodyW = 60.0;
    const qreal halfH = bodyH / 2.0;
    const qreal halfW = bodyW / 2.0;

    SymbolDefinition def;
    // Name and prefix will be filled in by the dialog before calling this

    // ── 4. Body rectangle ─────────────────────────────────────────────────
    {
        SymbolPrimitive rect;
        rect.type = SymbolPrimitive::Rect;
        rect.data["x"] = -halfW;
        rect.data["y"] = -halfH;
        rect.data["width"]  = bodyW;
        rect.data["height"] = bodyH;
        rect.data["filled"] = false;
        rect.data["lineWidth"] = 1.5;
        def.addPrimitive(rect);
    }

    // ── 5. Input pins (left side) ─────────────────────────────────────────
    auto addPin = [&](int number, const QString& name, const QString& type, qreal x, qreal y, const QString& orientation) {
        SymbolPrimitive pin = SymbolPrimitive::createPin(QPointF(x, y), number, name, orientation, 15.0);
        pin.data["label"] = name;
        pin.data["electricalType"] = type;
        pin.data["visible"] = true;
        def.addPrimitive(pin);
    };

    const qreal topOffset = -halfH + pinSpacing / 2.0;
    int pinNum = 1;
    for (int i = 0; i < inputPins.size(); ++i) {
        qreal y = topOffset + i * pinSpacing;
        addPin(pinNum++, inputPins[i].name, "Input", -halfW, y, "Right");
    }
    for (int i = 0; i < outputPins.size(); ++i) {
        qreal y = topOffset + i * pinSpacing;
        addPin(pinNum++, outputPins[i].name, "Output", halfW, y, "Left");
    }

    return def;
}

void SchematicEditor::onCreateSymbolFromSchematic() {
    using Flux::Model::SymbolPrimitive;

    // ── Step 1: Ask for symbol metadata ───────────────────────────────────
    bool ok = false;
    QString symbolName = QInputDialog::getText(
        this, "Create New Symbol from Schematic",
        "Symbol Name:", QLineEdit::Normal, "MySubckt", &ok);
    if (!ok || symbolName.trimmed().isEmpty()) return;
    symbolName = symbolName.trimmed();

    QString prefix = QInputDialog::getText(
        this, "Symbol Reference Prefix",
        "Reference Prefix (e.g. X, U, Q):", QLineEdit::Normal, "X", &ok);
    if (!ok) return;
    if (prefix.trimmed().isEmpty()) prefix = "X";
    prefix = prefix.trimmed();

    // Category selection
    QStringList categories = {"Integrated Circuits", "Subcircuits", "Custom", "Other"};
    QString category = QInputDialog::getItem(
        this, "Symbol Category", "Category:", categories, 0, false, &ok);
    if (!ok) category = "Custom";

    // ── Step 2: Preflight and build pin order from Net Labels ────────────
    struct OrderedPin {
        int order = 0;
        QString name;
    };
    QVector<OrderedPin> orderedPins;
    {
        QStringList errors;
        QStringList warnings;

        QList<QGraphicsItem*> selected = m_scene ? m_scene->selectedItems() : QList<QGraphicsItem*>();
        QList<QGraphicsItem*> candidates = !selected.isEmpty() ? selected : (m_scene ? m_scene->items() : QList<QGraphicsItem*>());
        QSet<QGraphicsItem*> selectedComps;
        for (QGraphicsItem* gi : candidates) {
            if (dynamic_cast<GenericComponentItem*>(gi)) selectedComps.insert(gi);
        }

        if (selectedComps.isEmpty()) {
            warnings << "[warn] No components selected. Symbol will be built from labeled nets only.";
        }

        const QRegularExpression orderRe("^\\s*(\\d+)\\s*:\\s*(\\S.+)\\s*$");
        QSet<int> usedOrders;
        QSet<QString> usedNames;

        for (QGraphicsItem* gi : candidates) {
            QString label;
            if (auto* nl = dynamic_cast<NetLabelItem*>(gi)) {
                label = nl->label().trimmed();
            } else if (auto* hp = dynamic_cast<HierarchicalPortItem*>(gi)) {
                label = hp->label().trimmed();
            } else {
                continue;
            }

            if (label.isEmpty() || label.compare("NET", Qt::CaseInsensitive) == 0) {
                errors << "[error] Net Label must use prefix order like \"1:IN\" (default \"NET\" is not valid).";
                continue;
            }

            QRegularExpressionMatch m = orderRe.match(label);
            if (!m.hasMatch()) {
                errors << QString("[error] Net Label \"%1\" must be in the form \"N:NAME\" (e.g., 1:IN).").arg(label);
                continue;
            }

            const int order = m.captured(1).toInt();
            const QString name = m.captured(2).trimmed();
            if (order <= 0) {
                errors << QString("[error] Net Label \"%1\" has invalid order.").arg(label);
                continue;
            }
            if (name.isEmpty() || name.contains(QRegularExpression("\\s"))) {
                errors << QString("[error] Net Label \"%1\" has invalid pin name (no spaces).").arg(label);
                continue;
            }
            if (usedOrders.contains(order)) {
                errors << QString("[error] Duplicate pin order %1 in net labels.").arg(order);
                continue;
            }
            if (usedNames.contains(name)) {
                warnings << QString("[warn] Duplicate pin name \"%1\".").arg(name);
            }

            usedOrders.insert(order);
            usedNames.insert(name);
            orderedPins.push_back({order, name});
        }

        if (orderedPins.isEmpty()) {
            errors << "[error] At least one Net Label with prefix order is required (e.g., 1:IN).";
        }

        std::sort(orderedPins.begin(), orderedPins.end(), [](const OrderedPin& a, const OrderedPin& b) {
            return a.order < b.order;
        });

        if (!orderedPins.isEmpty()) {
            int expected = orderedPins.front().order;
            for (const auto& p : orderedPins) {
                if (p.order != expected) {
                    warnings << QString("[warn] Pin order has gaps (expected %1, saw %2).").arg(expected).arg(p.order);
                    expected = p.order + 1;
                } else {
                    ++expected;
                }
            }
        }

        if (!errors.isEmpty()) {
            QMessageBox::critical(this, "Symbol Preflight", errors.join("\n"));
            return;
        }

        if (!warnings.isEmpty()) {
            QMessageBox msg(this);
            msg.setIcon(QMessageBox::Warning);
            msg.setWindowTitle("Symbol Preflight");
            msg.setText("Potential issues detected before opening the Symbol Editor.");
            msg.setDetailedText(warnings.join("\n"));
            msg.setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
            msg.setDefaultButton(QMessageBox::Cancel);
            msg.button(QMessageBox::Ok)->setText("Continue");
            if (msg.exec() != QMessageBox::Ok) return;
        }
    }

    // ── Step 3: Build SymbolDefinition from ordered net labels ───────────
    SymbolDefinition def;
    def.setName(symbolName);
    def.setReferencePrefix(prefix);
    def.setCategory(category);
    def.setDescription(QString("Custom subcircuit created from schematic on %1")
                       .arg(QDate::currentDate().toString(Qt::ISODate)));
    def.setModelSource("sub");
    def.setModelPath(symbolName + ".sub");
    def.setModelName(symbolName);

    const int pinCount = orderedPins.size();
    const qreal pinSpacing = 20.0;
    const qreal bodyH = qMax(60.0, pinCount * pinSpacing + pinSpacing);
    const qreal bodyW = 60.0;
    const qreal halfH = bodyH / 2.0;
    const qreal halfW = bodyW / 2.0;

    {
        SymbolPrimitive rect;
        rect.type = SymbolPrimitive::Rect;
        rect.data["x"] = -halfW;
        rect.data["y"] = -halfH;
        rect.data["width"]  = bodyW;
        rect.data["height"] = bodyH;
        rect.data["filled"] = false;
        rect.data["lineWidth"] = 1.5;
        def.addPrimitive(rect);
    }

    auto addPin = [&](int number, const QString& name, const QString& type, qreal x, qreal y, const QString& orientation) {
        SymbolPrimitive pin = SymbolPrimitive::createPin(QPointF(x, y), number, name, orientation, 15.0);
        pin.data["label"] = name;
        pin.data["electricalType"] = type;
        pin.data["visible"] = true;
        def.addPrimitive(pin);
    };

    int leftIdx = 0;
    int rightIdx = 0;
    const qreal topOffset = -halfH + pinSpacing / 2.0;
    for (int i = 0; i < orderedPins.size(); ++i) {
        const auto& p = orderedPins[i];
        const bool leftSide = (i % 2 == 0);
        qreal y = topOffset + (leftSide ? leftIdx++ : rightIdx++) * pinSpacing;
        addPin(p.order, p.name, "Passive", leftSide ? -halfW : halfW, y, leftSide ? "Right" : "Left");
    }

    // ── Step 4: Collect pin list for the .sub stub ───────────────────────
    QStringList pinNames;
    for (const auto& p : orderedPins) {
        if (!p.name.isEmpty() && !pinNames.contains(p.name)) pinNames.append(p.name);
    }

    // ── Step 5: Save a .sub SPICE subcircuit stub ────────────────────────
    QString libSubDir;
    const QStringList roots = ConfigManager::instance().libraryRoots();
    if (!roots.isEmpty()) {
        libSubDir = QDir(roots.first()).filePath("sub");
    } else {
        libSubDir = QDir::homePath() + "/ViospiceLib/sub";
    }
    QDir().mkpath(libSubDir);

    const QString subFilePath = QDir(libSubDir).filePath(symbolName + ".sub");
    QFile subFile(subFilePath);
    if (subFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ts(&subFile);
        // Build a subcircuit body from the current schematic
        SpiceNetlistGenerator::SimulationParams params;
        params.type = SpiceNetlistGenerator::OP;
        QString fullNetlist = SpiceNetlistGenerator::generate(m_scene, m_projectDir, m_netManager, params);
        QStringList bodyLines;
        bool inControl = false;
        const QStringList lines = fullNetlist.split('\n');
        for (const QString& rawLine : lines) {
            QString line = rawLine;
            QString t = line.trimmed().toLower();
            if (t.startsWith(".control")) { inControl = true; continue; }
            if (inControl) {
                if (t.startsWith(".endc")) inControl = false;
                continue;
            }
            if (t.startsWith(".tran") || t.startsWith(".op") || t.startsWith(".ac") || t.startsWith(".dc") ||
                t.startsWith(".end") || t == "run" || t.startsWith(".save")) {
                continue;
            }
            bodyLines << line;
        }

        ts << "* Subcircuit: " << symbolName << "\n";
        ts << "* Generated by viospice – " << QDate::currentDate().toString(Qt::ISODate) << "\n";
        ts << "*\n";
        ts << ".subckt " << symbolName;
        for (const QString& pin : pinNames) ts << " " << pin;
        ts << "\n";
        if (!bodyLines.isEmpty()) {
            ts << bodyLines.join("\n").trimmed() << "\n";
        } else {
            ts << "* Warning: No component items were found in the schematic to generate a netlist body.\n";
        }
        ts << ".ends " << symbolName << "\n";
        subFile.close();
        statusBar()->showMessage(
            QString("Stub saved: %1").arg(subFilePath), 4000);
    } else {
        QMessageBox::warning(this, "Warning",
            QString("Could not write subcircuit stub to:\n%1\n\nYou can create it manually later.").arg(subFilePath));
    }

    // Prefer absolute model path so ngspice can always find the .sub
    def.setModelPath(subFilePath);

    // ── Step 6: Open Symbol Editor with the pre-built definition ─────────
    openSymbolEditorWindow(symbolName, def);
}
