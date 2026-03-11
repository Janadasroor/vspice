#ifndef MODEL_BROWSER_WIDGET_H
#define MODEL_BROWSER_WIDGET_H

#include <QWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include "../../simulator/bridge/model_library_manager.h"

class ModelBrowserWidget : public QWidget {
    Q_OBJECT
public:
    explicit ModelBrowserWidget(QWidget* parent = nullptr);
    ~ModelBrowserWidget();

signals:
    void modelSelected(const SpiceModelInfo& info);
    void applyModelRequested(const SpiceModelInfo& info);

private slots:
    void onSearchChanged(const QString& text);
    void onItemSelectionChanged();
    void onApplyClicked();
    void onReloadClicked();
    void onLibraryReloaded();

private:
    void setupUI();
    void populateTree(const QVector<SpiceModelInfo>& models);

    QLineEdit* m_searchBox;
    QTreeWidget* m_tree;
    QLabel* m_detailLabel;
    QPushButton* m_applyBtn;
    
    QVector<SpiceModelInfo> m_currentModels;
};

#endif // MODEL_BROWSER_WIDGET_H
