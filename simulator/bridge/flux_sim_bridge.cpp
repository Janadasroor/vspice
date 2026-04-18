#include "flux/runtime/flux_sim_service.h"
#include "simulation_manager.h"
#include <QString>
#include <QDebug>
#include "jit_context_manager.h"


namespace {

double get_voltage_cb(const char* node) {
    QString qnode = QString::fromLatin1(node);
    if (!qnode.toLower().startsWith("v(")) {
        qnode = QString("v(%1)").arg(qnode);
    }
    return SimulationManager::instance().getVectorValue(qnode);
}

double get_current_cb(const char* branch) {
    QString qbranch = QString::fromLatin1(branch);
    if (!qbranch.toLower().startsWith("i(")) {
        qbranch = QString("i(%1)").arg(qbranch);
    }
    return SimulationManager::instance().getVectorValue(qbranch);
}

double get_param_cb(const char* name) {
    return SimulationManager::instance().getVectorValue(QString::fromLatin1(name));
}

void set_param_cb(const char* name, double value) {
    SimulationManager::instance().setParameter(QString::fromLatin1(name), value);
}

void sim_run_cb() {
    SimulationManager::instance().sendInternalCommand("resume");
}

void sim_stop_cb() {
    SimulationManager::instance().stopSimulation();
}

void sim_pause_cb(int pause) {
    if (pause) SimulationManager::instance().sendInternalCommand("halt");
    else SimulationManager::instance().sendInternalCommand("resume");
}

void log_message_cb(const char* msg) {
    QString qmsg = QString::fromLatin1(msg);
    Q_EMIT SimulationManager::instance().outputReceived(qmsg);
    Flux::JITContextManager::instance().logMessage(qmsg);
}


static FluxSimulationService s_service = {
    get_voltage_cb,
    get_current_cb,
    get_param_cb,
    set_param_cb,
    sim_run_cb,
    sim_stop_cb,
    sim_pause_cb,
    log_message_cb
};

} // namespace

void initializeFluxSimBridge() {
    g_flux_sim_service = &s_service;
    qDebug() << "FluxScript/VioSpice simulation bridge initialized.";
}
