#ifndef SPICE_MODEL_ARCHITECT_H
#define SPICE_MODEL_ARCHITECT_H

#include <QDialog>
#include <QTableWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QMap>

class SpiceModelArchitect : public QDialog {
    Q_OBJECT

public:
    explicit SpiceModelArchitect(QWidget *parent = nullptr);
    ~SpiceModelArchitect();

private slots:
    void onTypeChanged(int index);
    void onParameterChanged();
    void onCopyClicked();
    void onSaveToLibraryClicked();
    void onResetClicked();
    void onAutoArchitectClicked();

private:
    void setupUi();
    void applyTheme();
    void populateParameters(const QString& type);
    QString generateModelLine() const;
    void updatePreview();
    void runAutoArchitect(const QString& pdfPath);

    struct ParameterInfo {
        QString name;
        QString defaultValue;
        QString description;
        QString unit;
    };

    QComboBox* m_typeCombo;
    QLineEdit* m_nameEdit;
    QTableWidget* m_paramTable;
    QTextEdit* m_previewArea;
    
    QMap<QString, QList<ParameterInfo>> m_deviceDefinitions;
};

#endif // SPICE_MODEL_ARCHITECT_H
