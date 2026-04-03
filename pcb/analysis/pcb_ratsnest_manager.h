#ifndef PCB_RATSNEST_MANAGER_H
#define PCB_RATSNEST_MANAGER_H

#include <QObject>
#include <QGraphicsScene>
#include <QMap>
#include <QList>
#include <QString>
#include <QPointF>

class RatsnestItem;
class PCBItem;
class PadItem;
class ViaItem;

/**
 * @brief Manages the live rat's nest (unrouted connections) in the PCB scene
 */
class PCBRatsnestManager : public QObject {
    Q_OBJECT

public:
    static PCBRatsnestManager& instance();

    void setScene(QGraphicsScene* scene);
    void update(); // Full refresh
    void updateNet(const QString& netName); // Refresh specific net
    
    void setVisible(bool visible);
    bool isVisible() const { return m_visible; }
    void clearRatsnest();

    QStringList netNames() const { return m_netItems.keys(); }

private:
    explicit PCBRatsnestManager(QObject* parent = nullptr);
    ~PCBRatsnestManager();

    void calculateMST(const QString& netName, const QList<PCBItem*>& targets);

    QGraphicsScene* m_scene;
    bool m_visible;
    
    // Map of net name to its ratsnest line items
    QMap<QString, QList<RatsnestItem*>> m_netItems;
};

#endif // PCB_RATSNEST_MANAGER_H
