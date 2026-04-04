#ifndef DESIGN_REPORT_GENERATOR_H
#define DESIGN_REPORT_GENERATOR_H

#include <QString>
#include <QList>
#include <QMap>
#include <QVariant>

class QGraphicsScene;

struct DesignReportData {
    // Board metadata
    QString boardName;
    QString boardVersion;
    QString generatedAt;
    QString author;

    // Board dimensions
    double boardWidth = 0;   // mm
    double boardHeight = 0;  // mm

    // Layer stackup
    int copperLayerCount = 0;
    double boardThickness = 1.6; // mm
    QString surfaceFinish;       // ENIG, HASL, OSP
    struct LayerInfo {
        QString name;
        QString type;
        QString side;
        double thickness = 0;
        QString material;
        double copperWeightOz = 0;
    };
    QList<LayerInfo> layers;

    // Component summary
    int totalComponents = 0;
    QMap<QString, int> componentsByType;   // footprint type -> count
    QMap<QString, int> componentsByLayer;  // Top/Bottom -> count
    QMap<QString, int> componentsByValue;  // value -> count

    // Net summary
    int totalNets = 0;
    int unroutedNets = 0;
    int totalAirwires = 0;
    double totalTraceLength = 0; // mm
    int totalVias = 0;
    int totalTraceSegments = 0;

    // Manufacturing
    int copperPours = 0;
    int drills = 0;

    // DRC
    int drcErrors = 0;
    int drcWarnings = 0;
    int drcInfos = 0;
    struct DRCViolationInfo {
        QString severity; // Error, Warning, Info
        QString type;
        QString message;
        QString location;
    };
    QList<DRCViolationInfo> drcViolations;

    // Net classes
    struct NetClassInfo {
        QString name;
        double traceWidth = 0;
        double clearance = 0;
        double viaDiameter = 0;
        double viaDrill = 0;
        int netCount = 0;
    };
    QList<NetClassInfo> netClasses;

    // BOM summary
    struct BOMSummary {
        QString value;
        QString footprint;
        QString manufacturer;
        QString mpn;
        QStringList references;
        int quantity = 0;
    };
    QList<BOMSummary> bomSummary;
};

class DesignReportGenerator {
public:
    enum ReportFormat {
        HTML,    // HTML file (can be opened in browser)
        PDF      // PDF file (via QPrinter)
    };

    struct ReportOptions {
        ReportFormat format = PDF;
        bool includeDRC = true;
        bool includeBOM = true;
        bool includeNets = true;
        bool includeLayers = true;
        bool includeComponents = true;
        bool includeNetClasses = true;
        bool includeStatistics = true;
        QString companyName;
        QString projectName;
    };

    /**
     * @brief Collect all design data from the PCB scene.
     */
    static DesignReportData collectData(QGraphicsScene* scene);

    /**
     * @brief Generate a design report as HTML content.
     */
    static QString generateHTML(const DesignReportData& data, const ReportOptions& options);

    /**
     * @brief Generate a design report and save to file (PDF or HTML).
     * @return true on success, error message in errorMessage if false
     */
    static bool generateReport(QGraphicsScene* scene, const QString& filePath,
                               const ReportOptions& options, QString* errorMessage = nullptr);
};

#endif // DESIGN_REPORT_GENERATOR_H
