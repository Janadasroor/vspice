#ifndef SYMBOL_FIELD_EDITOR_DIALOG_H
#define SYMBOL_FIELD_EDITOR_DIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QLabel>
#include <QJsonObject>
#include <QMap>

/**
 * @brief Spreadsheet-style editor for project-wide component fields
 */
class SymbolFieldEditorDialog : public QDialog {
    Q_OBJECT

public:
    struct ComponentData {
        QString filePath;
        int indexInFile;
        QString reference;
        QString type;
        QString value;
        QString manufacturer;
        QString mpn;
        QString description;
        QMap<QString, QString> customFields;
        QString originalReference; // To detect renames
    };

    explicit SymbolFieldEditorDialog(const QString& rootPath, const QString& projectDir, QWidget* parent = nullptr);
    ~SymbolFieldEditorDialog();

private slots:
    void onSave();
    void onCellChanged(int row, int col);

private:
    enum BaseColumn {
        ColReference = 0,
        ColValue,
        ColManufacturer,
        ColMpn,
        ColDescription,
        ColSheet,
        ColType,
        ColBaseCount
    };

    void setupUI();
    void scanProject();
    void populateTable();
    QString cellText(int row, int col) const;

    QString m_rootPath;
    QString m_projectDir;
    QTableWidget* m_table;
    QLabel* m_summaryLabel = nullptr;
    QList<ComponentData> m_components;
    QMap<QString, QJsonObject> m_fileRoots;
    QStringList m_customFieldKeys;
};

#endif // SYMBOL_FIELD_EDITOR_DIALOG_H
