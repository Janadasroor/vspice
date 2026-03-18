#include "model_library_manager.h"
#include "../core/sim_model_parser.h"
#include "../../core/config_manager.h"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDirIterator>
#include <QDebug>

namespace {
QString typeToString(SimComponentType type) {
    switch (type) {
        case SimComponentType::Diode: return "Diode";
        case SimComponentType::BJT_NPN: return "NPN";
        case SimComponentType::BJT_PNP: return "PNP";
        case SimComponentType::MOSFET_NMOS: return "NMOS";
        case SimComponentType::MOSFET_PMOS: return "PMOS";
        case SimComponentType::SubcircuitInstance: return "Subcircuit";
        default: return "Model";
    }
}
}

ModelLibraryManager& ModelLibraryManager::instance() {
    static ModelLibraryManager instance;
    return instance;
}

ModelLibraryManager::ModelLibraryManager() {
    reload();
}

ModelLibraryManager::~ModelLibraryManager() {}

void ModelLibraryManager::reload() {
    m_modelIndex.clear();
    m_masterNetlist = SimNetlist();
    
    QStringList paths = ConfigManager::instance().modelPaths();
    for (const QString& path : paths) {
        if (QFileInfo(path).isDir()) {
            scanDirectory(path);
        } else if (QFileInfo(path).isFile()) {
            loadLibraryFile(path);
        }
    }
    
    emit libraryReloaded();
}

QVector<SpiceModelInfo> ModelLibraryManager::allModels() const {
    return m_modelIndex;
}

QVector<SpiceModelInfo> ModelLibraryManager::search(const QString& query) const {
    if (query.isEmpty()) return m_modelIndex;
    
    QVector<SpiceModelInfo> results;
    for (const auto& info : m_modelIndex) {
        if (info.name.contains(query, Qt::CaseInsensitive) || 
            info.type.contains(query, Qt::CaseInsensitive) ||
            info.description.contains(query, Qt::CaseInsensitive)) {
            results.append(info);
        }
    }
    return results;
}

const SimModel* ModelLibraryManager::findModel(const QString& name) const {
    return m_masterNetlist.findModel(name.toStdString());
}

const SimSubcircuit* ModelLibraryManager::findSubcircuit(const QString& name) const {
    return m_masterNetlist.findSubcircuit(name.toStdString());
}

void ModelLibraryManager::scanDirectory(const QString& path) {
    QDirIterator it(path, QStringList() << "*.lib" << "*.mod" << "*.sub" << "*.sp" << "*.inc" << "*.cmp",
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        loadLibraryFile(it.next());
    }
}

void ModelLibraryManager::loadLibraryFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to open library file:" << path;
        return;
    }
    
    QString content = file.readAll();
    file.close();
    
    SimModelParseOptions options;
    options.sourceName = path.toStdString();
    
    // Use a temporary netlist to see what's in THIS file
    SimNetlist tempNetlist;
    std::vector<SimParseDiagnostic> diagnostics;
    
    if (SimModelParser::parseLibrary(tempNetlist, content.toStdString(), options, &diagnostics)) {
        // Add models to index
        for (const auto& [name, model] : tempNetlist.models()) {
            SpiceModelInfo info;
            info.name = QString::fromStdString(name);
            info.type = typeToString(model.type);
            info.libraryPath = path;
            for (const auto& [pname, pval] : model.params) {
                info.params.append(QString::fromStdString(pname));
            }
            m_modelIndex.append(info);
            m_masterNetlist.addModel(model);
        }
        
        // Add subcircuits to index
        for (const auto& [name, sub] : tempNetlist.subcircuits()) {
            SpiceModelInfo info;
            info.name = QString::fromStdString(name);
            info.type = "Subcircuit";
            info.libraryPath = path;
            info.description = QString("%1 pins").arg(sub.pinNames.size());
            m_modelIndex.append(info);
            m_masterNetlist.addSubcircuit(sub);
        }
    }
}
