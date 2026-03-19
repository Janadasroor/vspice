#ifndef SIMULATION_PANEL_H
#define SIMULATION_PANEL_H

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QtCharts/QChartView>
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QListWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QSplitter>
#include <QCheckBox>
#include <QMap>
#include <QPair>
#include <QVariantMap>
#include <QDateTime>
#include "../../simulator/bridge/sim_manager.h"
#include "../../simulator/core/sim_results.h"

class QGraphicsScene;
class SchematicEditor;
class NetManager;

class SimulationPanel : public QWidget {
    Q_OBJECT

public:
    explicit SimulationPanel(QGraphicsScene* scene, NetManager* netManager, const QString& projectDir = QString(), QWidget* parent = nullptr);
    ~SimulationPanel();

    void addProbe(const QString& signalName);
    void addDifferentialProbe(const QString& pNet, const QString& nNet);
    void removeProbe(const QString& signalName);
    void clearAllProbes();
    void clearAllProbesPreserveX();
    void clearResults();
    bool hasProbe(const QString& signalName) const;
    void onRunSimulation();

    struct AnalysisConfig {
        SimAnalysisType type;
        double stop;
        double step;
        double fStart;
        double fStop;
        int pts;
    };
    void setAnalysisConfig(const AnalysisConfig& cfg);
    void setTargetScene(QGraphicsScene* scene, NetManager* netManager, const QString& projectDir, bool clearState = true);
    QWidget* getOscilloscopeContainer() const;
    bool hasResults() const { return m_hasLastResults; }

signals:
    void resultsReady(const SimResults& results);
    void timeSnapshotReady(double t, const QMap<QString, double>& nodeVoltages, const QMap<QString, double>& branchCurrents);
    void probeRequested();
    void placementToolRequested(const QString& toolName);
    void simulationTargetRequested(const QString& targetType, const QString& targetId);
    void overlayVisibilityChanged(bool showVoltage, bool showCurrent);
    void clearOverlaysRequested();

private slots:
    void onAnalysisChanged(int index);
    void onLogReceived(const QString& msg);
    void onSimulationFinished();
    void onViewNetlist();
    void onSimResultsReady(const SimResults& results);
    void onRealTimePointReceived(double t, const std::vector<double>& values);
    void onRealTimeDataBatchReceived(const std::vector<double>& times, const std::vector<std::vector<double>>& values);
    void onTimelineValueChanged(int value);
    void onGeneratorTypeChanged(int index);
    void onApplyGeneratorToSelection();
    void onOpenPwlEditor();
    void onImportPwlCsv();
    void onExportPwlCsv();
    void onSaveGeneratorPreset();
    void onDeleteGeneratorPreset();
    void onGeneratorPresetActivated(int index);
    void onExportResultsCsv();
    void onExportResultsJson();
    void onExportResultsReport();

private:
    void setupUI();
    QString generateSpiceNetlist();
    void plotResults(const QString& rawData); 
    void plotResultsFromRaw(const QString& path);
    void plotBuiltinResults(const SimResults& results);
    void updateVirtualMeters(const SimResults& results);
    QString buildGeneratorExpression() const;
    QVariantMap collectGeneratorConfig() const;
    void applyGeneratorConfig(const QVariantMap& cfg);
    QString generatorPresetsPath() const;
    void loadGeneratorLibrary();
    void saveUserGeneratorPresets() const;
    void refreshGeneratorPresetCombo();
    void seedDefaultPwlPointsIfNeeded();
    bool importPwlCsvFile(const QString& path);
    bool exportPwlCsvFile(const QString& path) const;
    bool exportResultsCsvFile(const QString& path) const;
    bool exportResultsJsonFile(const QString& path) const;
    bool exportResultsReportFile(const QString& path) const;
    double parseValue(const QString& text, double defaultVal);
    double sampleAtX(const SimWaveform& wave, double x) const;
    double estimateFrequency(const SimWaveform& wave) const;
    double estimateFftPeakHz(const SimWaveform& wave) const;
    void updateChartRealTime(const QString& name, double t, double value);
    void appendIssueItem(const QString& msg);

    QGraphicsScene* m_scene;
    NetManager* m_netManager;
    QString m_projectDir;
    bool m_acceptRealTimeStream = false;

    // Signal Selection
    QListWidget* m_signalList;
    QListWidget* m_issueList;
    QTableWidget* m_measurementsTable;
    
    // UI Elements
    QComboBox* m_analysisType;
    QWidget* m_paramWidget;
    QLineEdit* m_param1; // e.g., Start Time / Start Freq
    QLineEdit* m_param2; // e.g., Stop Time / Stop Freq
    QLineEdit* m_param3; // e.g., Step Size / Points
    QLineEdit* m_param4; 
    QLineEdit* m_param5; 

    // Source generator controls
    QComboBox* m_generatorType;
    QComboBox* m_generatorPresetCombo;
    QLabel* m_genLabel1;
    QLabel* m_genLabel2;
    QLabel* m_genLabel3;
    QLabel* m_genLabel4;
    QLabel* m_genLabel5;
    QLabel* m_genLabel6;
    QLineEdit* m_genParam1;
    QLineEdit* m_genParam2;
    QLineEdit* m_genParam3;
    QLineEdit* m_genParam4;
    QLineEdit* m_genParam5;
    QLineEdit* m_genParam6;
    QVector<QPair<QString, QString>> m_pwlPoints;
    QMap<QString, QVariantMap> m_generatorTemplates;
    QMap<QString, QVariantMap> m_userGeneratorPresets;
    
    QTextEdit* m_logOutput;
    QPushButton* m_runButton;
    QTimer* m_logFlushTimer = nullptr;
    QStringList m_logBuffer;
    
    // Timeline / Time-Travel
    QSlider* m_timelineSlider;
    QLabel* m_timelineLabel;
    
    // Plotting
    QChartView* m_plotView;
    QChart* m_chart;
    
    QChartView* m_spectrumView;
    QChart* m_spectrumChart;
    
    // Virtual Instruments
    class WaveformViewer* m_waveformViewer;
    class LogicAnalyzerWidget* m_logicAnalyzer;
    class VoltmeterWidget* m_voltmeter;
    class AmmeterWidget* m_ammeter;
    class WattmeterWidget* m_wattmeter;
    class FrequencyCounterWidget* m_freqCounter;
    class LogicProbeWidget* m_logicProbe;
    class QDoubleSpinBox* m_scopeTimeDiv;
    class QDoubleSpinBox* m_scopeVoltDiv;
    QComboBox* m_scopeChannelCombo;
    QWidget* m_scopeContainer;
    
    QString m_lastNetlistPath;
    QCheckBox* m_overlayPreviousRun;
    double m_cursorAFrac = 0.25;
    double m_cursorBFrac = 0.75;
    SimResults m_lastResults;
    SimResults m_previousResults;
    bool m_hasLastResults = false;
    bool m_buildInProgress = false;
    bool m_hasPreviousResults = false;
    QDateTime m_lastRunTimestampUtc;
    SimNetlist m_currentNetlist;
    QMap<QString, QLineSeries*> m_realTimeSeries;
    int m_realTimePointCounter = 0;
};

#endif // SIMULATION_PANEL_H
