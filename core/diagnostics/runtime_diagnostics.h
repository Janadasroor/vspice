#pragma once

#include <QByteArray>
#include <QDebug>
#include <QElapsedTimer>
#include <QString>

namespace flux::diagnostics {

inline bool runtimeDiagnosticsEnabled() {
    static int cached = -1;
    if (cached == -1) {
        const QByteArray raw = qgetenv("FLUX_DIAGNOSTICS");
        const QByteArray value = raw.trimmed().toLower();
        cached = (!value.isEmpty() && value != "0" && value != "false") ? 1 : 0;
    }
    return cached == 1;
}

class ScopedTimer {
public:
    explicit ScopedTimer(const char* name)
        : m_name(name)
        , m_enabled(runtimeDiagnosticsEnabled()) {
        if (m_enabled) {
            m_timer.start();
        }
    }

    ~ScopedTimer() {
        if (m_enabled) {
            const double elapsedMs = static_cast<double>(m_timer.nsecsElapsed()) / 1e6;
            qInfo().noquote() << "[diag]" << m_name << "duration_ms=" << QString::number(elapsedMs, 'f', 3);
        }
    }

private:
    const char* m_name;
    bool m_enabled;
    QElapsedTimer m_timer;
};

} // namespace flux::diagnostics

#define FLUX_DIAG_CONCAT_IMPL(x, y) x##y
#define FLUX_DIAG_CONCAT(x, y) FLUX_DIAG_CONCAT_IMPL(x, y)
#define FLUX_DIAG_SCOPE(name_literal) \
    ::flux::diagnostics::ScopedTimer FLUX_DIAG_CONCAT(_fluxDiagScope_, __LINE__)(name_literal)
