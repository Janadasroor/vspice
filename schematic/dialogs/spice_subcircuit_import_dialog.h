#ifndef SPICE_SUBCIRCUIT_IMPORT_DIALOG_H
#define SPICE_SUBCIRCUIT_IMPORT_DIALOG_H

#include <QDialog>
#include <QStringList>

class QCheckBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTableWidget;
class SpiceHighlighter;

class SpiceSubcircuitImportDialog : public QDialog {
    Q_OBJECT

public:
    struct Result {
        QString subcktName;
        QString fileName;
        QString absolutePath;
        QString relativeIncludePath;
        QStringList pins;
        QString netlistText;
        bool insertIncludeDirective = true;
        bool openSymbolEditor = true;
    };

    explicit SpiceSubcircuitImportDialog(const QString& projectDir,
                                         const QString& currentFilePath,
                                         QWidget* parent = nullptr);

    Result result() const { return m_result; }

private slots:
    void onAccepted();
    void updateFromText();

private:
    void setupUi();
    QString baseDirectory() const;
    QString suggestedFileName(const QString& subcktName) const;
    QString targetAbsolutePath() const;
    void refreshPathPreview();

    QString m_projectDir;
    QString m_currentFilePath;
    Result m_result;

    QPlainTextEdit* m_textEdit;
    QLineEdit* m_nameEdit;
    QLineEdit* m_fileNameEdit;
    QLabel* m_pathPreviewLabel;
    QLabel* m_statusLabel;
    QTableWidget* m_pinTable;
    QCheckBox* m_insertIncludeCheck;
    QCheckBox* m_openSymbolEditorCheck;
    QDialogButtonBox* m_buttonBox;
    SpiceHighlighter* m_highlighter;
};

#endif
