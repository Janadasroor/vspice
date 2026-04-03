#ifndef PCB_3D_WINDOW_H
#define PCB_3D_WINDOW_H

#include <QMainWindow>
#include <QGraphicsScene>
#include <QUuid>
#include "pcb_3d_view.h"

class PCB3DWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit PCB3DWindow(QGraphicsScene* scene, QWidget* parent = nullptr);
    ~PCB3DWindow() override;

    void updateView();
    void setSubstrateAlpha(float alpha);
    void setComponentAlpha(float alpha);
    void setShowCopper(bool enabled);
    void setShowBottomCopper(bool enabled);

signals:
    void componentPicked(const QUuid& id);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void refreshNetList();

    PCB3DView* m_view = nullptr;
    QGraphicsScene* m_scene = nullptr;
    class QComboBox* m_netCombo = nullptr;
    class QLabel* m_collisionLabel = nullptr;
    class QLabel* m_measureLabel = nullptr;
};

#endif // PCB_3D_WINDOW_H
