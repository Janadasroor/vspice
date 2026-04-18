#include "smart_probe_engine.h"
#include "../../core/config_manager.h"
#include "../../python/cpp/gemini/gemini_panel.h"
#include "smart_probe_overlay.h"
#include <QDebug>

namespace {
QStringList getWaveformNetAliases(const QString& netName) {
    const QString trimmed = netName.trimmed();
    if (trimmed.isEmpty()) return {};

    QStringList aliases{trimmed};
    
    if (trimmed.startsWith("I(", Qt::CaseInsensitive) && trimmed.endsWith(")")) {
        QString comp = trimmed.mid(2, trimmed.size() - 3).trimmed();
        aliases << QString("@%1[i]").arg(comp) << QString("@%1[I]").arg(comp);
    } else if (trimmed.startsWith("P(", Qt::CaseInsensitive) && trimmed.endsWith(")")) {
        QString comp = trimmed.mid(2, trimmed.size() - 3).trimmed();
        aliases << QString("@%1[p]").arg(comp) << QString("@%1[P]").arg(comp);
    } else {
        aliases << QString("V(%1)").arg(trimmed);
        if (trimmed.startsWith("V(", Qt::CaseInsensitive) && trimmed.endsWith(")")) {
            aliases << trimmed.mid(2, trimmed.size() - 3).trimmed();
        }
    }

    const QString upper = trimmed.toUpper();
    if (upper == "GND" || trimmed == "0") {
        aliases << "0" << "GND" << "V(0)" << "V(GND)";
    }
    aliases.removeDuplicates();
    return aliases;
}
} // namespace

SmartProbeEngine::SmartProbeEngine(GeminiPanel* geminiPanel, SmartProbeOverlay* overlay, QObject* parent) 
    : QObject(parent), m_geminiPanel(geminiPanel), m_overlay(overlay) {
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(600);
    connect(m_debounceTimer, &QTimer::timeout, this, &SmartProbeEngine::onDebounceTimeout);

    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    m_hideTimer->setInterval(250); // 250ms grace period before hiding
    connect(m_hideTimer, &QTimer::timeout, this, &SmartProbeEngine::onHideTimeout);
}

void SmartProbeEngine::probe(const QString& netName, const QString& context, const QPoint& viewPos) {
    if (netName.isEmpty()) {
        m_debounceTimer->stop();
        if (!m_hideTimer->isActive()) {
            m_hideTimer->start();
        }
        return;
    }

    // Stop hide timer if we found a net
    m_hideTimer->stop();

    if (m_currentNet == netName && m_overlay->isVisible()) {
        // Smoothly follow the mouse if still on the same net
        m_overlay->move(viewPos);
        return;
    }

    m_currentNet = netName;
    m_currentContext = context;
    m_currentPos = viewPos;

    m_overlay->showAt(viewPos, netName, formatInstantValue(netName, m_currentResults), context);
    m_overlay->clearAIAnnotation(); // Reset for new net

    if (ConfigManager::instance().aiOverlayEnabled()) {
        if (m_annotationCache.contains(netName)) {
            m_overlay->updateAIAnnotation(m_annotationCache[netName]);
        } else {
            m_debounceTimer->start();
        }
    }
}

void SmartProbeEngine::updateResults(const SimResults& results) {
    m_currentResults = results; // Copy only when a new simulation finishes
    clearCache();
}

void SmartProbeEngine::onHideTimeout() {
    m_currentNet.clear();
    m_overlay->hideOverlay();
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
            if (m_overlay && !m_currentNet.isEmpty()) {
                m_annotationCache[m_currentNet] = m_overlay->currentAIAnnotation();
            }
        }
    );
}

QString SmartProbeEngine::formatInstantValue(const QString& netName, const SimResults& results) {
    auto stdNet = netName.toStdString();
    
    // Check DC Op Point first
    if (results.nodeVoltages.count(stdNet)) {
        return QString("V = %1 V (DC)").arg(results.nodeVoltages.at(stdNet), 0, 'f', 3);
    }

    // Check waveforms for Transient/AC (including .step suffixes like "V(out) [step]")
    QStringList stepResults;
    int maxStepsToShow = 3;
    int matchedCount = 0;

    QStringList aliases = getWaveformNetAliases(netName);

    for (const auto& wf : results.waveforms) {
        QString wName = QString::fromStdString(wf.name);
        
        bool isMatch = false;
        // Check exact match with aliases
        for (const QString& alias : aliases) {
            if (wName.compare(alias, Qt::CaseInsensitive) == 0) {
                isMatch = true;
                break;
            }
        }
        
        // Check for step param suffix: "@R1[i] [step=1]"
        if (!isMatch) {
            int bracketIdx = wName.indexOf(" [");
            if (bracketIdx > 0) {
                QString textNoSuffix = wName.left(bracketIdx).trimmed();
                for (const QString& alias : aliases) {
                    if (textNoSuffix.compare(alias, Qt::CaseInsensitive) == 0) {
                        isMatch = true;
                        break;
                    }
                }
            }
        }

        if (isMatch) {
            matchedCount++;
            if (stepResults.size() >= maxStepsToShow) continue;

            if (wf.yData.empty()) {
                stepResults.append("Analyzing...");
                continue;
            }
            
            double maxV = -1e308, minV = 1e308, sumSq = 0;
            size_t validPoints = 0;
            for (double v : wf.yData) {
                if (std::isnan(v) || std::isinf(v)) continue;
                if (v > maxV) maxV = v;
                if (v < minV) minV = v;
                sumSq += v * v;
                validPoints++;
            }

            if (validPoints == 0) {
                stepResults.append("Invalid Data");
                continue;
            }

            double rms = std::sqrt(sumSq / validPoints);
            
            // Extract step suffix if present for display
            bool isCurrent = netName.startsWith("I(", Qt::CaseInsensitive);
            bool isPower = netName.startsWith("P(", Qt::CaseInsensitive);
            
            QString baseLabel = isCurrent ? "I_rms" : (isPower ? "P_avg" : "V_rms");
            QString unit = isCurrent ? "A" : (isPower ? "W" : "V");
            QString prefix = isCurrent ? "I " : (isPower ? "P " : "V ");

            QString label = baseLabel;
            if (wName.indexOf('[') >= 0) {
                int idx = wName.indexOf('[');
                if (idx >= 0) {
                    label = prefix + wName.mid(idx);
                }
            }

            stepResults.append(QString("%1 = %2 %3 (Peak-Peak: %4 %3 to %5 %3)")
                    .arg(label)
                    .arg(rms, 0, 'f', 2)
                    .arg(unit)
                    .arg(minV, 0, 'f', 2)
                    .arg(maxV, 0, 'f', 2));
        }
    }

    if (matchedCount > 0) {
        if (matchedCount > maxStepsToShow) {
            stepResults.append(QString("...and %1 more steps").arg(matchedCount - maxStepsToShow));
        }
        return stepResults.join("\n");
    }

    return "No simulation data available.";
}
