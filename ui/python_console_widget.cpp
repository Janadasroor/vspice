#include "python_console_widget.h"
#include "python_executor.h"

#include <QKeyEvent>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextBlock>

PythonConsoleWidget::PythonConsoleWidget(QWidget* parent)
    : QPlainTextEdit(parent)
{
    setFont(QFont("Monospace", 10));
    setReadOnly(false);
    setTabStopDistance(24);

    // Show startup status
    if (py_executor_is_initialized()) {
        printOutput("Python 3 initialized successfully.\n", false);
    } else {
        printOutput("WARNING: Python runtime not available.\n", true);
    }

    // Initial prompt
    showPrompt();
    m_promptLine = document()->blockCount();
}

void PythonConsoleWidget::executeLine(const QString& code) {
    if (code.trimmed().isEmpty()) {
        showPrompt();
        return;
    }

    m_history.append(code);
    m_historyIndex = m_history.size();

    QString codeToRun = m_inContinuation ? m_currentInput : code;
    QByteArray utf8 = codeToRun.toUtf8();

    int isError = 0;
    char* output = py_executor_execute(utf8.constData(), &isError);

    if (output) {
        QString qOutput = QString::fromUtf8(output);
        printOutput(qOutput, isError != 0);  // Always print, even if empty
        py_executor_free(output);
    } else {
        printOutput("(null result)\n", true);
    }

    if (m_inContinuation) {
        m_currentInput.clear();
        m_inContinuation = false;
    }

    emit commandExecuted(code, QString::fromUtf8(output ? output : ""), isError != 0);
    showPrompt();
}

void PythonConsoleWidget::clearOutput() {
    clear();
    showPrompt();
    m_promptLine = document()->blockCount();
    m_history.clear();
    m_historyIndex = 0;
}

void PythonConsoleWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (event->modifiers() & Qt::ShiftModifier) {
            QPlainTextEdit::keyPressEvent(event);
            return;
        }

        // Get text from prompt line to end
        QTextCursor cursor = textCursor();
        cursor.setPosition(document()->findBlockByLineNumber(m_promptLine - 1).position());
        cursor.movePosition(QTextCursor::End);
        QString code = cursor.selectedText();

        if (code.startsWith(">>> ")) code = code.mid(4);
        if (code.startsWith("... ")) code = code.mid(4);

        if (m_inContinuation) {
            m_currentInput += "\n" + code;
        } else {
            m_currentInput = code;
        }

        // Check for continuation using the C executor
        QByteArray checkUtf8 = code.toUtf8();
        if (!m_inContinuation && !py_executor_is_complete(checkUtf8.constData())) {
            m_inContinuation = true;
            printOutput("\n... ");
            return;
        }

        // Move cursor to end and execute
        cursor.movePosition(QTextCursor::End);
        setTextCursor(cursor);
        printOutput("\n");

        executeLine(m_currentInput);

    } else if (event->key() == Qt::Key_Up) {
        if (m_historyIndex > 0) {
            m_historyIndex--;
            QTextCursor cursor = textCursor();
            cursor.movePosition(QTextCursor::End);
            cursor.setPosition(document()->findBlockByLineNumber(m_promptLine - 1).position(), QTextCursor::KeepAnchor);
            cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            printOutput(m_history[m_historyIndex]);
        }
    } else if (event->key() == Qt::Key_Down) {
        if (m_historyIndex < m_history.size() - 1) {
            m_historyIndex++;
            QTextCursor cursor = textCursor();
            cursor.movePosition(QTextCursor::End);
            cursor.setPosition(document()->findBlockByLineNumber(m_promptLine - 1).position(), QTextCursor::KeepAnchor);
            cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            printOutput(m_history[m_historyIndex]);
        } else {
            m_historyIndex = m_history.size();
            QTextCursor cursor = textCursor();
            cursor.movePosition(QTextCursor::End);
            cursor.setPosition(document()->findBlockByLineNumber(m_promptLine - 1).position(), QTextCursor::KeepAnchor);
            cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
        }
    } else if (event->key() == Qt::Key_C && event->modifiers() & Qt::ControlModifier) {
        // Ctrl+C — cancel current input
        printOutput("^C\n");
        m_inContinuation = false;
        m_currentInput.clear();
        showPrompt();
    } else {
        QPlainTextEdit::keyPressEvent(event);
    }
}

void PythonConsoleWidget::printOutput(const QString& text, bool isError) {
    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::End);

    QTextCharFormat format;
    if (isError) {
        format.setForeground(Qt::red);
    } else {
        format.setForeground(QColor("#cccccc"));
    }

    cursor.insertText(text, format);
    setTextCursor(cursor);
    ensureCursorVisible();
}

void PythonConsoleWidget::showPrompt() {
    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::End);

    QTextCharFormat format;
    format.setForeground(QColor("#569cd6"));  // Blue prompt

    if (m_inContinuation) {
        cursor.insertText("... ", format);
    } else {
        cursor.insertText(">>> ", format);
    }

    // Reset to default text color
    format.setForeground(QColor("#d4d4d4"));
    cursor.insertText("", format);

    m_promptLine = document()->blockCount();
    setTextCursor(cursor);
    ensureCursorVisible();
}
