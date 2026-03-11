#ifndef LIBRARY_BROWSER_DIALOG_H
#define LIBRARY_BROWSER_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QTreeWidget>
#include <QListWidget>
#include <QLabel>
#include <QSplitter>
#include <QGraphicsView>
#include <QGraphicsScene>
#include "../../symbols/models/symbol_definition.h"

using Flux::Model::SymbolDefinition;

class LibraryBrowserDialog : public QDialog {
    Q_OBJECT

public:
    explicit LibraryBrowserDialog(QWidget *parent = nullptr);
    ~LibraryBrowserDialog();

    SymbolDefinition selectedSymbol() const { return m_selectedSymbol; }

signals:
    void symbolPlaced(const SymbolDefinition& symbol);

private slots:
    void onSearch();
    void onCategorySelected(QTreeWidgetItem* item, int column);
    void onResultSelected(QListWidgetItem* item);
    void onPlaceClicked();

private:
    void setupUI();
    void performSymbolSearch(const QString& query);

    QLineEdit* m_searchBox;
    QTreeWidget* m_categoriesTree;
    QListWidget* m_resultsList;
    
    // Preview panel
    QLabel* m_previewTitle;
    QLabel* m_previewDesc;
    QLabel* m_previewStats;
    QGraphicsView* m_previewView;
    QGraphicsScene* m_previewScene;

    SymbolDefinition m_selectedSymbol;
    QList<SymbolDefinition> m_searchResults;
};

#endif // LIBRARY_BROWSER_DIALOG_H
