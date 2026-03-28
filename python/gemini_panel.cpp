#include "gemini_panel.h"
#include "python_manager.h"
#include "config_manager.h"
#include "schematic_file_io.h"
#include "theme_manager.h"
#include <QMenu>
#include <QUndoStack>
#include <QMessageBox>
#include <QAction>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QPushButton>
#include <QScrollBar>
#include <QTextCursor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QClipboard>
#include <QApplication>
#include <QBuffer>
#include <QPainter>
#include <QGraphicsView>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>
#include <QSet>
#include <QDockWidget>
#include <QDateTime>
#include <QCheckBox>
#include <QDialog>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QToolButton>
#include <QKeyEvent>
#include <QSizePolicy>

namespace {
QString compactErrorSummary(const QString& raw, int maxLen = 180) {
    QString text = raw;
    text.replace("\r\n", "\n");
    text = text.trimmed();
    if (text.isEmpty()) return QString("Unknown error");
    const int nl = text.indexOf('\n');
    if (nl >= 0) text = text.left(nl).trimmed();
    if (text.size() > maxLen) text = text.left(maxLen) + "...";
    return text;
}

QString nowTimeChip() {
    return QDateTime::currentDateTime().toString("HH:mm");
}

QString actionToSubtitle(QString actionText) {
    QString s = actionText.trimmed();
    if (s.isEmpty()) return QString("Executing assistant tools.");
    s.replace('_', ' ');
    return s;
}

bool isBenignBackendWarningLine(const QString& line) {
    const QString t = line.trimmed();
    if (t.isEmpty()) return true;
    return t.contains("non-text parts in the response", Qt::CaseInsensitive)
        || t.contains("function_call", Qt::CaseInsensitive)
        || t.contains("candidates.content.parts", Qt::CaseInsensitive);
}

QString sanitizeAgentTextChunk(QString text) {
    text.remove(QRegularExpression(R"(</?(THOUGHT|ACTION|SUGGESTION|HIGHLIGHT)>)", QRegularExpression::CaseInsensitiveOption));
    text.remove(QRegularExpression(R"((^|\s)(THOUGHT|ACTION|SUGGESTION|HIGHLIGHT)>)", QRegularExpression::CaseInsensitiveOption));
    return text;
}

QString markdownDocStyleSheet() {
    bool isLight = ThemeManager::theme() && ThemeManager::theme()->type() == PCBTheme::Light;
    if (isLight) {
        return QString(
            "body { color: #1e293b; }"
            "p { margin: 0 0 10px 0; line-height: 1.7; }"
            "h1, h2, h3, h4 { color: #0f172a; margin: 12px 0 8px 0; font-weight: 700; }"
            "h1 { font-size: 18px; border-bottom: 1px solid #e2e8f0; padding-bottom: 6px; }"
            "h2 { font-size: 16px; border-bottom: 1px solid #f1f5f9; padding-bottom: 4px; }"
            "h3 { font-size: 14px; }"
            "ul, ol { margin: 6px 0 12px 22px; }"
            "li { margin: 0 0 6px 0; line-height: 1.6; }"
            "blockquote { margin: 10px 0; padding: 8px 12px; border-left: 3px solid #3b82f6; background: #eff6ff; color: #1e40af; }"
            "code { background: #f1f5f9; color: #2563eb; border: 1px solid #e2e8f0; border-radius: 4px; padding: 1px 5px; font-family: 'JetBrains Mono', 'Consolas', monospace; font-size: 12px; }"
            "pre { margin: 12px 0; padding: 12px; background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 8px; }"
            "pre code { background: transparent; border: none; padding: 0; color: #334155; font-size: 12px; }"
            "table { border-collapse: collapse; margin: 10px 0; width: 100%; }"
            "th, td { border: 1px solid #e2e8f0; padding: 6px 8px; text-align: left; }"
            "th { background: #f1f5f9; color: #0f172a; }"
            "a { color: #2563eb; text-decoration: none; }"
            "a:hover { text-decoration: underline; }"
        );
    }
    return QString(
        "body { color: #d0d7de; }"
        "p { margin: 0 0 10px 0; line-height: 1.7; }"
        "h1, h2, h3, h4 { color: #f0f6fc; margin: 12px 0 8px 0; font-weight: 700; }"
        "h1 { font-size: 18px; border-bottom: 1px solid #30363d; padding-bottom: 6px; }"
        "h2 { font-size: 16px; border-bottom: 1px solid #2d333b; padding-bottom: 4px; }"
        "h3 { font-size: 14px; }"
        "ul, ol { margin: 6px 0 12px 22px; }"
        "li { margin: 0 0 6px 0; line-height: 1.6; }"
        "blockquote { margin: 10px 0; padding: 8px 12px; border-left: 3px solid #58a6ff; background: #0f1723; color: #9fb4c8; }"
        "code { background: #111927; color: #c3e0ff; border: 1px solid #233043; border-radius: 4px; padding: 1px 5px; font-family: 'JetBrains Mono', 'Consolas', monospace; font-size: 12px; }"
        "pre { margin: 12px 0; padding: 12px; background: #0b1220; border: 1px solid #2b3b52; border-radius: 8px; }"
        "pre code { background: transparent; border: none; padding: 0; color: #c9d1d9; font-size: 12px; }"
        "table { border-collapse: collapse; margin: 10px 0; width: 100%; }"
        "th, td { border: 1px solid #30363d; padding: 6px 8px; text-align: left; }"
        "th { background: #161b22; color: #f0f6fc; }"
        "a { color: #58a6ff; text-decoration: none; }"
        "a:hover { text-decoration: underline; }"
    );
}

QString wrapModelCard(const QString& bodyHtml) {
    bool isLight = ThemeManager::theme() && ThemeManager::theme()->type() == PCBTheme::Light;
    const QString ts = nowTimeChip();
    if (isLight) {
        return QString(
            "<div style='display:block; width:100%;'>"
            "<div style='"
            "background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffffff, stop:1 #f8fafc);"
            "color: #1e293b;"
            "padding: 14px 16px;"
            "border-radius: 12px;"
            "border: 1px solid #e2e8f0;"
            "margin: 6px 0 12px 0;"
            "max-width: 92%;"
            "box-shadow: 0 4px 12px rgba(0,0,0,0.08);"
            "'>"
            "<div style='font-size:10px; color:#64748b; margin-bottom:7px; font-weight:700; text-transform:uppercase; letter-spacing:0.5px;'>Viora AI · %2</div>"
            "%1</div>"
            "<div style='height:8px;'></div>"
            "</div>"
        ).arg(bodyHtml, ts);
    }
    return QString(
        "<div style='display:block; width:100%;'>"
        "<div style='"
        "background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #1a2333, stop:1 #0f1520);"
        "color: #e6edf3;"
        "padding: 16px;"
        "border-radius: 12px;"
        "border: 1px solid #30405a;"
        "margin: 6px 0 12px 0;"
        "max-width: 92%;"
        "box-shadow: 0 8px 24px rgba(0,0,0,0.3);"
        "'>"
        "<div style='font-size:10px; color:#9fb0c8; margin-bottom:7px; font-weight:700; text-transform:uppercase; letter-spacing:0.5px;'>Viora AI · %2</div>"
        "%1</div>"
        "<div style='height:8px;'></div>"
        "</div>"
    ).arg(bodyHtml, ts);
}

QString wrapUserCard(const QString& textHtml, const QString& headerHtml = QString()) {
    bool isLight = ThemeManager::theme() && ThemeManager::theme()->type() == PCBTheme::Light;
    const QString ts = nowTimeChip();
    if (isLight) {
        return QString(
            "<div style='display:block; width:100%; margin: 6px 0 12px 0; text-align: right;'>"
            "<div class='user-card' style='"
            "background: #f1f5f9;"
            "color: #334155;"
            "padding: 10px 12px;"
            "border-radius: 14px;"
            "border: 1px solid #e2e8f0;"
            "display: inline-block;"
            "max-width: 84%;"
            "text-align: left;"
            "'>"
            "%2"
            "<div style='font-size:10px; color:#64748b; margin: 0 0 6px 0; font-weight:600; text-align:right;'>You · %3</div>"
            "<div style='line-height: 1.55;'>%1</div>"
            "</div>"
            "</div>"
            "<div style='height:8px;'></div>"
        ).arg(textHtml, headerHtml, ts);
    }
    return QString(
        "<div style='display:block; width:100%; margin: 6px 0 12px 0; text-align: right;'>"
        "<div class='user-card' style='"
        "background: #182131;"
        "color: #e6edf3;"
        "padding: 10px 12px;"
        "border-radius: 14px;"
        "border: 1px solid #30405a;"
        "display: inline-block;"
        "max-width: 84%;"
        "text-align: left;"
        "'>"
        "%2"
        "<div style='font-size:10px; color:#9fb0c8; margin: 0 0 6px 0; font-weight:600; text-align:right;'>You · %3</div>"
        "<div style='line-height: 1.55;'>%1</div>"
        "</div>"
        "</div>"
        "<div style='height:8px;'></div>"
    ).arg(textHtml, headerHtml, ts);
}
} // namespace

class SyntaxHighlighter : public QSyntaxHighlighter {
public:
    SyntaxHighlighter(QTextDocument* parent) : QSyntaxHighlighter(parent) {
        HighlightingRule rule;
        bool isLight = ThemeManager::theme() && ThemeManager::theme()->type() == PCBTheme::Light;
        
        keywordFormat.setForeground(QColor(isLight ? "#0550ae" : "#569cd6"));
        keywordFormat.setFontWeight(QFont::Bold);
        QStringList keywords = {
            "def", "class", "if", "else", "elif", "for", "while", "return", "import", "from",
            "True", "False", "None", "self", "net", "component", "and", "or", "not", "in", "is", "lambda"
        };
        for (const QString& keyword : keywords) {
            rule.pattern = QRegularExpression("\\b" + keyword + "\\b");
            rule.format = keywordFormat;
            highlightingRules.append(rule);
        }
        functionFormat.setForeground(QColor(isLight ? "#8250df" : "#dcdcaa"));
        rule.pattern = QRegularExpression("\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\()");
        rule.format = functionFormat;
        highlightingRules.append(rule);
        stringFormat.setForeground(QColor(isLight ? "#0a3069" : "#ce9178"));
        rule.pattern = QRegularExpression("\".*?\"");
        rule.format = stringFormat;
        highlightingRules.append(rule);
        rule.pattern = QRegularExpression("'.*?'");
        highlightingRules.append(rule);
        commentFormat.setForeground(QColor(isLight ? "#6e7781" : "#6a9955"));
        rule.pattern = QRegularExpression("#[^\\n]*");
        rule.format = commentFormat;
        highlightingRules.append(rule);
        numberFormat.setForeground(QColor(isLight ? "#0550ae" : "#b5cea8"));
        rule.pattern = QRegularExpression("\\b[0-9]+(\\.[0-9]+)?\\b");
        rule.format = numberFormat;
        highlightingRules.append(rule);
    }
protected:
    void highlightBlock(const QString& text) override {
        for (const HighlightingRule& rule : highlightingRules) {
            QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
            while (it.hasNext()) {
                QRegularExpressionMatch match = it.next();
                setFormat(match.capturedStart(), match.capturedLength(), rule.format);
            }
        }
    }
private:
    struct HighlightingRule { QRegularExpression pattern; QTextCharFormat format; };
    QList<HighlightingRule> highlightingRules;
    QTextCharFormat keywordFormat, functionFormat, stringFormat, commentFormat, numberFormat;
};

GeminiPanel::GeminiPanel(QGraphicsScene* scene, QWidget* parent) 
    : QWidget(parent), m_scene(scene) {
    
    PCBTheme* theme = ThemeManager::theme();
    QString bg_main = theme ? theme->panelBackground().name() : "#ffffff";
    QString fg = theme ? theme->textColor().name() : "#000000";
    QString border = theme ? theme->panelBorder().name() : "#cccccc";
    QString headerBg = (theme && theme->type() == PCBTheme::Light) ? "#f8fafc" : "#161b22";
    
    // Modern Glassy Sidebar Look
    QString glassBg = (theme && theme->type() == PCBTheme::Light) 
        ? "rgba(255, 255, 255, 0.9)" 
        : "rgba(24, 24, 27, 0.85)";
    
    setStyleSheet(QString("QWidget { background-color: %1; color: %2; }").arg(glassBg, fg));
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // API Key Container
    m_apiKeyContainer = new QWidget(this);
    m_apiKeyContainer->setStyleSheet(QString("background-color: %1; border-bottom: 1px solid %2;").arg(headerBg, border));
    m_apiKeyContainer->setFixedHeight(44);
    QHBoxLayout* keyLayout = new QHBoxLayout(m_apiKeyContainer);
    keyLayout->setContentsMargins(10, 0, 10, 0);
    keyLayout->setSpacing(8);
    keyLayout->setAlignment(Qt::AlignVCenter);

    m_apiKeyField = new QLineEdit(m_apiKeyContainer);
    m_apiKeyField->setEchoMode(QLineEdit::Password);
    m_apiKeyField->setPlaceholderText("API Key...");
    m_apiKeyField->setFixedHeight(30);
    m_apiKeyField->setStyleSheet(QString("QLineEdit { background: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 0 8px; }")
        .arg(bg_main, fg, border));
    
    m_saveKeyButton = new QPushButton("SAVE", m_apiKeyContainer);
    m_saveKeyButton->setFixedHeight(30);
    m_saveKeyButton->setStyleSheet("QPushButton { background: #238636; color: white; border-radius: 6px; padding: 0 12px; font-weight: bold; font-size: 11px; } QPushButton:hover { background: #2ea043; }");
    connect(m_saveKeyButton, &QPushButton::clicked, this, &GeminiPanel::onSaveKeyClicked);
    
    QLabel* al = new QLabel("API KEY:", m_apiKeyContainer); 
    al->setStyleSheet(QString("color: %1; font-weight: bold; font-size: 10px;").arg(theme ? theme->textSecondary().name() : "#888"));
    
    keyLayout->addWidget(al);
    keyLayout->addWidget(m_apiKeyField);
    keyLayout->addWidget(m_saveKeyButton);
    keyLayout->addStretch();
    mainLayout->addWidget(m_apiKeyContainer);

    // Header Toolbar
    QWidget* header = new QWidget(this);
    header->setStyleSheet(QString("background-color: %1; border-bottom: 1px solid %2;").arg(headerBg, border));
    header->setFixedHeight(40);
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(8, 0, 8, 0);
    headerLayout->setSpacing(4);
    headerLayout->setAlignment(Qt::AlignVCenter);

    const QString topIconStyle = QString(
        "QPushButton {"
        " border: none;"
        " background: transparent;"
        " color: %1;"
        " border-radius: 4px;"
        " padding: 2px;"
        "}"
        "QPushButton:hover { background: %2; }"
        "QPushButton:pressed { background: %3; }")
        .arg(fg)
        .arg((theme && theme->type() == PCBTheme::Light) ? "#e2e8f0" : "#2a3444")
        .arg((theme && theme->type() == PCBTheme::Light) ? "#cbd5e1" : "#1f2937");
    
    m_clearButton = new QPushButton(this);
    m_clearButton->setText("+");
    m_clearButton->setToolTip("New Chat");
    m_clearButton->setFixedSize(24, 24);
    m_clearButton->setStyleSheet(topIconStyle +
        "QPushButton { font-size: 16px; font-weight: 700; }");
    connect(m_clearButton, &QPushButton::clicked, this, &GeminiPanel::clearHistory);

    m_copyButton = new QPushButton(this);
    m_copyButton->setIcon(QIcon(":/icons/tool_duplicate.svg"));
    m_copyButton->setToolTip("Copy Code");
    m_copyButton->setEnabled(false);
    m_copyButton->setFixedSize(24, 24);
    m_copyButton->setStyleSheet(topIconStyle);
    m_copyButton->hide();
    connect(m_copyButton, &QPushButton::clicked, this, &GeminiPanel::onCopyClicked);

    auto getThemeIcon = [&](const QString& path) {
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

    m_historyMenuButton = new QPushButton(this);
    m_historyMenuButton->setIcon(getThemeIcon(":/icons/undo.svg"));
    m_historyMenuButton->setIconSize(QSize(14, 14));
    m_historyMenuButton->setToolTip("Chat History");
    m_historyMenuButton->setFixedSize(24, 24);
    m_historyMenuButton->setStyleSheet(topIconStyle);
    connect(m_historyMenuButton, &QPushButton::clicked, this, [this](){
        if (m_projectFilePath.isEmpty()) return;
        QFileInfo info(m_projectFilePath);
        QString hDir = info.absolutePath() + "/.gemini/history";
        QDir dir(hDir);
        if (!dir.exists()) return;
        
        PCBTheme* theme = ThemeManager::theme();
        QMenu menu(this);
        if (theme) {
            menu.setStyleSheet(QString("QMenu { background: %1; color: %2; border: 1px solid %3; } QMenu::item:selected { background: %4; color: white; }")
                .arg(theme->panelBackground().name(), theme->textColor().name(), theme->panelBorder().name(), theme->accentColor().name()));
        }
        for (const QString& f : dir.entryList({"*.md"}, QDir::Files)) {
            QAction* act = menu.addAction(f);
            connect(act, &QAction::triggered, this, [this, hDir, f](){
                loadHistoryFromFile(hDir + "/" + f);
            });
        }
        if (menu.actions().isEmpty()) menu.addAction("No history found");
        menu.exec(m_historyMenuButton->mapToGlobal(QPoint(0, m_historyMenuButton->height())));
    });

    QPushButton* moreButton = new QPushButton(this);
    moreButton->setIcon(getThemeIcon(":/icons/tool_gear.svg"));
    moreButton->setIconSize(QSize(14, 14));
    moreButton->setToolTip("More");
    moreButton->setFixedSize(24, 24);
    moreButton->setStyleSheet(topIconStyle);
    connect(moreButton, &QPushButton::clicked, this, [this, moreButton]() {
        PCBTheme* theme = ThemeManager::theme();
        QMenu menu(this);
        if (theme) {
            menu.setStyleSheet(QString("QMenu { background: %1; color: %2; border: 1px solid %3; } QMenu::item:selected { background: %4; color: white; }")
                .arg(theme->panelBackground().name(), theme->textColor().name(), theme->panelBorder().name(), theme->accentColor().name()));
        }
        QAction* copyAct = menu.addAction(QIcon(":/icons/tool_duplicate.svg"), "Copy Code");
        copyAct->setEnabled(!m_lastGeneratedCode.isEmpty());
        connect(copyAct, &QAction::triggered, this, &GeminiPanel::onCopyClicked);
        QAction* refreshAct = menu.addAction(QIcon(":/icons/toolbar_refresh.png"), "Refresh Models");
        connect(refreshAct, &QAction::triggered, this, &GeminiPanel::onRefreshModelsClicked);
        QAction* thinkingAct = menu.addAction("Toggle Thinking Tray");
        thinkingAct->setCheckable(true);
        thinkingAct->setChecked(m_statusButton->isChecked());
        connect(thinkingAct, &QAction::triggered, this, [this](bool checked) {
            m_statusButton->setChecked(checked);
        });
        menu.exec(moreButton->mapToGlobal(QPoint(0, moreButton->height())));
    });

    QPushButton* closeButton = new QPushButton(this);
    closeButton->setIcon(getThemeIcon(":/icons/tool_clear.svg"));
    closeButton->setIconSize(QSize(14, 14));
    closeButton->setToolTip("Close AI Panel");
    closeButton->setFixedSize(24, 24);
    closeButton->setStyleSheet(topIconStyle);
    connect(closeButton, &QPushButton::clicked, this, [this]() {
        QWidget* w = this;
        while (w) {
            if (auto* dock = qobject_cast<QDockWidget*>(w)) {
                dock->hide();
                return;
            }
            w = w->parentWidget();
        }
        this->hide();
    });

    headerLayout->addWidget(m_clearButton);
    headerLayout->addWidget(m_historyMenuButton);
    headerLayout->addWidget(moreButton);
    headerLayout->addWidget(closeButton);
    headerLayout->addStretch(1);
    
    m_statusButton = new QPushButton("IDLE", this);
    m_statusButton->setCheckable(true);
    m_statusButton->setFixedHeight(28);
    m_statusButton->setMaximumWidth(0);
    m_statusButton->setMinimumWidth(0);
    if (theme) {
        m_statusButton->setStyleSheet(QString("QPushButton { color: %1; font-weight: bold; font-size: 10px; border: 1px solid %2; border-radius: 6px; padding: 0 12px; background: %3; } QPushButton:checked { background: %1; color: %3; }")
            .arg(theme->accentColor().name(), border, bg_main));
    }
    connect(m_statusButton, &QPushButton::toggled, this, [this](bool checked){ m_thinkingDisplay->setVisible(checked); });
    m_statusButton->hide();
    mainLayout->addWidget(header);

    m_errorBanner = new QWidget(this);
    m_errorBanner->setStyleSheet("background: #fee2e2; border-bottom: 1px solid #ef4444;");
    QHBoxLayout* errorLayout = new QHBoxLayout(m_errorBanner);
    errorLayout->setContentsMargins(10, 6, 10, 6);
    errorLayout->setAlignment(Qt::AlignVCenter);
    m_errorLabel = new QLabel(m_errorBanner);
    m_errorLabel->setStyleSheet("color: #b91c1c; font-size: 12px; font-weight: bold;");
    m_errorLabel->setWordWrap(true);
    m_errorDetailsButton = new QPushButton("DETAILS", m_errorBanner);
    m_errorDetailsButton->setFixedHeight(24);
    m_errorDetailsButton->setStyleSheet("QPushButton { background: #ffffff; color: #991b1b; border: 1px solid #ef4444; border-radius: 6px; padding: 0 10px; font-weight: bold; font-size: 10px; }");
    connect(m_errorDetailsButton, &QPushButton::clicked, this, &GeminiPanel::onViewErrorDetailsClicked);
    m_errorDismissButton = new QPushButton("DISMISS", m_errorBanner);
    m_errorDismissButton->setFixedHeight(24);
    m_errorDismissButton->setStyleSheet("QPushButton { background: #ef4444; color: #fff; border-radius: 6px; padding: 0 10px; font-weight: bold; font-size: 10px; }");
    connect(m_errorDismissButton, &QPushButton::clicked, this, &GeminiPanel::onDismissErrorClicked);
    errorLayout->addWidget(m_errorLabel, 1);
    errorLayout->addWidget(m_errorDetailsButton);
    errorLayout->addWidget(m_errorDismissButton);
    m_errorBanner->hide();
    mainLayout->addWidget(m_errorBanner);

    // Chat Area
    m_chatArea = new QTextBrowser(this);
    m_chatArea->setReadOnly(true);
    m_chatArea->setOpenExternalLinks(false);
    connect(m_chatArea, &QTextBrowser::anchorClicked, this, &GeminiPanel::onAnchorClicked);
    m_chatArea->document()->setDefaultStyleSheet(
        "a.undo-link {"
        "  opacity: 0.0;"
        "}"
        ".user-card:hover a.undo-link {"
        "  opacity: 1.0;"
        "}"
    );
    m_chatArea->setStyleSheet(QString(
        "QTextBrowser {"
        " background-color: transparent;"
        " color: %2;"
        " border: none;"
        " padding: 16px;"
        " font-family: 'Inter', 'Segoe UI', sans-serif;"
        " font-size: 13px;"
        " line-height: 1.6;"
        "}"
    ).arg(bg_main, fg));
    m_highlighter = new SyntaxHighlighter(m_chatArea->document());
    mainLayout->addWidget(m_chatArea, 1);

    // Thinking Tray
    m_thinkingDisplay = new QTextEdit(this);
    m_thinkingDisplay->setReadOnly(true);
    m_thinkingDisplay->setFixedHeight(160);
    m_thinkingDisplay->setStyleSheet(QString("QTextEdit { background-color: %1; color: %2; border-top: 1px solid %3; padding: 12px; font-style: italic; font-size: 12px; line-height: 1.4; }")
        .arg(headerBg, theme ? theme->textSecondary().name() : "#888", border));
    m_thinkingDisplay->hide();
    mainLayout->addWidget(m_thinkingDisplay);

    // Footer Input
    QWidget* footer = new QWidget(this);
    QString footerBg = (theme && theme->type() == PCBTheme::Light) ? "#f1f5f9" : "#0d1117";
    footer->setStyleSheet(QString("background-color: %1; border-top: 1px solid %2;").arg(footerBg, border));
    footer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    QVBoxLayout* footerLayout = new QVBoxLayout(footer);
    footerLayout->setContentsMargins(12, 12, 12, 12);
    footerLayout->setSizeConstraint(QLayout::SetMinimumSize);

    QWidget* composer = new QWidget(this);
    composer->setStyleSheet(QString("QWidget { background: %1; border: 1px solid %2; border-radius: 12px; }").arg(bg_main, border));
    composer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    QVBoxLayout* composerLayout = new QVBoxLayout(composer);
    composerLayout->setContentsMargins(12, 10, 12, 10);
    composerLayout->setSpacing(4);

    m_includeContextCheck = new QCheckBox(this);
    m_includeContextCheck->setChecked(true);
    m_includeContextCheck->hide();
    m_includeScreenshotCheck = new QCheckBox(this);
    m_includeScreenshotCheck->hide();

    m_toolCallBanner = new QWidget(this);
    m_toolCallBanner->setStyleSheet(
        "QWidget {"
        " background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #0f223d, stop:1 #1a2f50);"
        " border: 1px solid #335f93;"
        " border-radius: 8px;"
        "}"
    );
    QVBoxLayout* toolBannerLayout = new QVBoxLayout(m_toolCallBanner);
    toolBannerLayout->setContentsMargins(10, 8, 10, 8);
    toolBannerLayout->setSpacing(2);
    m_toolCallTitle = new QLabel("Running tools...", m_toolCallBanner);
    m_toolCallTitle->setStyleSheet("color: #dbeafe; font-size: 12px; font-weight: 700; border: none; background: transparent;");
    m_toolCallSubtitle = new QLabel("Executing assistant tools.", m_toolCallBanner);
    m_toolCallSubtitle->setWordWrap(true);
    m_toolCallSubtitle->setStyleSheet("color: #93c5fd; font-size: 11px; border: none; background: transparent;");
    toolBannerLayout->addWidget(m_toolCallTitle);
    toolBannerLayout->addWidget(m_toolCallSubtitle);
    m_toolCallBanner->hide();
    composerLayout->addWidget(m_toolCallBanner);

    m_inputField = new QTextEdit(this);
    m_inputField->setPlaceholderText("Message Viora AI...  (Enter = send, Shift+Enter = new line)");
    m_inputField->setFixedHeight(40);
    m_inputField->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_inputField->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_inputField->setAcceptRichText(false);
    m_inputField->setStyleSheet(QString(
        "QTextEdit { background: transparent; color: %1; border: none; padding: 2px 0; font-size: 13px; }"
        "QTextEdit:disabled { color: %2; }")
        .arg(fg, theme ? theme->textSecondary().name() : "#888"));
    m_inputField->installEventFilter(this);
    connect(m_inputField, &QTextEdit::textChanged, this, &GeminiPanel::updateSendEnabled);
    composerLayout->addWidget(m_inputField);

    QHBoxLayout* toolsRow = new QHBoxLayout();
    toolsRow->setContentsMargins(0, 0, 0, 0);
    toolsRow->setSpacing(6);
    toolsRow->setAlignment(Qt::AlignVCenter);

    QString toolBtnStyle = QString("QPushButton { background: %1; color: %2; border: 1px solid %3; border-radius: 6px; font-size: 10px; font-weight: bold; padding: 0 8px; } QPushButton:hover { background: %4; border-color: %5; }")
        .arg((theme && theme->type() == PCBTheme::Light) ? "#f8fafc" : "#21262d")
        .arg(fg, border)
        .arg((theme && theme->type() == PCBTheme::Light) ? "#f1f5f9" : "#30363d")
        .arg(theme ? theme->textSecondary().name() : "#888");

    m_refreshModelsButton = new QPushButton("REFRESH", this);
    m_refreshModelsButton->setFixedHeight(24);
    m_refreshModelsButton->setStyleSheet(toolBtnStyle);
    connect(m_refreshModelsButton, &QPushButton::clicked, this, &GeminiPanel::onRefreshModelsClicked);

    QString comboStyle = QString("QComboBox { background: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 0 16px 0 6px; font-size: 10px; font-weight: bold; } QComboBox::drop-down { border: none; width: 16px; } QComboBox QAbstractItemView { background: %4; color: %2; border: 1px solid %3; selection-background-color: %5; }")
        .arg((theme && theme->type() == PCBTheme::Light) ? "#f8fafc" : "#21262d")
        .arg(fg, border, bg_main)
        .arg((theme && theme->type() == PCBTheme::Light) ? "#eff6ff" : "#30363d");

    QComboBox* speedCombo = new QComboBox(this);
    speedCombo->addItem("PLAN", "schematic");
    speedCombo->addItem("DIRECT", "schematic");
    speedCombo->addItem("ASK", "ask");
    speedCombo->setCurrentIndex(0);
    speedCombo->setFixedHeight(24);
    speedCombo->setStyleSheet(comboStyle);
    
    connect(speedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, speedCombo](int index) {
        QString mode = speedCombo->itemData(index).toString();
        this->setMode(mode);
    });

    m_modelCombo = new QComboBox(this);
    m_modelCombo->setMinimumWidth(110);
    m_modelCombo->setFixedHeight(24);
    m_modelCombo->setStyleSheet(comboStyle);
    m_modelCombo->addItem("Gemini 3.1 Flash Lite Preview", "gemini-3.1-flash-lite-preview");

    QPushButton* micButton = new QPushButton("VOICE", this);
    micButton->setFixedHeight(24);
    micButton->setStyleSheet(QString("QPushButton { background: transparent; color: %1; border: none; font-size: 10px; font-weight: bold; padding: 0 4px; } QPushButton:hover { color: %2; }")
        .arg(theme ? theme->textSecondary().name() : "#888", fg));

    m_sendButton = new QPushButton("SEND", this);
    m_sendButton->setFixedHeight(24);
    m_sendButton->setStyleSheet(QString("QPushButton { background: %1; color: white; border: 1px solid %2; border-radius: 6px; font-weight: bold; font-size: 10px; padding: 0 12px; } QPushButton:hover { background: %3; }")
        .arg("#238636", "#2ea043", "#2ea043"));
    connect(m_sendButton, &QPushButton::clicked, this, &GeminiPanel::onSendClicked);
    m_sendButton->setEnabled(false);

    m_stopButton = new QPushButton("STOP", this);
    m_stopButton->setFixedHeight(24);
    m_stopButton->hide();
    m_stopButton->setStyleSheet("QPushButton { background: #ef4444; color: white; border: 1px solid #ef4444; border-radius: 6px; font-weight: bold; font-size: 10px; padding: 0 12px; } QPushButton:hover { background: #dc2626; }");
    connect(m_stopButton, &QPushButton::clicked, this, &GeminiPanel::onStopClicked);

    toolsRow->addWidget(m_refreshModelsButton);
    toolsRow->addWidget(speedCombo);
    toolsRow->addWidget(m_modelCombo);
    toolsRow->addStretch();
    toolsRow->addWidget(micButton);
    toolsRow->addWidget(m_sendButton);
    toolsRow->addWidget(m_stopButton);
    composerLayout->addLayout(toolsRow);

    footerLayout->addWidget(composer);
    mainLayout->addWidget(footer, 0);

    m_thinkingPulseTimer = new QTimer(this);
    m_thinkingPulseTimer->setInterval(500);
    connect(m_thinkingPulseTimer, &QTimer::timeout, this, &GeminiPanel::updateThinkingPulse);
    updateApiKeyVisibility();
    updateSendEnabled();
    if (!ConfigManager::instance().geminiApiKey().isEmpty()) {
        QTimer::singleShot(0, this, &GeminiPanel::refreshModelList);
    }
}

void GeminiPanel::setMode(const QString& m) { m_mode = m; }

void GeminiPanel::setProjectFilePath(const QString& path) {
    if (m_projectFilePath == path) return;
    m_projectFilePath = path;
    loadHistory();
}

void GeminiPanel::saveHistory() {
    if (m_projectFilePath.isEmpty() || m_history.isEmpty()) return;

    QFileInfo info(m_projectFilePath);
    QString historyDir = info.absolutePath() + "/.gemini/history";
    QDir().mkpath(historyDir);
    QDir dir(historyDir);

    // Generate smart title once and ensure it does not overwrite an existing chat file.
    if (m_currentChatTitle.isEmpty()) {
        QString baseTitle;
        for (const auto& msg : m_history) {
            if (msg["role"].toString() == "user") {
                QString firstPrompt = msg["text"].toString();
                // Sanitize and truncate for filename
                baseTitle = firstPrompt.left(30).toLower();
                baseTitle.replace(QRegularExpression("[^a-z0-9]"), "_");
                baseTitle.replace(QRegularExpression("_+"), "_");
                if (baseTitle.startsWith("_")) baseTitle = baseTitle.mid(1);
                if (baseTitle.endsWith("_")) baseTitle = baseTitle.left(baseTitle.length() - 1);
                if (baseTitle.isEmpty()) baseTitle = "chat_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmm");
                break;
            }
        }
        if (baseTitle.isEmpty()) {
            baseTitle = "chat_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmm");
        }

        QString candidate = baseTitle;
        int prefix = 2;
        while (dir.exists(candidate + ".md")) {
            candidate = QString::number(prefix) + "_" + baseTitle;
            ++prefix;
        }
        m_currentChatTitle = candidate;
    }

    QString historyFile = historyDir + "/" + m_currentChatTitle + ".md";
    QFile file(historyFile);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "<!-- TITLE: " << m_currentChatTitle << " -->\n";
        out << "# viospice AI - " << m_currentChatTitle << "\n\n";
        for (const auto& msg : m_history) {
            QString role = msg["role"].toString();
            QString text = msg["text"].toString();
            out << "## " << (role == "user" ? "User" : "Co-Pilot") << "\n" << text << "\n\n---\n\n";
        }
    }
}
void GeminiPanel::loadHistory() {
    if (m_projectFilePath.isEmpty()) return;
    QFileInfo info(m_projectFilePath);
    QString historyFile = info.absolutePath() + "/.gemini/history/" + info.baseName() + ".md";
    loadHistoryFromFile(historyFile);
}

void GeminiPanel::loadHistoryFromFile(const QString& filePath) {
    if (filePath.isEmpty() || !QFile::exists(filePath)) return;

    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_history.clear(); 
        clearSuggestionButtons();
        m_chatArea->clear();
        m_currentChatTitle = QFileInfo(filePath).baseName();

        QTextStream in(&file);
        QString content = in.readAll();

        // Extract title metadata if exists
        QRegularExpression titleRegex("<!-- TITLE: (.*?) -->");
        auto titleMatch = titleRegex.match(content);
        if (titleMatch.hasMatch()) {
            m_currentChatTitle = titleMatch.captured(1);
        }

        QRegularExpression entryRegex(R"(##\s*(User|Co-Pilot)\s*\n([\s\S]*?)(?=\n##\s*(?:User|Co-Pilot)\s*\n|\z))");
        QRegularExpressionMatchIterator it = entryRegex.globalMatch(content);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            const QString roleHeader = match.captured(1).trimmed();
            const QString text = match.captured(2).trimmed();
            if (text.isEmpty()) continue;

            const QString roleStr = roleHeader.compare("User", Qt::CaseInsensitive) == 0 ? "user" : "model";
            QVariantMap msg; msg["role"] = roleStr; msg["text"] = text; m_history.append(msg);

            if (roleStr == "user") {
                m_chatArea->insertHtml(wrapUserCard(text.toHtmlEscaped()));
            } else {
                QTextDocument doc;
                doc.setDefaultStyleSheet(markdownDocStyleSheet());
                doc.setMarkdown(text);
                m_chatArea->insertHtml(wrapModelCard(doc.toHtml()));
            }

            m_chatArea->insertHtml("<div style='height: 4px;'></div>");
        }
        m_chatArea->verticalScrollBar()->setValue(m_chatArea->verticalScrollBar()->maximum());
    }
}
void GeminiPanel::updateApiKeyVisibility() {
    const bool hasKey = !ConfigManager::instance().geminiApiKey().isEmpty();
    m_apiKeyContainer->setVisible(!hasKey);
    if (m_saveKeyButton) m_saveKeyButton->setVisible(!hasKey);
    if (m_modelCombo) m_modelCombo->setEnabled(hasKey);
    if (m_refreshModelsButton) m_refreshModelsButton->setEnabled(hasKey);
    if (hasKey) {
        m_apiKeyField->clear();
        m_apiKeyField->setPlaceholderText("API Key saved");
    } else {
        m_apiKeyField->setPlaceholderText("API Key...");
    }
}
void GeminiPanel::onSaveKeyClicked() {
    QString k = m_apiKeyField->text().trimmed();
    if (!k.isEmpty()) {
        ConfigManager::instance().setGeminiApiKey(k);
        updateApiKeyVisibility();
        refreshModelList();
    }
}
void GeminiPanel::setUndoStack(QUndoStack* stack) {
    m_undoStack = stack;
    qDebug() << "Gemini AI: Undo stack set to" << stack << (stack ? " (Valid)" : " (NULL!)");
}

bool GeminiPanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_inputField && event && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        const bool enterPressed = (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter);
        if (enterPressed) {
            const Qt::KeyboardModifiers mods = keyEvent->modifiers();
            const bool wantsNewline = mods.testFlag(Qt::ShiftModifier);
            if (!wantsNewline) {
                onSendClicked();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void GeminiPanel::updateSendEnabled() {
    if (!m_sendButton || !m_inputField) return;
    const bool hasText = !m_inputField->toPlainText().trimmed().isEmpty();
    m_sendButton->setEnabled(!m_isWorking && hasText);
}

void GeminiPanel::onSendClicked() {
    const QString t = m_inputField->toPlainText().trimmed();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (!t.isEmpty() && t == m_lastSubmittedPrompt && (nowMs - m_lastSubmitEpochMs) < 1200) {
        updateSendEnabled();
        return;
    }
    if (!t.isEmpty()) {
        m_lastSubmittedPrompt = t;
        m_lastSubmitEpochMs = nowMs;
        m_inputField->clear();
        askPrompt(t, m_includeContextCheck->isChecked());
    }
    updateSendEnabled();
}
void GeminiPanel::clearHistory() {
    if (m_process) {
        m_process->disconnect(this);
        if (m_process->state() != QProcess::NotRunning) {
            m_process->kill();
        }
        m_process->deleteLater();
        m_process = nullptr;
    }

    m_history.clear(); 
    clearSuggestionButtons();
    m_chatArea->clear();
    m_currentChatTitle.clear();
    m_leftover.clear();
    m_responseBuffer.clear();
    m_thinkingBuffer.clear();
    m_errorBuffer.clear();
    m_responseStartPos = 0;
    m_isWorking = false;
    m_errorHistory.clear();
    if (m_errorHistoryList) m_errorHistoryList->clear();
    if (m_errorSummaryView) m_errorSummaryView->clear();
    if (m_errorRawView) m_errorRawView->clear();
    m_thinkingPulseTimer->stop();
    if (m_statusButton) m_statusButton->hide();
    hideToolCallBanner();
    if (m_thinkingDisplay) m_thinkingDisplay->clear();
    if (m_inputField) m_inputField->setEnabled(true);
    if (m_stopButton) m_stopButton->hide();
    if (m_sendButton) m_sendButton->show();

    hideErrorBanner();
    m_chatArea->append("<div style='color: #8b949e; font-style: italic; margin-top: 10px;'>[SYSTEM] New conversation started.</div>");
}

void GeminiPanel::askPrompt(const QString& text, bool includeContext) {
    if (m_isWorking) return;
    clearSuggestionButtons();
    QString key = ConfigManager::instance().geminiApiKey();
    if (key.isEmpty()) {
        reportError("API Key Missing",
                    "Gemini API key is not configured.\nOpen the AI panel key field, paste your key, then press SAVE.",
                    true);
        m_chatArea->append("<div style='color: #f85149; background: #161b22; padding: 10px; border-radius: 6px; border: 1px solid #30363d;'><b>API Key Missing</b></div>");
        m_apiKeyContainer->show(); return;
    }
    // Capture checkpoint
    int undoIndex = m_undoStack ? m_undoStack->index() : -1;
    qDebug() << "Gemini AI: Snapshot index=" << undoIndex << "stack=" << m_undoStack;

    QString checkpointIconHtml;
    if (undoIndex != -1) {
        checkpointIconHtml = QString(
            "<a class='undo-link' href='checkpoint:%1' title='Retrieve changes from this point' "
            "style='display:inline-block; min-width:20px; height:18px; line-height:18px; text-align:center; "
            "border-radius:9px; border:1px solid #3a4558; background:#1f2a3a; color:#dce7f6; text-decoration:none; font-size:9px; font-weight:600; padding:0 6px;'>"
            "RET</a>"
        ).arg(undoIndex);
    }
    QString checkpointHeaderHtml;
    if (!checkpointIconHtml.isEmpty()) {
        checkpointHeaderHtml = QString(
            "<div style='width:100%; text-align:right; margin: 0 0 4px 0;'>%1</div>"
        ).arg(checkpointIconHtml);
    }

    // Force clear position and insert
    m_chatArea->moveCursor(QTextCursor::End);
    m_chatArea->insertHtml(wrapUserCard(text.toHtmlEscaped(), checkpointHeaderHtml));

    m_chatArea->insertHtml("<div style='height: 6px;'></div>");
    m_chatArea->verticalScrollBar()->setValue(m_chatArea->verticalScrollBar()->maximum());
    
    // Capture the start position for streaming response AFTER prompt is in
    m_chatArea->moveCursor(QTextCursor::End); 
    m_responseStartPos = m_chatArea->textCursor().position();

    QVariantMap msg; msg["role"] = "user"; msg["text"] = text; m_history.append(msg);
    QStringList args; args << text;

    if (m_includeScreenshotCheck->isChecked() && m_scene) {
        QRectF r = m_scene->itemsBoundingRect(); if (r.isEmpty()) r = QRectF(0,0,800,600);
        QImage img(r.size().toSize(), QImage::Format_ARGB32); img.fill(Qt::black);
        QPainter p(&img); p.translate(-r.topLeft()); m_scene->render(&p, QRectF(), r); p.end();
        QByteArray ba; QBuffer buf(&ba); buf.open(QIODevice::WriteOnly); img.save(&buf, "PNG");
        args << "--image" << QString::fromLatin1(ba.toBase64());
        m_chatArea->append("<div style='color: #8b949e; font-size: 10px; text-align: right; margin: 0 0 10px 0;'>[SYSTEM] Viewport snapshot attached</div>");
    }
    if (includeContext && m_scene) {
        QJsonObject ctx = SchematicFileIO::serializeSceneToJson(m_scene);
        QString fs = SchematicFileIO::convertToFluxScript(m_scene, m_netManager); 
        if (!fs.isEmpty()) ctx["fluxscript"] = fs;
        args << "--context" << QString::fromUtf8(QJsonDocument(ctx).toJson(QJsonDocument::Compact));
        m_chatArea->append("<div style='color: #8b949e; font-size: 10px; text-align: right; margin: 0 0 14px 0;'>[SYSTEM] Context attached</div>");
    }
    if (!m_history.isEmpty()) { QJsonArray ha; for (const auto& m : m_history) ha.append(QJsonObject::fromVariantMap(m)); args << "--history" << QJsonDocument(ha).toJson(QJsonDocument::Compact); }
    const QString selectedModel = m_modelCombo ? m_modelCombo->currentData().toString() : QString();
    if (!selectedModel.isEmpty()) args << "--model" << selectedModel;

    m_isWorking = true; m_sendButton->hide(); m_stopButton->show(); m_inputField->setEnabled(false);
    m_responseBuffer.clear(); m_thinkingBuffer.clear(); m_errorBuffer.clear(); m_thinkingDisplay->clear();
    hideToolCallBanner();
    hideErrorBanner();
    m_statusButton->setText("THINKING"); m_statusButton->show(); m_thinkingPulseTimer->start();

    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_process = new QProcess(this);
    QProcess* proc = m_process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // ADK warns/fails-over when both keys exist; force a single key for this process.
    env.remove("GEMINI_API_KEY");
    env.remove("GOOGLE_API_KEY");
    env.insert("GOOGLE_API_KEY", key);
    m_process->setProcessEnvironment(env);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &GeminiPanel::onProcessReadyRead);
    connect(proc, &QProcess::readyReadStandardError, this, [this, proc](){
        if (proc != m_process) return;
        QByteArray d = proc->readAllStandardError();
        if (!d.isEmpty()) {
            const QString err = QString::fromUtf8(d);
            const QStringList lines = err.split('\n');
            QStringList filtered;
            filtered.reserve(lines.size());
            for (const QString& line : lines) {
                if (!isBenignBackendWarningLine(line)) filtered.append(line);
            }
            const QString cleaned = filtered.join('\n').trimmed();
            if (!cleaned.isEmpty()) {
                if (!m_errorBuffer.isEmpty()) m_errorBuffer += '\n';
                m_errorBuffer += cleaned;
                showErrorBanner(compactErrorSummary(m_errorBuffer), m_errorBuffer);
            }
        }
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &GeminiPanel::onProcessFinished);

    QString sDir = QCoreApplication::applicationDirPath() + "/../python/scripts";
    QString sPath = QDir(sDir).absoluteFilePath("adk_agent.py");
    QString vPy = QDir(sDir).absoluteFilePath("../venv/bin/python");
    QString py = QFile::exists(vPy) ? vPy : "python3";
    
    QStringList pArgs;
    pArgs << sPath << args;
    
    if (!m_mode.isEmpty()) pArgs << "--mode" << m_mode;
    if (!m_projectFilePath.isEmpty()) pArgs << "--project_path" << m_projectFilePath;

    m_process->start(py, pArgs);
}

void GeminiPanel::onProcessReadyRead() {
    QProcess* proc = qobject_cast<QProcess*>(sender());
    if (!proc || proc != m_process || !m_chatArea) return;
    QByteArray d = proc->readAllStandardOutput(); if (d.isEmpty()) return;
    QString text = m_leftover + QString::fromUtf8(d); m_leftover.clear();
    while (!text.isEmpty()) {
        int tS = text.indexOf("<THOUGHT>"), aS = text.indexOf("<ACTION>"), sS = text.indexOf("<SUGGESTION>"), hS = text.indexOf("<HIGHLIGHT>");
        int fT = -1; QList<int> starts = {tS, aS, sS, hS}; for (int s : starts) { if (s != -1 && (fT == -1 || s < fT)) fT = s; }
        if (fT == -1) {
            int lA = text.lastIndexOf('<'); if (lA != -1 && lA > text.length() - 15) { m_leftover = text.mid(lA); text = text.left(lA); }
            if (!text.isEmpty()) {
                const QString clean = sanitizeAgentTextChunk(text);
                if (!clean.isEmpty()) {
                    m_responseBuffer += clean;
                    m_chatArea->moveCursor(QTextCursor::End);
                    m_chatArea->insertPlainText(clean);
                }
                text.clear();
            }
        } else {
            if (fT > 0) {
                QString pT = sanitizeAgentTextChunk(text.left(fT));
                if (!pT.isEmpty()) {
                    m_responseBuffer += pT;
                    m_chatArea->moveCursor(QTextCursor::End);
                    m_chatArea->insertPlainText(pT);
                }
                text = text.mid(fT);
                continue;
            }
            if (text.startsWith("<THOUGHT>")) {
                int end = text.indexOf("</THOUGHT>");
                if (end != -1) { QString th = text.mid(9, end - 9); m_thinkingBuffer += th; m_thinkingDisplay->append(th); text = text.mid(end + 10); }
                else { QString p = text.mid(9); m_thinkingBuffer += p; m_thinkingDisplay->insertPlainText(p); m_leftover = text; text.clear(); }
            } else if (text.startsWith("<ACTION>")) {
                int end = text.indexOf("</ACTION>");
                if (end != -1) { 
                    QString a = text.mid(8, end - 8); m_statusButton->setText(a.toUpper());
                    showToolCallBanner(a);
                    m_chatArea->moveCursor(QTextCursor::End); m_chatArea->insertHtml(QString("<div style='color: #58a6ff; font-size: 11px; margin: 5px 0;'>[ACTION] <i>%1</i></div>").arg(a.toHtmlEscaped()));
                    text = text.mid(end + 9);
                } else { m_leftover = text; text.clear(); }
            } else if (text.startsWith("<SUGGESTION>")) {
                int end = text.indexOf("</SUGGESTION>");
                if (end != -1) {
                    const QString sug = text.mid(12, end - 12).trimmed();
                    const int splitPos = sug.indexOf('|');
                    const QString lbl = (splitPos >= 0 ? sug.left(splitPos) : sug).trimmed();
                    const QString cmd = (splitPos >= 0 ? sug.mid(splitPos + 1) : lbl).trimmed();
                    addSuggestionButton(lbl, cmd);
                    text = text.mid(end + 7);
                    } else { m_leftover = text; text.clear(); }
                    } else if (text.startsWith("<SNIPPET>")) {
                        int end = text.indexOf("</SNIPPET>");
                        if (end != -1) {
                            QString snip = text.mid(9, end - 9).trimmed();
                            if (!snip.isEmpty()) {
                                m_chatArea->moveCursor(QTextCursor::End);
                                // Determine if it's a fix or a feature based on the responseBuffer context
                                QString label = "APPLY AI SNIPPET";
                                QString color = "#238636"; // Success
                                if (m_responseBuffer.contains("fix", Qt::CaseInsensitive) || m_responseBuffer.contains("error", Qt::CaseInsensitive)) {
                                    label = "APPLY AI FIX";
                                    color = "#f85149"; // Danger
                                } else if (m_responseBuffer.contains("part", Qt::CaseInsensitive) || m_responseBuffer.contains("spec", Qt::CaseInsensitive) || m_responseBuffer.contains("lookup", Qt::CaseInsensitive)) {
                                    label = "APPLY COMPONENT SPECS";
                                    color = "#21262d"; // Button
                                }
                                m_chatArea->insertHtml(QString("<div style='margin: 15px 0;'><a href='snippet:%1' style='background: %2; color: #c9d1d9; border: 1px solid #30363d; border-radius: 6px; padding: 10px 24px; text-decoration: none; font-size: 13px; font-weight: bold;'>%3</a></div>").arg(snip.toHtmlEscaped()).arg(color).arg(label));

                            }
                            text = text.mid(end + 10);
                        } else { m_leftover = text; text.clear(); }
                    } else if (text.startsWith("<NETLIST>")) {
                        int end = text.indexOf("</NETLIST>");
                        if (end != -1) {
                            QString netlist = text.mid(9, end - 9).trimmed();
                            if (!netlist.isEmpty()) {
                                m_chatArea->moveCursor(QTextCursor::End);
                                QString label = "GENERATE SCHEMATIC";
                                QString color = "#8250df"; // Purple action
                                m_chatArea->insertHtml(QString("<div style='margin: 15px 0;'><a href='netlist:%1' style='background: %2; color: #ffffff; border: 1px solid #30363d; border-radius: 6px; padding: 10px 24px; text-decoration: none; font-size: 13px; font-weight: bold;'>%3</a></div>").arg(QString(netlist.toUtf8().toBase64())).arg(color).arg(label));
                            }
                            text = text.mid(end + 10);
                        } else { m_leftover = text; text.clear(); }
                    }
                    }
                    }
    m_chatArea->verticalScrollBar()->setValue(m_chatArea->verticalScrollBar()->maximum());
}

void GeminiPanel::onProcessFinished(int ec) {
    QProcess* proc = qobject_cast<QProcess*>(sender());
    if (!proc || proc != m_process) return;
    if (!m_chatArea) return;
    
    // Safety check: ensure start position is still valid (it might be invalid after clearHistory)
    if (m_responseStartPos >= 0 && m_responseStartPos <= m_chatArea->document()->characterCount()) {
        QTextCursor cur(m_chatArea->document());
        cur.setPosition(m_responseStartPos);
        cur.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        cur.removeSelectedText();
    }

    m_thinkingPulseTimer->stop();

    if (!m_errorBuffer.trimmed().isEmpty()) {
        reportError("Viora AI Backend Error", m_errorBuffer, ec != 0);
    }

    if (ec != 0) {
        m_chatArea->moveCursor(QTextCursor::End);
        m_chatArea->insertHtml(QString("<div style='color: #f85149; font-weight: bold; margin: 15px 0;'>[ERROR] SIGNAL INTERRUPTED (Exit Code: %1)</div>").arg(ec));
        m_statusButton->setText("ERROR");
    } else {
        m_statusButton->hide();
    }
    hideToolCallBanner();
    
    if (!m_thinkingBuffer.isEmpty()) { m_chatArea->append(QString("<div style='background: #161b22; border-left: 4px solid #58a6ff; padding: 12px; margin: 16px 0 18px 0;'><div style='color: #8b949e; font-size: 10px; font-weight: bold; margin-bottom: 8px;'>THINKING PROCESS</div><div style='color: #c9d1d9; font-size: 13px; line-height: 1.6;'>%1</div></div>").arg(m_thinkingBuffer.toHtmlEscaped().replace("\n", "<br>"))); }
    if (!m_responseBuffer.isEmpty()) {
        QString cleanResponse = m_responseBuffer;
        cleanResponse.replace(QRegularExpression(R"((?im)(?:^\s*[\u25c8*•\-]?\s*)?Context attached\s*)"), "");
        cleanResponse.replace(QRegularExpression(R"(\n{3,})"), "\n\n");
        cleanResponse = cleanResponse.trimmed();
        QVariantMap ai; ai["role"] = "model"; ai["text"] = cleanResponse; m_history.append(ai);
        QTextDocument doc;
        doc.setDefaultStyleSheet(markdownDocStyleSheet());
        doc.setMarkdown(cleanResponse);
        m_chatArea->insertHtml(wrapModelCard(doc.toHtml()));
        auto ext = [&](const QString& l, const QString& t) { int s = cleanResponse.indexOf("```" + l, 0, Qt::CaseInsensitive); if (s != -1) { s += t.length(); int e = cleanResponse.indexOf("```", s); if (e != -1) return cleanResponse.mid(s, e - s).trimmed(); } return QString(); };
        QString code;
        if (m_mode == "symbol") code = ext("json", "```json"); else if (m_mode == "logic") code = ext("python", "```python"); else code = ext("fluxscript", "```fluxscript");
        if (!code.isEmpty()) { m_lastGeneratedCode = code; m_copyButton->setEnabled(true); if (m_mode == "symbol") emit symbolJsonGenerated(code); else if (m_mode == "logic") emit pythonScriptGenerated(code); else emit fluxScriptGenerated(code); }
    }
    m_isWorking = false; m_inputField->setEnabled(true); m_stopButton->hide(); m_sendButton->show(); updateSendEnabled();
    saveHistory();
    QTimer::singleShot(100, this, [this]() { if (m_inputField) m_inputField->setFocus(); });
}

void GeminiPanel::onCopyClicked() { if (!m_lastGeneratedCode.isEmpty()) { QApplication::clipboard()->setText(m_lastGeneratedCode); m_copyButton->setText("COPIED"); QTimer::singleShot(2000, this, [this](){ if (m_copyButton) m_copyButton->setText("COPY CODE"); }); } }
void GeminiPanel::showToolCallBanner(const QString& actionText) {
    if (!m_toolCallBanner || !m_toolCallSubtitle) return;
    m_toolCallSubtitle->setText(actionToSubtitle(actionText));
    m_toolCallBanner->show();
}

void GeminiPanel::hideToolCallBanner() {
    if (!m_toolCallBanner) return;
    m_toolCallBanner->hide();
}

void GeminiPanel::onStopClicked() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_chatArea->append("<div style='color: #f85149; font-weight: bold; margin: 10px 0;'>[SYSTEM] PROCESS TERMINATED</div>");
        m_statusButton->setText("ABORTED");
        m_thinkingPulseTimer->stop();
    }
    hideToolCallBanner();
}
void GeminiPanel::onAnchorClicked(const QUrl& url) { 
    QString link = url.toString(); 
    if (link.startsWith("suggestion:")) { 
        triggerSuggestionCommand(QString::fromUtf8(QUrl::fromPercentEncoding(link.mid(11).toUtf8()).toUtf8()));
    } else if (link.startsWith("checkpoint:")) {
        int idx = link.mid(11).toInt();
        if (m_undoStack && idx >= 0 && idx <= m_undoStack->count()) {
            m_undoStack->setIndex(idx);
            m_chatArea->append(QString("<div style='color: #8b949e; font-size: 11px; font-style: italic; margin: 4px 0 12px 0; text-align: right;'>[SYSTEM] Schematic reverted to checkpoint %1.</div>").arg(idx));
        }
    } else if (link.startsWith("snippet:")) {
        QString json = link.mid(8);
        emit snippetGenerated(json);
        m_statusButton->setText("PLACING SNIPPET");
        m_statusButton->show();
        QTimer::singleShot(2000, m_statusButton, &QPushButton::hide);
    } else if (link.startsWith("netlist:")) {
        QString base64 = link.mid(8);
        QString netlist = QString::fromUtf8(QByteArray::fromBase64(base64.toUtf8()));
        emit netlistGenerated(netlist);
        m_statusButton->setText("GENERATING SCHEMATIC");
        m_statusButton->show();
        QTimer::singleShot(2000, m_statusButton, &QPushButton::hide);
    }
}

void GeminiPanel::clearSuggestionButtons() {
    m_suggestionKeys.clear();
}

void GeminiPanel::addSuggestionButton(const QString& label, const QString& command) {
    if (!m_chatArea) return;
    const QString cleanLabel = label.trimmed().isEmpty() ? QString("RUN") : label.trimmed();
    const QString cleanCommand = command.trimmed().isEmpty() ? cleanLabel : command.trimmed();
    const QString key = QString("%1|%2").arg(cleanLabel.toLower(), cleanCommand.toLower());
    if (m_suggestionKeys.contains(key)) return;
    m_suggestionKeys.insert(key);

    PCBTheme* theme = ThemeManager::theme();
    const bool isLight = theme && theme->type() == PCBTheme::Light;
    const QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(cleanCommand));
    const QString bg = isLight ? "#eff6ff" : "#1d2738";
    const QString fg = isLight ? "#1d4ed8" : "#9cc8ff";
    const QString border = isLight ? "#bfdbfe" : "#30405a";

    m_chatArea->moveCursor(QTextCursor::End);
    m_chatArea->insertHtml(QString(
        "<div style='margin: 8px 0 12px 0;'>"
        "<a href='suggestion:%1' style='display:inline-block; background:%2; color:%3; border:1px solid %4; border-radius:6px; padding:6px 12px; text-decoration:none; font-size:11px; font-weight:700; text-transform:uppercase;'>%5</a>"
        "</div>")
        .arg(encoded, bg, fg, border, cleanLabel.toHtmlEscaped()));
}

void GeminiPanel::triggerSuggestionCommand(const QString& command) {
    const QString cmd = command.trimmed();
    if (cmd.isEmpty()) return;

    const bool hasExternalHandler = receivers(SIGNAL(suggestionTriggered(QString))) > 0;
    emit suggestionTriggered(cmd);

    if (!hasExternalHandler && !m_isWorking) {
        askPrompt(cmd, m_includeContextCheck ? m_includeContextCheck->isChecked() : true);
    }

    if (m_statusButton) {
        m_statusButton->setText("EXEC: " + cmd.toUpper());
        m_statusButton->show();
        QTimer::singleShot(2000, m_statusButton, &QPushButton::hide);
    }
}

void GeminiPanel::updateThinkingPulse() { m_pulseStep = (m_pulseStep + 1) % 4; QString dots = QString(".").repeated(m_pulseStep); QString base = m_statusButton->text().replace(".", ""); m_statusButton->setText(base + dots); }
void GeminiPanel::updateContextCheckbox() { 
    if (m_mode == "ask") m_includeContextCheck->setToolTip("Attach Full Circuit Context for Explanation");
    else m_includeContextCheck->setToolTip("Attach Schematic Context"); 
}

void GeminiPanel::onRefreshModelsClicked() {
    refreshModelList();
}

void GeminiPanel::refreshModelList() {
    QString key = ConfigManager::instance().geminiApiKey().trimmed();
    if (key.isEmpty()) {
        reportError("Model Fetch Error", "Cannot fetch models: API key is missing.", true);
        m_apiKeyContainer->show();
        return;
    }
    if (m_modelFetchProcess && m_modelFetchProcess->state() != QProcess::NotRunning) return;

    m_modelFetchStdErr.clear();
    m_refreshModelsButton->setEnabled(false);
    m_refreshModelsButton->setText("WAIT");

    if (m_modelFetchProcess) {
        m_modelFetchProcess->deleteLater();
    }
    m_modelFetchProcess = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // gemini_query.py reads GEMINI_API_KEY; keep only that key here.
    env.remove("GOOGLE_API_KEY");
    env.insert("GEMINI_API_KEY", key);
    m_modelFetchProcess->setProcessEnvironment(env);

    connect(m_modelFetchProcess, &QProcess::readyReadStandardError, this, [this]() {
        if (!m_modelFetchProcess) return;
        m_modelFetchStdErr += QString::fromUtf8(m_modelFetchProcess->readAllStandardError());
    });
    connect(m_modelFetchProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &GeminiPanel::onModelFetchFinished);

    QString sDir = QCoreApplication::applicationDirPath() + "/../python/scripts";
    QString sPath = QDir(sDir).absoluteFilePath("gemini_query.py");
    QString vPy = QDir(sDir).absoluteFilePath("../venv/bin/python");
    QString py = QFile::exists(vPy) ? vPy : "python3";
    m_modelFetchProcess->start(py, QStringList() << sPath << "--list-models");
}

void GeminiPanel::onModelFetchFinished(int exitCode, QProcess::ExitStatus) {
    if (m_refreshModelsButton) {
        m_refreshModelsButton->setEnabled(true);
        m_refreshModelsButton->setText("REFRESH");
    }
    if (!m_modelFetchProcess) return;

    const QString stdoutText = QString::fromUtf8(m_modelFetchProcess->readAllStandardOutput()).trimmed();
    const QString selectedBeforeRaw = m_modelCombo ? m_modelCombo->currentData().toString() : QString();
    QString selectedBefore = selectedBeforeRaw.trimmed();
    // Ignore legacy bootstrap default so first refresh can pick the preferred model.
    if (selectedBefore == "gemini-2.0-flash-thinking-exp-01-21") {
        selectedBefore.clear();
    }

    if (exitCode != 0) {
        reportError("Model Fetch Error",
                    QString("Model fetch failed: %1").arg(m_modelFetchStdErr.trimmed().isEmpty() ? "Unknown error" : m_modelFetchStdErr.trimmed()),
                    true);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(stdoutText.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        reportError("Model Fetch Error", "Model fetch failed: invalid response format.", true);
        return;
    }

    QSet<QString> uniqueModels;
    QStringList models;
    const QJsonArray arr = doc.array();
    for (const QJsonValue& v : arr) {
        QString modelName;
        if (v.isString()) {
            modelName = v.toString().trimmed();
        } else if (v.isObject()) {
            modelName = v.toObject().value("name").toString().trimmed();
        }
        if (!modelName.isEmpty() && !uniqueModels.contains(modelName)) {
            uniqueModels.insert(modelName);
            models << modelName;
        }
    }
    if (models.isEmpty()) {
        reportError("Model Fetch Error", "No Gemini models were returned for this API key.", true);
        return;
    }

    if (m_modelCombo) {
        m_modelCombo->clear();
        auto prettyModelLabel = [](const QString& modelId) {
            QString lbl = modelId;
            if (lbl.startsWith("models/")) lbl = lbl.mid(7);
            return lbl.trimmed();
        };
        for (const QString& model : models) {
            m_modelCombo->addItem(prettyModelLabel(model), model);
            m_modelCombo->setItemData(m_modelCombo->count() - 1, model, Qt::ToolTipRole);
        }
        int idx = selectedBefore.isEmpty() ? -1 : m_modelCombo->findData(selectedBefore);
        if (idx < 0) {
            const QStringList preferredDefaults = {
                "gemini-3.1-flash-lite-preview",
                "models/gemini-3.1-flash-lite-preview",
                "gemini-3.1-flash-lite-preview-latest"
            };
            for (const QString& candidate : preferredDefaults) {
                idx = m_modelCombo->findData(candidate);
                if (idx >= 0) break;
            }
        }
        if (idx < 0) {
            // Fallback for versioned preview IDs (e.g., ...-preview-YYYYMMDD).
            for (int i = 0; i < m_modelCombo->count(); ++i) {
                const QString data = m_modelCombo->itemData(i).toString().toLower();
                if (data.contains("gemini-3.1") && data.contains("flash-lite") && data.contains("preview")) {
                    idx = i;
                    break;
                }
            }
        }
        if (idx < 0) idx = m_modelCombo->findData("gemini-2.0-flash-thinking-exp-01-21");
        if (idx >= 0) m_modelCombo->setCurrentIndex(idx);
    }
    hideErrorBanner();
}

void GeminiPanel::showErrorBanner(const QString& summaryText, const QString& detailsText) {
    if (!m_errorBanner || !m_errorLabel) return;
    QString cleaned = summaryText.trimmed();
    if (cleaned.isEmpty()) return;
    if (cleaned.size() > 320) cleaned = cleaned.left(320) + "...";
    if (!detailsText.trimmed().isEmpty()) m_lastErrorDetails = detailsText.trimmed();
    m_errorLabel->setText(QString("Backend Error: %1").arg(cleaned.toHtmlEscaped().replace("\n", "<br>")));
    if (m_errorDetailsButton) {
        m_errorDetailsButton->setVisible(!m_lastErrorDetails.trimmed().isEmpty());
    }
    m_errorBanner->show();
}

void GeminiPanel::hideErrorBanner() {
    if (!m_errorBanner || !m_errorLabel) return;
    m_errorLabel->clear();
    m_errorBanner->hide();
}

void GeminiPanel::appendErrorHistory(const QString& title, const QString& detailsText) {
    const QString details = detailsText.trimmed();
    if (details.isEmpty()) return;
    ErrorRecord rec;
    rec.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    rec.title = title.trimmed().isEmpty() ? "Viora AI Error" : title.trimmed();
    rec.details = details;
    m_errorHistory.append(rec);
    if (m_errorHistory.size() > 200) {
        m_errorHistory.removeFirst();
    }
}

void GeminiPanel::ensureErrorDialog() {
    if (m_errorDialog) return;

    m_errorDialog = new QDialog(this);
    m_errorDialog->setWindowTitle("Viora AI Error Center");
    m_errorDialog->setModal(false);
    m_errorDialog->resize(860, 520);

    QVBoxLayout* root = new QVBoxLayout(m_errorDialog);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    QSplitter* split = new QSplitter(Qt::Horizontal, m_errorDialog);
    m_errorHistoryList = new QListWidget(split);
    m_errorHistoryList->setMinimumWidth(280);
    m_errorSummaryView = new QTextEdit(split);
    m_errorSummaryView->setReadOnly(true);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    root->addWidget(split, 1);

    m_errorRawToggle = new QToolButton(m_errorDialog);
    m_errorRawToggle->setText("Show Raw Logs");
    m_errorRawToggle->setCheckable(true);
    root->addWidget(m_errorRawToggle, 0, Qt::AlignLeft);

    m_errorRawView = new QPlainTextEdit(m_errorDialog);
    m_errorRawView->setReadOnly(true);
    m_errorRawView->setVisible(false);
    m_errorRawView->setMaximumBlockCount(20000);
    root->addWidget(m_errorRawView, 1);

    QHBoxLayout* footer = new QHBoxLayout();
    footer->addStretch(1);
    QPushButton* copyBtn = new QPushButton("Copy Details", m_errorDialog);
    QPushButton* closeBtn = new QPushButton("Close", m_errorDialog);
    footer->addWidget(copyBtn);
    footer->addWidget(closeBtn);
    root->addLayout(footer);

    connect(m_errorRawToggle, &QToolButton::toggled, this, [this](bool on) {
        if (!m_errorRawView || !m_errorRawToggle) return;
        m_errorRawView->setVisible(on);
        m_errorRawToggle->setText(on ? "Hide Raw Logs" : "Show Raw Logs");
    });
    connect(m_errorHistoryList, &QListWidget::currentRowChanged, this, &GeminiPanel::selectErrorHistoryRow);
    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        const int row = m_errorHistoryList ? m_errorHistoryList->currentRow() : -1;
        if (row < 0 || row >= m_errorHistory.size()) return;
        QApplication::clipboard()->setText(m_errorHistory[row].details);
    });
    connect(closeBtn, &QPushButton::clicked, m_errorDialog, &QDialog::hide);
}

void GeminiPanel::populateErrorDialogHistory() {
    if (!m_errorHistoryList) return;
    m_errorHistoryList->clear();
    for (const ErrorRecord& rec : m_errorHistory) {
        m_errorHistoryList->addItem(QString("[%1] %2 - %3")
            .arg(rec.timestamp, rec.title, compactErrorSummary(rec.details, 54)));
    }
}

void GeminiPanel::selectErrorHistoryRow(int row) {
    if (!m_errorSummaryView || !m_errorRawView) return;
    if (row < 0 || row >= m_errorHistory.size()) {
        m_errorSummaryView->clear();
        m_errorRawView->clear();
        return;
    }
    const ErrorRecord& rec = m_errorHistory[row];
    m_errorSummaryView->setHtml(QString(
        "<div style='font-weight:700; font-size:14px;'>%1</div>"
        "<div style='color:#6b7280; font-size:11px; margin: 4px 0 10px 0;'>%2</div>"
        "<div>%3</div>")
        .arg(rec.title.toHtmlEscaped(),
             rec.timestamp.toHtmlEscaped(),
             compactErrorSummary(rec.details, 1200).toHtmlEscaped().replace("\n", "<br>")));
    m_errorRawView->setPlainText(rec.details);
}

void GeminiPanel::showErrorDialog(const QString& title, const QString& detailsText) {
    const QString details = detailsText.trimmed();
    if (!details.isEmpty()) {
        appendErrorHistory(title, details);
    }
    if (m_errorHistory.isEmpty()) return;

    ensureErrorDialog();
    populateErrorDialogHistory();
    if (m_errorHistoryList) {
        m_errorHistoryList->setCurrentRow(m_errorHistory.size() - 1);
    }
    m_errorDialog->show();
    m_errorDialog->raise();
    m_errorDialog->activateWindow();
}

void GeminiPanel::reportError(const QString& title, const QString& detailsText, bool openDialog) {
    m_lastErrorTitle = title.trimmed().isEmpty() ? "Viora AI Error" : title.trimmed();
    m_lastErrorDetails = detailsText.trimmed();
    if (m_lastErrorDetails.isEmpty()) return;
    appendErrorHistory(m_lastErrorTitle, m_lastErrorDetails);
    showErrorBanner(compactErrorSummary(m_lastErrorDetails), m_lastErrorDetails);
    if (openDialog) {
        showErrorDialog(QString(), QString());
    }
}

void GeminiPanel::onDismissErrorClicked() {
    m_errorBuffer.clear();
    hideErrorBanner();
}

void GeminiPanel::onViewErrorDetailsClicked() {
    if (m_lastErrorDetails.trimmed().isEmpty() && m_errorHistory.isEmpty()) return;
    showErrorDialog(QString(), QString());
}
