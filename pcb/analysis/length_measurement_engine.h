#ifndef LENGTH_MEASUREMENT_ENGINE_H
#define LENGTH_MEASUREMENT_ENGINE_H

#include <QList>
#include <QString>
#include <QPointF>
#include <QMap>
#include <QMultiMap>

class QGraphicsScene;
class TraceItem;
class ViaItem;
class PadItem;

struct NetLengthData {
    QString netName;
    double totalLength = 0.0;        // mm (trace + via contribution)
    double traceLength = 0.0;        // mm (horizontal traces only)
    double viaLength = 0.0;          // mm (vertical via contribution)
    int viaCount = 0;
    int segmentCount = 0;
    double propagationDelayPs = 0.0; // picoseconds
    QList<QPointF> padPositions;
    bool isDiffPairP = false;
    bool isDiffPairN = false;
    QString diffPairName;
};

/**
 * @brief Computes trace lengths per net from the PCB scene.
 * 
 * Measures all trace segments and vias associated with each net,
 * computes total length and propagation delay.
 */
class LengthMeasurementEngine {
public:
    /**
     * @brief Measure all nets in the scene.
     * @param scene The PCB scene
     * @return Map of net name -> length data
     */
    static QMap<QString, NetLengthData> measureAllNets(QGraphicsScene* scene);

    /**
     * @brief Measure a specific net.
     * @param scene The PCB scene
     * @param netName The net to measure
     * @return Length data for the net
     */
    static NetLengthData measureNet(QGraphicsScene* scene, const QString& netName);

    /**
     * @brief Get all unique net names from the scene.
     */
    static QStringList getNetNames(QGraphicsScene* scene);

    /**
     * @brief Detect differential pairs from net naming conventions.
     * 
     * Recognizes patterns like:
     * - USB_DP, USB_DN
     * - PCIE_TX_P0, PCIE_TX_N0
     * - DQS_P, DQS_N
     * - Any pair ending in _P/_N or +/-
     * 
     * @return Map of pair name -> (P net, N net)
     */
    static QMap<QString, QPair<QString, QString>> detectDiffPairs(QGraphicsScene* scene);

    /**
     * @brief Compute propagation delay from length and stackup.
     * @param lengthMm Trace length in mm
     * @param effectiveEr Effective dielectric constant (default 4.0 for FR4)
     * @return Delay in picoseconds
     */
    static double computePropagationDelay(double lengthMm, double effectiveEr = 4.0);

    /**
     * @brief Compute via length contribution.
     * @param boardThicknessMm Total board thickness
     * @param startLayer Start layer ID
     * @param endLayer End layer ID
     * @return Via length in mm
     */
    static double computeViaLength(double boardThicknessMm = 1.6, int startLayer = 0, int endLayer = 1);

    /**
     * @brief Compute skew between two nets (for diff pairs or bus matching).
     * @param len1 Length of first net
     * @param len2 Length of second net
     * @return Absolute skew in mm
     */
    static double computeSkew(double len1, double len2);

private:
    static QMultiMap<QString, TraceItem*> findTracesForNet(QGraphicsScene* scene, const QString& netName);
    static QList<ViaItem*> findViasForNet(QGraphicsScene* scene, const QString& netName);
    static QList<PadItem*> findPadsForNet(QGraphicsScene* scene, const QString& netName);
};

#endif // LENGTH_MEASUREMENT_ENGINE_H
