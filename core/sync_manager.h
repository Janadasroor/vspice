#ifndef SYNCMANAGER_H
#define SYNCMANAGER_H

#include <QObject>
#include "eco_types.h"

class SyncManager : public QObject {
    Q_OBJECT

public:
    enum class ECOTarget {
        Any,
        Schematic,
        PCB
    };

    static SyncManager& instance() {
        static SyncManager inst;
        return inst;
    }

    void pushECO(const ECOPackage& package, ECOTarget target = ECOTarget::Any) {
        m_pendingECO = package;
        m_pendingTarget = target;
        emit ecoAvailable();
    }

    ECOPackage pendingECO() const {
        return m_pendingECO;
    }

    ECOTarget pendingECOTarget() const {
        return m_pendingTarget;
    }

    void clearPendingECO() {
        m_pendingECO = ECOPackage();
        m_pendingTarget = ECOTarget::Any;
    }

    bool hasPendingECO() const {
        return !m_pendingECO.components.isEmpty() || !m_pendingECO.nets.isEmpty();
    }

    void pushCrossProbe(const QString& refDes, const QString& netName = "") {
        if (m_isProbing) return;
        m_isProbing = true;
        emit crossProbeReceived(refDes, netName);
        m_isProbing = false;
    }

signals:
    void ecoAvailable();
    void crossProbeReceived(const QString& refDes, const QString& netName);

private:
    SyncManager() = default;
    ~SyncManager() = default;
    
    ECOPackage m_pendingECO;
    ECOTarget m_pendingTarget = ECOTarget::Any;
    bool m_isProbing = false;
};

#endif // SYNCMANAGER_H
