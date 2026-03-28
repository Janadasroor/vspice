#ifndef SUBCIRCUIT_PICKER_DIALOG_H
#define SUBCIRCUIT_PICKER_DIALOG_H

#include <QDialog>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;

class SubcircuitPickerDialog : public QDialog {
    Q_OBJECT

public:
    explicit SubcircuitPickerDialog(const QString& currentModel = QString(), QWidget* parent = nullptr);

    QString selectedModel() const;

private slots:
    void filterModels(const QString& text);
    void onModelSelected(QListWidgetItem* item);
    void applySelected();

private:
    void loadModels(const QString& currentModel);

    QString m_selectedModel;
    QLineEdit* m_searchEdit = nullptr;
    QListWidget* m_modelList = nullptr;
    QLabel* m_detailLabel = nullptr;
};

#endif
