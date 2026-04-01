#include "smart_probe_engine.h"
#include "../../python/gemini_panel.h"
#include "smart_probe_overlay.h"
#include <QDebug>

SmartProbeEngine::SmartProbeEngine(GeminiPanel* geminiPanel, SmartProbeOverlay* overlay, QObject* parent) 
    : QObject(parent), m_geminiPanel(geminiPanel), m_overlay(overlay) {
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(600);
    connect(m_debounceTimer, &QTimer::timeout, this, &SmartProbeEngine::onDebounceTimeout);
}

void SmartProbeEngine::probe(const QString& netName, const SimResults& results, const QString& context, const QPoint& viewPos) {
    if (netName.isEmpty()) {
        m_debounceTimer->stop();
        m_overlay->hideOverlay();
        return;
    }

    if (m_currentNet == netName && m_overlay->isVisible()) {
        // Keep it open if still on the same net
        return;
    }

    m_currentNet = netName;
    m_currentResults = results;
    m_currentContext = context;
    m_currentPos = viewPos;

    m_overlay->showAt(viewPos, netName, formatInstantValue(netName, results), context);

    if (m_annotationCache.contains(netName)) {
        m_overlay->updateAIAnnotation(m_annotationCache[netName]);
    } else {
        m_debounceTimer->start();
    }
}

void SmartProbeEngine::clearCache() {
    m_annotationCache.clear();
}

void SmartProbeEngine::onDebounceTimeout() {
    if (!m_geminiPanel || m_currentNet.isEmpty()) return;

    QString analysis = "OP";
    if (m_currentResults.analysisType == SimAnalysisType::Transient) analysis = "Transient";
    else if (m_currentResults.analysisType == SimAnalysisType::AC) analysis = "AC";

    QString prompt = QString("You are a concise circuit engineer. Provide a 2-sentence technical insight for net '%1'.\n"
                             "Context: %2\n Analysis: %3\n Measured: %4\n"
                             "Focus on identifying the signal's role or potential issues.")
                             .arg(m_currentNet, m_currentContext, analysis, formatInstantValue(m_currentNet, m_currentResults));

    m_geminiPanel->askSmartProbe(prompt, 
        [this](const QString& chunk) {
            // Update in real-time
            m_overlay->updateAIAnnotation(chunk);
        },
        [this]() {
            // Done streaming, cache the full response
            m_annotationCache[m_currentNet] = m_overlay->findChild<QLabel*>("m_aiLabel") ? static_cast<QLabel*>(m_overlay->children().at(0))->text() : ""; // Need a better way to get full text
        }
    );
}

QString SmartProbeEngine::formatInstantValue(const QString& netName, const SimResults& results) {
    auto stdNet = netName.toStdString();
    
    // Check DC Op Point first
    if (results.nodeVoltages.count(stdNet)) {
        return QString("V = %1 V (DC)").arg(results.nodeVoltages.at(stdNet), 0, 'f', 3);
    }

    // Check waveforms for Transient/AC
    for (const auto& wf : results.waveforms) {
        if (wf.name == stdNet || wf.name == "V(" + stdNet + ")") {
            if (wf.yData.empty()) return "Analyzing...";
            
            double maxV = -1e308, minV = 1e308, sumSq = 0;
            for (double v : wf.yData) {
                if (v > maxV) maxV = v;
                if (v < minV) minV = v;
                sumSq += v * v;
            }
            double rms = std::sqrt(sumSq / wf.yData.size());
            
            return QString("V_rms = %1 V (Peak-Peak: %2 V to %3 V)")
                    .arg(rms, 0, 'f', 3)
                    .arg(minV, 0, 'f', 3)
                    .arg(maxV, 0, 'f', 3);
        }
    }

    return "No simulation data available.";
}
