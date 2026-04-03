#ifndef PCBCOMPONENTSWIDGET_H
#define PCBCOMPONENTSWIDGET_H

#include <QWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QGraphicsView>
#include <QGraphicsScene>

class PCBComponentsWidget : public QWidget {
    Q_OBJECT

public:
    explicit PCBComponentsWidget(QWidget *parent = nullptr);
    ~PCBComponentsWidget();

    void populate();

signals:
    void footprintSelected(const QString &fpName);
    void footprintCreated(const QString &fpName);

private slots:
    void onSearchTextChanged(const QString &text);
    void onItemClicked(QTreeWidgetItem *item, int column);
    void onCreateFootprint();
    void onOpenLibraryBrowser();

private:
    void updatePreview(const QString& fpName, const QString& libName = QString());

    QLineEdit *m_searchBox;
    QTreeWidget *m_componentList;
    class FootprintPreviewView *m_previewView;
};

#endif // PCBCOMPONENTSWIDGET_H
