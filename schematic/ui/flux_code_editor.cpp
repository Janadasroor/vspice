#include "flux_code_editor.h"
#include <QGraphicsScene>
#include "flux/core/net_manager.h"
#include <QCompleter>
#include <QAbstractItemView>
#include <QScrollBar>
#include <QKeyEvent>
#include <QPainter>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStringListModel>
#include "../../core/diagnostics/debugger.h"

#include <QToolTip>

namespace Flux {

FluxHighlighter::FluxHighlighter(QTextDocument* parent)
    : QSyntaxHighlighter(parent) {
    HighlightingRule rule;

    // Python Keywords
    QStringList pythonKeywords = {
        "and", "as", "assert", "async", "await", "break", "class", "continue",
        "def", "del", "elif", "else", "except", "False", "finally", "for",
        "from", "global", "if", "import", "in", "is", "lambda", "None",
        "nonlocal", "not", "or", "pass", "raise", "return", "True", "try",
        "while", "with", "yield", "self", "net"
    };
    
    keywordFormat.setForeground(QColor("#569cd6")); // Blue
    keywordFormat.setFontWeight(QFont::Bold);
    for (const QString& keyword : pythonKeywords) {
        rule.pattern = QRegularExpression("\\b" + keyword + "\\b");
        rule.format = keywordFormat;
        highlightingRules.append(rule);
    }

    functionFormat.setForeground(QColor("#dcdcaa")); // Yellowish
    rule.pattern = QRegularExpression("\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\()");
    rule.format = functionFormat;
    highlightingRules.append(rule);

    singleLineCommentFormat.setForeground(QColor("#6a9955")); // Green
    rule.pattern = QRegularExpression("#[^\\n]*");
    rule.format = singleLineCommentFormat;
    highlightingRules.append(rule);

    quotationFormat.setForeground(QColor("#ce9178")); // Orange-ish
    rule.pattern = QRegularExpression("\".*?\"");
    rule.format = quotationFormat;
    highlightingRules.append(rule);
    rule.pattern = QRegularExpression("'.*?'");
    highlightingRules.append(rule);

    // Built-ins
    QStringList pythonBuiltins = {
        "abs", "all", "any", "ascii", "bin", "bool", "bytearray", "bytes", "callable",
        "chr", "classmethod", "compile", "complex", "delattr", "dict", "dir", "divmod",
        "enumerate", "eval", "exec", "filter", "float", "format", "frozenset", "getattr",
        "globals", "hasattr", "hash", "help", "hex", "id", "input", "int", "isinstance",
        "issubclass", "iter", "len", "list", "locals", "map", "max", "memoryview", "min",
        "next", "object", "oct", "open", "ord", "pow", "print", "property", "range",
        "repr", "reversed", "round", "set", "setattr", "slice", "sorted", "staticmethod",
        "str", "sum", "super", "tuple", "type", "vars", "zip"
    };
    builtinFormat.setForeground(QColor("#4EC9B0"));
    for (const QString& builtin : pythonBuiltins) {
        rule.pattern = QRegularExpression("\\b" + builtin + "\\b");
        rule.format = builtinFormat;
        highlightingRules.append(rule);
    }
    
    // Numbers
    numberFormat.setForeground(QColor("#b5cea8"));
    rule.pattern = QRegularExpression("\\b[0-9]+(\\.[0-9]+)?\\b");
    rule.format = numberFormat;
    highlightingRules.append(rule);

    // Multi-line strings
    multiLineStringFormat.setForeground(QColor("#ce9178"));
    multiLineStringPattern1 = QRegularExpression("\"\"\"");
    multiLineStringPattern2 = QRegularExpression("'''");
}

void FluxHighlighter::highlightBlock(const QString& text) {
    for (const HighlightingRule& rule : highlightingRules) {
        QRegularExpressionMatchIterator matchIterator = rule.pattern.globalMatch(text);
        while (matchIterator.hasNext()) {
            QRegularExpressionMatch match = matchIterator.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }

    // Process multi-line strings
    setCurrentBlockState(0);
    
    int startIndex = 0;
    if (previousBlockState() != 1 && previousBlockState() != 2) {
        int idx1 = text.indexOf(multiLineStringPattern1);
        int idx2 = text.indexOf(multiLineStringPattern2);
        if (idx1 != -1 && (idx2 == -1 || idx1 < idx2)) {
            startIndex = idx1;
            setCurrentBlockState(1);
        } else if (idx2 != -1) {
            startIndex = idx2;
            setCurrentBlockState(2);
        } else {
            return;
        }
    } else {
        setCurrentBlockState(previousBlockState());
    }

    while (startIndex >= 0) {
        QRegularExpression pattern = (currentBlockState() == 1) ? multiLineStringPattern1 : multiLineStringPattern2;
        QRegularExpressionMatch match = pattern.match(text, startIndex + 3);
        int endIndex = match.capturedStart();
        int commentLength = 0;
        
        if (endIndex == -1) {
            setCurrentBlockState(currentBlockState());
            commentLength = text.length() - startIndex;
        } else {
            commentLength = endIndex - startIndex + match.capturedLength();
            setCurrentBlockState(0);
        }
        
        setFormat(startIndex, commentLength, multiLineStringFormat);
        
        if (currentBlockState() == 0) {
            int idx1 = text.indexOf(multiLineStringPattern1, startIndex + commentLength);
            int idx2 = text.indexOf(multiLineStringPattern2, startIndex + commentLength);
            if (idx1 != -1 && (idx2 == -1 || idx1 < idx2)) {
                startIndex = idx1;
                setCurrentBlockState(1);
            } else if (idx2 != -1) {
                startIndex = idx2;
                setCurrentBlockState(2);
            } else {
                startIndex = -1;
            }
        } else {
            startIndex = -1;
        }
    }
}

CodeEditor::CodeEditor(QGraphicsScene* scene, NetManager* netManager, QWidget* parent)
    : QPlainTextEdit(parent), m_scene(scene), m_netManager(netManager) {
    
    m_lineNumberArea = new LineNumberArea(this);

    connect(this, &CodeEditor::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &CodeEditor::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &CodeEditor::cursorPositionChanged, this, &CodeEditor::highlightCurrentLine);

    updateLineNumberAreaWidth(0);
    highlightCurrentLine();

    setupEditor();
    setMouseTracking(true);
    
    // Initialize default completer
    QStringList keywords = {
        "def", "class", "if", "else", "elif", "for", "while", "return", "import", "from",
        "True", "False", "None", "self", "SmartSignal", "update", "init", "inputs",
        "math.sin", "math.cos", "math.tan", "math.pi", "math.exp", "math.log", "math.sqrt",
        "inputs.get", "abs", "round", "min", "max", "len", "print"
    };
    QCompleter* completer = new QCompleter(keywords, this);
    completer->setModelSorting(QCompleter::CaseInsensitivelySortedModel);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setWrapAround(false);
    setCompleter(completer);

    m_highlighter = new FluxHighlighter(document());
}

void CodeEditor::setScene(QGraphicsScene* scene, NetManager* netManager) {
    m_scene = scene;
    m_netManager = netManager;
}

void CodeEditor::updateCompletionKeywords(const QStringList& additionalKeywords) {
    if (!m_completer) return;
    
    QStringList baseKeywords = {
        "def", "class", "if", "else", "elif", "for", "while", "return", "import", "from",
        "True", "False", "None", "self", "SmartSignal", "update", "init", "inputs",
        "math.sin", "math.cos", "math.tan", "math.pi", "math.exp", "math.log", "math.sqrt",
        "inputs.get", "abs", "round", "min", "max", "len", "print"
    };
    
    QStringList allKeywords = baseKeywords + additionalKeywords;
    allKeywords.removeDuplicates();
    allKeywords.sort();

    auto* model = new QStringListModel(allKeywords, m_completer);
    m_completer->setModel(model);
}

void CodeEditor::setErrorLines(const QMap<int, QString>& errors) {
    m_errorLines = errors;
    highlightCurrentLine();
}

void CodeEditor::setActiveDebugLine(int line) {
    m_activeDebugLine = line;
    highlightCurrentLine();
}

int CodeEditor::lineNumberAreaWidth() {
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) {
        max /= 10;
        digits++;
    }

    int space = 24 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    return space;
}

void CodeEditor::updateLineNumberAreaWidth(int /* newBlockCount */) {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect& rect, int dy) {
    if (dy)
        m_lineNumberArea->scroll(0, dy);
    else
        m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth(0);
}

void CodeEditor::resizeEvent(QResizeEvent* e) {
    QPlainTextEdit::resizeEvent(e);

    QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent* event) {
    QPainter painter(m_lineNumberArea);
    painter.fillRect(event->rect(), QColor("#1e1e1e")); // Match editor background

    // A subtle right-edge line separating numbers from code
    painter.setPen(QColor("#333333"));
    painter.drawLine(m_lineNumberArea->width() - 1, event->rect().top(), 
                     m_lineNumberArea->width() - 1, event->rect().bottom());

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QString number = QString::number(blockNumber + 1);
            painter.setPen(QColor("#6e7681"));
            painter.setFont(this->font()); // Match the editor font
            painter.drawText(0, top, m_lineNumberArea->width() - 12, fontMetrics().height(),
                             Qt::AlignRight | Qt::AlignVCenter, number);

            // Draw Breakpoint
            if (Debugger::instance().hasBreakpoint(blockNumber + 1)) {
                painter.setBrush(QColor("#e51400"));
                painter.setPen(Qt::NoPen);
                painter.drawEllipse(4, top + 4, 10, 10);
            }
        }

        block = block.next();
        if (!block.isValid()) break; // Safety break
        
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

void CodeEditor::highlightCurrentLine() {
    QList<QTextEdit::ExtraSelection> extraSelections;

    if (!isReadOnly()) {
        QTextEdit::ExtraSelection selection;
        QColor lineColor = QColor("#2c2c2d");
        selection.format.setBackground(lineColor);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        extraSelections.append(selection);
    }

    // --- Error Squiggles ---
    for (auto it = m_errorLines.begin(); it != m_errorLines.end(); ++it) {
        int line = it.key();
        QTextBlock block = document()->findBlockByLineNumber(line - 1);
        if (block.isValid()) {
            QTextEdit::ExtraSelection sel;
            sel.cursor = QTextCursor(block);
            
            // Red wavy underline
            sel.format.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
            sel.format.setUnderlineColor(Qt::red);
            sel.format.setToolTip(it.value());
            
            extraSelections.append(sel);
        }
    }

    // Bracket matching logic
    QTextCursor cursor = textCursor();
    QTextBlock block = cursor.block();
    QString text = block.text();
    int pos = cursor.positionInBlock();

    auto matchBracket = [&](QChar searchChar, QChar matchChar, bool searchForward, int originalPos) {
        QTextCursor c = textCursor();
        c.setPosition(block.position() + originalPos);
        int braceDepth = 1;
        
        while (!c.isNull()) {
            if (searchForward) {
                if (!c.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor)) break;
            } else {
                if (!c.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor)) break;
            }
            
            QString s = c.selectedText();
            c.clearSelection();
            if (s.isEmpty()) continue;
            
            QChar ch = s.at(0);
            if (ch == searchChar) braceDepth++;
            else if (ch == matchChar) braceDepth--;
            
            if (braceDepth == 0) {
                QTextEdit::ExtraSelection sel;
                sel.format.setBackground(QColor("#404040"));
                sel.format.setFontWeight(QFont::Bold);
                
                QTextCursor matchCursor = c;
                if (searchForward) matchCursor.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor);
                else matchCursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
                sel.cursor = matchCursor;
                extraSelections.append(sel);
                
                QTextEdit::ExtraSelection origSel;
                origSel.format = sel.format;
                QTextCursor origCursor = textCursor();
                origCursor.setPosition(block.position() + originalPos);
                origCursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
                origSel.cursor = origCursor;
                extraSelections.append(origSel);
                break;
            }
        }
    };

    if (pos >= 0 && pos <= text.length()) {
        QChar prev = pos > 0 ? text.at(pos - 1) : QChar();
        QChar next = pos < text.length() ? text.at(pos) : QChar();
        
        if (prev == '(') matchBracket('(', ')', true, pos - 1);
        else if (prev == '[') matchBracket('[', ']', true, pos - 1);
        else if (prev == '{') matchBracket('{', '}', true, pos - 1);
        else if (prev == ')') matchBracket(')', '(', false, pos - 1);
        else if (prev == ']') matchBracket(']', '[', false, pos - 1);
        else if (prev == '}') matchBracket('}', '{', false, pos - 1);
        else if (next == '(') matchBracket('(', ')', true, pos);
        else if (next == '[') matchBracket('[', ']', true, pos);
        else if (next == '{') matchBracket('{', '}', true, pos);
        else if (next == ')') matchBracket(')', '(', false, pos);
        else if (next == ']') matchBracket(']', '[', false, pos);
        else if (next == '}') matchBracket('}', '{', false, pos);
    }

    // Highlighting for debugger
    if (m_activeDebugLine != -1) {
        QTextEdit::ExtraSelection debugSelection;
        QColor debugColor = (Debugger::instance().state() == DebugState::Paused) ? QColor("#68217a") : QColor("#0e639c");
        debugSelection.format.setBackground(debugColor);
        debugSelection.format.setProperty(QTextFormat::FullWidthSelection, true);
        
        QTextBlock block = document()->findBlockByLineNumber(m_activeDebugLine - 1);
        debugSelection.cursor = QTextCursor(block);
        extraSelections.append(debugSelection);
    }

    setExtraSelections(extraSelections);
}

void CodeEditor::mousePressEvent(QMouseEvent* e) {
    if (e->position().x() < lineNumberAreaWidth()) {
        QTextBlock block = cursorForPosition(e->pos()).block();
        int line = block.blockNumber() + 1;
        
        if (Debugger::instance().hasBreakpoint(line)) {
            Debugger::instance().removeBreakpoint(line);
        } else {
            Debugger::instance().setBreakpoint(line);
        }
        m_lineNumberArea->update();
        return;
    }
    QPlainTextEdit::mousePressEvent(e);
}

void CodeEditor::setupEditor() {
    setStyleSheet(
        "QPlainTextEdit { "
        "background: #1e1e1e; color: #d4d4d4; "
        "font-family: 'Fira Code', 'Consolas', monospace; font-size: 14px; "
        "border: none; padding: 6px; "
        "selection-background-color: #264f78; "
        "}"
    );
    setPlaceholderText("# Define your circuit here...\nimport standard.passives\n\nR1 = component(\"Resistor\", \"10k\")\nnet V1 { R1.p1 }");
    setTabStopDistance(QFontMetricsF(font()).horizontalAdvance(' ') * 4);
}

void CodeEditor::setCompleter(QCompleter* completer) {
    if (m_completer)
        m_completer->disconnect(this);

    m_completer = completer;

    if (!m_completer)
        return;

    m_completer->setWidget(this);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    connect(m_completer, QOverload<const QString&>::of(&QCompleter::activated),
            this, &CodeEditor::insertCompletion);
}

QCompleter* CodeEditor::completer() const {
    return m_completer;
}

void CodeEditor::insertCompletion(const QString& completion) {
    if (m_completer->widget() != this)
        return;
    QTextCursor tc = textCursor();
    int extra = completion.length() - m_completer->completionPrefix().length();
    tc.movePosition(QTextCursor::Left);
    tc.movePosition(QTextCursor::EndOfWord);
    tc.insertText(completion.right(extra));
    setTextCursor(tc);
}

QString CodeEditor::textUnderCursor() const {
    QTextCursor tc = textCursor();
    tc.select(QTextCursor::WordUnderCursor);
    return tc.selectedText();
}

bool CodeEditor::event(QEvent* e) {
    if (e->type() == QEvent::ToolTip) {
        QHelpEvent* helpEvent = static_cast<QHelpEvent*>(e);
        QTextCursor cursor = cursorForPosition(helpEvent->pos());
        cursor.select(QTextCursor::WordUnderCursor);
        QString word = cursor.selectedText();
        
        static QMap<QString, QString> helpDb = {
            {"update", "<b>update(self, t, inputs)</b><br>The main simulation loop. Called at every time-step.<br><i>t</i>: current time in seconds.<br><i>inputs</i>: dictionary of input pin voltages."},
            {"init", "<b>init(self)</b><br>Called once when the simulation starts.<br>Use this to initialize parameters or internal state."},
            {"self", "Refers to the current <b>SmartSignal</b> instance."},
            {"params", "<b>self.params</b><br>A dictionary used to define UI-controllable parameters."},
            {"inputs", "A dictionary containing the current voltages of all input pins."},
            {"math", "Python's standard math library."},
            {"np", "NumPy library for high-performance numerical operations."},
            {"sin", "<b>math.sin(x)</b><br>Returns the sine of x (measured in radians)."},
            {"pi", "The mathematical constant π (3.14159...)."}
        };
        
        if (helpDb.contains(word)) {
            QToolTip::showText(helpEvent->globalPos(), helpDb[word], this);
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    return QPlainTextEdit::event(e);
}

void CodeEditor::focusInEvent(QFocusEvent* e) {
    if (m_completer)
        m_completer->setWidget(this);
    QPlainTextEdit::focusInEvent(e);
}

void CodeEditor::keyPressEvent(QKeyEvent* e) {
    if (m_completer && m_completer->popup()->isVisible()) {
        // The following keys are forwarded by the completer to the help window
        switch (e->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Escape:
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            e->ignore();
            return; // let the completer do default behavior
        default:
            break;
        }
    }

    if (e->modifiers().testFlag(Qt::ControlModifier) && e->key() == Qt::Key_Slash) {
        QTextCursor cursor = textCursor();
        int start = cursor.selectionStart();
        int end = cursor.selectionEnd();
        
        cursor.setPosition(start);
        int startBlock = cursor.blockNumber();
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        int endBlock = cursor.blockNumber();
        
        cursor.beginEditBlock();
        for (int i = startBlock; i <= endBlock; ++i) {
            QTextBlock block = document()->findBlockByLineNumber(i);
            QTextCursor lineCursor(block);
            QString text = block.text();
            
            if (text.trimmed().startsWith("#")) {
                // Uncomment
                int hashPos = text.indexOf("#");
                lineCursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, hashPos);
                lineCursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 1);
                // Also remove one space after # if it exists
                if (text.length() > hashPos + 1 && text.at(hashPos + 1) == ' ') {
                    lineCursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 1);
                }
                lineCursor.removeSelectedText();
            } else {
                // Comment
                lineCursor.insertText("# ");
            }
        }
        cursor.endEditBlock();
        return;
    }

    if (e->modifiers().testFlag(Qt::ControlModifier) && e->key() == Qt::Key_F) {
        showFindReplaceDialog();
        return;
    }

    if (e->key() == Qt::Key_Backtab || (e->key() == Qt::Key_Tab && e->modifiers().testFlag(Qt::ShiftModifier))) {
        QTextCursor cursor = textCursor();
        int start = cursor.selectionStart();
        int end = cursor.selectionEnd();
        
        cursor.setPosition(start);
        int startBlock = cursor.blockNumber();
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        int endBlock = cursor.blockNumber();
        
        cursor.beginEditBlock();
        for (int i = startBlock; i <= endBlock; ++i) {
            QTextBlock block = document()->findBlockByLineNumber(i);
            QString text = block.text();
            if (text.startsWith("    ")) {
                QTextCursor lineCursor(block);
                lineCursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 4);
                lineCursor.removeSelectedText();
            } else if (text.startsWith("\t")) {
                QTextCursor lineCursor(block);
                lineCursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 1);
                lineCursor.removeSelectedText();
            }
        }
        cursor.endEditBlock();
        return;
    }

    if (e->key() == Qt::Key_Tab) {
        insertPlainText("    ");
        return;
    }

    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        QString lineText = textCursor().block().text();
        QString indent;
        for (QChar c : lineText) {
            if (c.isSpace()) indent += c;
            else break;
        }
        if (lineText.trimmed().endsWith(':')) {
            indent += "    "; // Add 4 spaces for new block
        }
        QPlainTextEdit::keyPressEvent(e); // Insert newline
        if (!indent.isEmpty()) {
            insertPlainText(indent);
        }
        return;
    }

    if (e->key() == Qt::Key_Backspace) {
        QTextCursor tc = textCursor();
        if (!tc.hasSelection() && tc.positionInBlock() >= 4) {
            QString text = tc.block().text().left(tc.positionInBlock());
            if (text.endsWith("    ") && text.trimmed().isEmpty()) {
                tc.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, 4);
                tc.removeSelectedText();
                return;
            }
        }
    }

    const bool isShortcut = (e->modifiers().testFlag(Qt::ControlModifier) && e->key() == Qt::Key_E); // CTRL+E as alternative trigger
    if (!m_completer || !isShortcut) // do not process the shortcut when we have a completer
        QPlainTextEdit::keyPressEvent(e);

    const bool ctrlOrShift = e->modifiers().testFlag(Qt::ControlModifier) ||
                             e->modifiers().testFlag(Qt::ShiftModifier);
    if (!m_completer || (ctrlOrShift && e->text().isEmpty()))
        return;

    static QString eow("~!@#$%^&*()_+{}|:\"<>?,./;'[]\\-="); // End of word
    bool hasModifier = (e->modifiers() != Qt::NoModifier) && !ctrlOrShift;
    QString completionPrefix = textUnderCursor();

    if (!isShortcut && (hasModifier || e->text().isEmpty()|| completionPrefix.length() < 2
                      || eow.contains(e->text().right(1)))) {
        m_completer->popup()->hide();
        return;
    }

    if (completionPrefix != m_completer->completionPrefix()) {
        m_completer->setCompletionPrefix(completionPrefix);
        m_completer->popup()->setCurrentIndex(m_completer->completionModel()->index(0, 0));
    }
    QRect cr = cursorRect();
    cr.setWidth(m_completer->popup()->sizeHintForColumn(0)
                + m_completer->popup()->verticalScrollBar()->sizeHint().width());
    m_completer->complete(cr); // popup it up!
}

void CodeEditor::onRunRequested() {
    if (!m_scene || !m_netManager) {
        qWarning() << "FluxScript: Scene or NetManager is null. Cannot run script.";
        return;
    }

    QString source = toPlainText();
    if (source.trimmed().isEmpty()) return;

    /* Legacy FluxScript execution disabled
    Lexer lexer(source);
    QList<Token> tokens = lexer.scanTokens();
    if (tokens.isEmpty()) return;
    
    Parser parser(tokens);
    try {
        auto statements = parser.parse();
        if (statements.empty()) return;

        Evaluator evaluator(m_scene, m_netManager);
        evaluator.evaluate(statements);
    } catch (const std::exception& e) {
        qWarning() << "FluxScript Error:" << e.what();
    }
    */
}

void CodeEditor::showFindReplaceDialog() {
    QDialog* dialog = new QDialog(this);
    dialog->setWindowTitle("Find & Replace");
    dialog->setMinimumWidth(300);

    QVBoxLayout* mainLayout = new QVBoxLayout(dialog);
    
    QHBoxLayout* findLayout = new QHBoxLayout();
    findLayout->addWidget(new QLabel("Find:"));
    QLineEdit* findEdit = new QLineEdit();
    findLayout->addWidget(findEdit);
    mainLayout->addLayout(findLayout);

    QHBoxLayout* replaceLayout = new QHBoxLayout();
    replaceLayout->addWidget(new QLabel("Replace:"));
    QLineEdit* replaceEdit = new QLineEdit();
    replaceLayout->addWidget(replaceEdit);
    mainLayout->addLayout(replaceLayout);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* findBtn = new QPushButton("Find Next");
    QPushButton* replaceBtn = new QPushButton("Replace");
    QPushButton* replaceAllBtn = new QPushButton("Replace All");
    btnLayout->addWidget(findBtn);
    btnLayout->addWidget(replaceBtn);
    btnLayout->addWidget(replaceAllBtn);
    mainLayout->addLayout(btnLayout);

    connect(findBtn, &QPushButton::clicked, [this, findEdit]() {
        findNext(findEdit->text());
    });

    connect(replaceBtn, &QPushButton::clicked, [this, findEdit, replaceEdit]() {
        replaceText(findEdit->text(), replaceEdit->text());
    });

    connect(replaceAllBtn, &QPushButton::clicked, [this, findEdit, replaceEdit]() {
        while (document()->find(findEdit->text(), textCursor()).isNull() == false) {
             replaceText(findEdit->text(), replaceEdit->text());
        }
    });

    dialog->show();
}

void CodeEditor::findNext(const QString& text, bool forward) {
    if (text.isEmpty()) return;
    
    QTextDocument::FindFlags flags;
    if (!forward) flags |= QTextDocument::FindBackward;

    if (!find(text, flags)) {
        // Wrap around
        QTextCursor cursor = textCursor();
        if (forward) cursor.movePosition(QTextCursor::Start);
        else cursor.movePosition(QTextCursor::End);
        setTextCursor(cursor);
        find(text, flags);
    }
}

void CodeEditor::replaceText(const QString& findText, const QString& replaceText) {
    QTextCursor cursor = textCursor();
    if (cursor.hasSelection() && cursor.selectedText() == findText) {
        cursor.insertText(replaceText);
    }
    findNext(findText);
}

void CodeEditor::onContentChanged() {
}

} // namespace Flux
