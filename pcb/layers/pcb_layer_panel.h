#ifndef PCBLAYERPANEL_H
#define PCBLAYERPANEL_H

#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QCheckBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QColorDialog>
#include <QMenu>
#include <QLineEdit>

class PCBLayerPanel : public QWidget {
    Q_OBJECT

public:
    explicit PCBLayerPanel(QWidget* parent = nullptr);

signals:
    void activeLayerChanged(int layerId);
    void layerVisibilityChanged(int layerId, bool visible);

public slots:
    void refreshLayers();
    void selectLayer(int layerId);

private slots:
    void onLayerItemClicked(QTreeWidgetItem* item, int column);
    void onLayerItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onLayerContextMenu(const QPoint& pos);
    void onShowAllLayers();
    void onHideAllLayers();
    void onToggleTopLayers();
    void onToggleBottomLayers();

private:
    void setupUI();
    void createLayerItem(int layerId, const QString& name, const QColor& color, bool visible, bool active);
    QWidget* createColorButton(const QColor& color, int layerId);
    QWidget* createVisibilityCheckbox(bool visible, int layerId);

    QTreeWidget* m_layerTree;
    QLineEdit* m_searchEdit;
    QPushButton* m_showAllBtn;
    QPushButton* m_hideAllBtn;
    QLabel* m_activeLayerLabel;
};

#endif // PCBLAYERPANEL_H
