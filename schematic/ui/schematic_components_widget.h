#ifndef SCHEMATICCOMPONENTSWIDGET_H
#define SCHEMATICCOMPONENTSWIDGET_H

#include <QWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QLabel>
#include <QTabWidget>
#include "../../symbols/models/symbol_definition.h"
#include "../../simulator/bridge/model_library_manager.h"

using Flux::Model::SymbolDefinition;

class SchematicComponentsWidget : public QWidget {
    Q_OBJECT

public:
    explicit SchematicComponentsWidget(QWidget *parent = nullptr);
    ~SchematicComponentsWidget();

    void populate();
    void focusSearch();

signals:
    void toolSelected(const QString &toolName);
    void symbolCreated(const QString &symbolName);
    void symbolPlacementRequested(const class SymbolDefinition& symbol);
    void modelAssignmentRequested(const QString& modelName);

private slots:
    void onSearchTextChanged(const QString &text);
    void onItemClicked(QTreeWidgetItem *item, int column);
    void onCreateSymbol();
    void onOpenLibraryBrowser();
    void onApplyModelRequested(const SpiceModelInfo& info);

private:
    QTabWidget *m_tabs;
    QWidget *m_symbolTab;
    class ModelBrowserWidget *m_modelTab;

    QLineEdit *m_searchBox;
    QTreeWidget *m_componentList;
    SymbolDefinition m_selectedSymbol;
    
    QIcon createComponentIcon(const QString& name);
};

#endif // SCHEMATICCOMPONENTSWIDGET_H
