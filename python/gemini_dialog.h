#ifndef GEMINI_DIALOG_H
#define GEMINI_DIALOG_H

#include <QDialog>
#include <QTextEdit>
#include <QLineEdit>
class GeminiPanel;
class QGraphicsScene;

class GeminiDialog : public QDialog {
    Q_OBJECT
public:
    explicit GeminiDialog(QGraphicsScene* scene = nullptr, QWidget* parent = nullptr);
    GeminiPanel* panel() const { return m_panel; }

private:
    GeminiPanel* m_panel;
};

#endif // GEMINI_DIALOG_H
