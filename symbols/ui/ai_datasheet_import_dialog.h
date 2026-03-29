#ifndef AI_DATASHEET_IMPORT_DIALOG_H
#define AI_DATASHEET_IMPORT_DIALOG_H

#include <QDialog>
#include <QJsonArray>
#include <QProcess>

class QPlainTextEdit;
class QPushButton;
class QLabel;
class QProgressBar;

namespace Flux {
namespace Model {
class SymbolPrimitive;
}
}

/**
 * @brief Dialog for importing datasheet text and extracting pins via AI.
 */
class AIDatasheetImportDialog : public QDialog {
    Q_OBJECT
public:
    explicit AIDatasheetImportDialog(QWidget* parent = nullptr);
    ~AIDatasheetImportDialog();

    QList<Flux::Model::SymbolPrimitive> generatedPins() const { return m_generatedPins; }

private slots:
    void onGenerateClicked();
    void onProcessReadyRead();
    void onProcessFinished(int exitCode);

private:
    QPlainTextEdit* m_inputEdit;
    QPushButton* m_generateBtn;
    QLabel* m_statusLabel;
    QProgressBar* m_progressBar;
    QProcess* m_process = nullptr;
    QString m_responseBuffer;
    
    QList<Flux::Model::SymbolPrimitive> m_generatedPins;
};

#endif // AI_DATASHEET_IMPORT_DIALOG_H
