#ifndef FIND_REPLACE_DIALOG_H
#define FIND_REPLACE_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QUuid>

struct EditorSearchResult {
    QString label;      // e.g. "R1 (Resistor)"
    QString value;      // e.g. "10k"
    QString sheetPath;  // e.g. "Root/PowerSupply"
    QString fileName;   // absolute path to file
    QUuid itemId;
    QString context;    // "Reference", "Value", "Net", "Text"
};

class FindReplaceDialog : public QDialog {
    Q_OBJECT

public:
    explicit FindReplaceDialog(QWidget* parent = nullptr);

    void setProjectContext(const QString& currentFile, const QString& projectDir);

signals:
    void navigateToResult(const EditorSearchResult& result);
    void replaceRequested(const EditorSearchResult& result, const QString& newValue);

private slots:
    void onSearch();
    void onReplaceSelected();
    void onReplaceAll();
    void onResultDoubleClicked(QListWidgetItem* item);

private:
    void setupUI();
    void performSearch(const QString& term, const QString& rootFile);
    void searchInFile(const QString& filePath, const QString& term, const QString& sheetPath);

    QLineEdit* m_findEdit;
    QLineEdit* m_replaceEdit;
    QComboBox* m_scopeCombo; // "Current Sheet", "Entire Project"
    QCheckBox* m_matchCase;
    QCheckBox* m_wholeWord;
    
    QListWidget* m_resultsList;
    QLabel* m_statusLabel;
    
    QString m_currentFile;
    QString m_projectDir;
    QList<EditorSearchResult> m_results;
};

#endif // FIND_REPLACE_DIALOG_H
