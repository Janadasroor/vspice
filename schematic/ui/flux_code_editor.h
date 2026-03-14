#ifndef FLUX_CODE_EDITOR_H
#define FLUX_CODE_EDITOR_H

#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>

class QGraphicsScene;
class NetManager;
class QCompleter;

namespace Flux {

class FluxHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit FluxHighlighter(QTextDocument* parent = nullptr);

protected:
    void highlightBlock(const QString& text) override;

private:
    struct HighlightingRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QList<HighlightingRule> highlightingRules;

    QTextCharFormat keywordFormat;
    QTextCharFormat componentFormat;
    QTextCharFormat singleLineCommentFormat;
    QTextCharFormat quotationFormat;
    QTextCharFormat functionFormat;
    QTextCharFormat numberFormat;
    QTextCharFormat builtinFormat;
    QTextCharFormat multiLineStringFormat;
    
    QRegularExpression multiLineStringPattern1;
    QRegularExpression multiLineStringPattern2;
};

class CodeEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit CodeEditor(QGraphicsScene* scene, NetManager* netManager, QWidget* parent = nullptr);

    void setCompleter(QCompleter* completer);
    QCompleter* completer() const;
    void setScene(QGraphicsScene* scene, NetManager* netManager);
    void updateCompletionKeywords(const QStringList& keywords);
    void setErrorLines(const QMap<int, QString>& errors);
    void setActiveDebugLine(int line);

    void lineNumberAreaPaintEvent(QPaintEvent* event);
    int lineNumberAreaWidth();

public slots:
    void onRunRequested();
    void showFindReplaceDialog();
    void findNext(const QString& text, bool forward = true);
    void replaceText(const QString& find, const QString& replace);

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void focusInEvent(QFocusEvent* e) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* e) override;
    bool event(QEvent* e) override;

private slots:
    void onContentChanged();
    void insertCompletion(const QString& completion);
    void updateLineNumberAreaWidth(int newBlockCount);
    void highlightCurrentLine();
    void updateLineNumberArea(const QRect& rect, int dy);

private:
    void setupEditor();
    QString textUnderCursor() const;

    QGraphicsScene* m_scene;
    NetManager* m_netManager;
    FluxHighlighter* m_highlighter;
    QCompleter* m_completer = nullptr;
    QWidget* m_lineNumberArea;
    QMap<int, QString> m_errorLines;
    int m_activeDebugLine = -1;
};

class LineNumberArea : public QWidget {
public:
    LineNumberArea(CodeEditor* editor) : QWidget(editor), m_codeEditor(editor) {}

    QSize sizeHint() const override {
        return QSize(m_codeEditor->lineNumberAreaWidth(), 0);
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        m_codeEditor->lineNumberAreaPaintEvent(event);
    }

private:
    CodeEditor* m_codeEditor;
};

} // namespace Flux

#endif // FLUX_CODE_EDITOR_H
