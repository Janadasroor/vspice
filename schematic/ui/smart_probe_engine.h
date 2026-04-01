#ifndef SMART_PROBE_ENGINE_H
#define SMART_PROBE_ENGINE_H

#include <QObject>
#include <QTimer>
#include <QHash>
#include <QPoint>
#include "../../simulator/core/sim_results.h"

class GeminiPanel;
class SmartProbeOverlay;

class SmartProbeEngine : public QObject {
    Q_OBJECT

public:
    explicit SmartProbeEngine(GeminiPanel* geminiPanel, SmartProbeOverlay* overlay, QObject* parent = nullptr);

    void probe(const QString& netName, const SimResults& results, const QString& context, const QPoint& viewPos);
    void clearCache();

private slots:
    void onDebounceTimeout();

private:
    GeminiPanel* m_geminiPanel;
    SmartProbeOverlay* m_overlay;
    
    QString m_currentNet;
    SimResults m_currentResults;
    QString m_currentContext;
    QPoint m_currentPos;

    QTimer* m_debounceTimer;
    QHash<QString, QString> m_annotationCache;

    QString formatInstantValue(const QString& netName, const SimResults& results);
};

#endif // SMART_PROBE_ENGINE_H
