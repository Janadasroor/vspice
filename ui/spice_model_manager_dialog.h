#ifndef SPICE_MODEL_MANAGER_DIALOG_H
#define SPICE_MODEL_MANAGER_DIALOG_H

#include <QDialog>
#include <QTreeWidget>
#include <QLineEdit>
#include <QLabel>
#include <QTextBrowser>
#include "../simulator/bridge/model_library_manager.h"

class SpiceModelManagerDialog : public QDialog {
    Q_OBJECT
public:
    explicit SpiceModelManagerDialog(QWidget* parent = nullptr);

private slots:
    void onSearchChanged(const QString& query);
    void onModelSelected(QTreeWidgetItem* item);
    void onReloadLibraries();
    void onAddLibraryPath();

private:
    void setupUI();
    void updateModelList(const QVector<SpiceModelInfo>& models);

    QLineEdit* m_searchField;
    QTreeWidget* m_modelTree;
    QLabel* m_modelTitle;
    QLabel* m_modelMeta;
    QTextBrowser* m_modelDetails;
    QPushButton* m_reloadBtn;
};

#endif // SPICE_MODEL_MANAGER_DIALOG_H
