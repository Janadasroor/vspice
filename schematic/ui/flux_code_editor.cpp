#include "flux_code_editor.h"
#include <QGraphicsScene>
#include "net_manager.h"
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
#include <QToolTip>
#include "diagnostics/debugger.h"
#include "jit_context_manager.h"


namespace Flux {

CodeEditor::CodeEditor(QGraphicsScene* scene, NetManager* netManager, QWidget* parent)
    : FluxEditor(parent), m_scene(scene), m_netManager(netManager) {
    
    setMouseTracking(true);
    
    // Initialize FluxScript keywords
    QStringList keywords = {
        "var", "let", "def", "if", "else", "elif", "for", "while", "return", "import", "from",
        "intrinsic", "V", "I", "P", "sim", "math", "Eigen", 
        "True", "False", "None", 
        "math.sin", "math.cos", "math.tan", "math.pi", "math.exp", "math.log", "math.sqrt",
        "abs", "round", "min", "max", "len", "println"
    };
    QCompleter* completer = new QCompleter(keywords, this);
    completer->setModelSorting(QCompleter::CaseInsensitivelySortedModel);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setWrapAround(false);
    setCompleter(completer);

}

void CodeEditor::setScene(QGraphicsScene* scene, NetManager* netManager) {
    m_scene = scene;
    m_netManager = netManager;
}

void CodeEditor::updateCompletionKeywords(const QStringList& additionalKeywords) {
    if (!m_completer) return;
    
    QStringList baseKeywords = {
        "var", "let", "def", "if", "else", "elif", "for", "while", "return", "import", "from",
        "intrinsic", "V", "I", "P", "sim", "math", "Eigen", 
        "True", "False", "None", 
        "math.sin", "math.cos", "math.tan", "math.pi", "math.exp", "math.log", "math.sqrt",
        "abs", "round", "min", "max", "len", "println"
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

void CodeEditor::onRunRequested() {
#ifdef HAVE_FLUXSCRIPT
    QString source = toPlainText();
    QMap<int, QString> errors;
    
    // Clear old errors
    setErrorLines({});
    
    if (JITContextManager::instance().compileAndLoad(source, errors)) {
        // Success notification is handled by the manager signal if needed,
        // or we can show a tooltip/status bar message here.
        qDebug() << "FluxScript: Run successful.";
    } else {
        // Show errors in editor
        setErrorLines(errors);
        qDebug() << "FluxScript: Run failed with errors.";
    }
#endif
}


void CodeEditor::keyPressEvent(QKeyEvent* e) {
    if (m_completer && m_completer->popup()->isVisible()) {
        switch (e->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Escape:
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            e->ignore();
            return;
        default:
            break;
        }
    }

    FluxEditor::keyPressEvent(e);
}

void CodeEditor::focusInEvent(QFocusEvent* e) {
    if (m_completer)
        m_completer->setWidget(this);
    FluxEditor::focusInEvent(e);
}

bool CodeEditor::event(QEvent* e) {
    if (e->type() == QEvent::ToolTip) {
        QHelpEvent* helpEvent = static_cast<QHelpEvent*>(e);
        QTextCursor cursor = cursorForPosition(helpEvent->pos());
        cursor.select(QTextCursor::WordUnderCursor);
        QString word = cursor.selectedText();
        
        static QMap<QString, QString> helpDb = {
            {"V", "<b>V(node)</b><br>Returns the real-time voltage at the specified node.<br><i>Example</i>: V(\"N001\")"},
            {"I", "<b>I(branch)</b><br>Returns the current flowing through a branch or device.<br><i>Example</i>: I(\"V1\")"},
            {"P", "<b>P(param)</b><br>Gets or sets a simulation parameter.<br><i>Example</i>: P(\"R1.R\") = 10k"},
            {"sim", "<b>sim object</b><br>Simulation control object.<br><i>Methods</i>: sim.run(), sim.stop(), sim.pause()"},
            {"var", "Explicit variable declaration with type inference."},
            {"intrinsic", "Declares an external C/C++ function for JIT linking."},
            {"math", "FluxScript's high-performance math library (mapped to LLVM intrinsics)."},
            {"println", "<b>println(str)</b><br>Logs a message to the VioSpice simulation console."},
            {"Eigen", "Linear algebra support via the Eigen JIT bridge."},
            {"sin", "<b>math.sin(x)</b><br>Returns the sine of x (radians). Evaluated at JIT-speed."},
            {"pi", "The mathematical constant π (3.14159...)."}
        };

        
        if (helpDb.contains(word)) {
            QToolTip::showText(helpEvent->globalPos(), helpDb[word], this);
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    return FluxEditor::event(e);
}

void CodeEditor::showFindReplaceDialog() {}
void CodeEditor::findNext(const QString& /* text */, bool /* forward */) {}
void CodeEditor::replaceText(const QString& /* find */, const QString& /* replace */) {}
void CodeEditor::onContentChanged() {}
void CodeEditor::insertCompletion(const QString& /* completion */) {}

} // namespace Flux
