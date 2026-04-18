#include "model_library_manager.h"
#include "../core/sim_model_parser.h"
#include "config_manager.h"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDirIterator>
#include <QDebug>
#include <QVector>

namespace {
QString decodeSpiceTextInLibrary(const QByteArray& raw) {
    if (raw.isEmpty()) return QString();

    auto decodeUtf16Le = [](const QByteArray& bytes, int start) {
        QVector<char16_t> u16;
        u16.reserve((bytes.size() - start) / 2);
        for (int i = start; i + 1 < bytes.size(); i += 2) {
            const char16_t ch = static_cast<char16_t>(static_cast<unsigned char>(bytes[i])) |
                                (static_cast<char16_t>(static_cast<unsigned char>(bytes[i + 1])) << 8);
            u16.push_back(ch);
        }
        return QString::fromUtf16(u16.constData(), u16.size());
    };

    auto decodeUtf16Be = [](const QByteArray& bytes, int start) {
        QVector<char16_t> u16;
        u16.reserve((bytes.size() - start) / 2);
        for (int i = start; i + 1 < bytes.size(); i += 2) {
            const char16_t ch = (static_cast<char16_t>(static_cast<unsigned char>(bytes[i])) << 8) |
                                 static_cast<char16_t>(static_cast<unsigned char>(bytes[i + 1]));
            u16.push_back(ch);
        }
        return QString::fromUtf16(u16.constData(), u16.size());
    };

    if (raw.size() >= 2) {
        const unsigned char b0 = static_cast<unsigned char>(raw[0]);
        const unsigned char b1 = static_cast<unsigned char>(raw[1]);
        if (b0 == 0xFF && b1 == 0xFE) return decodeUtf16Le(raw, 2);
        if (b0 == 0xFE && b1 == 0xFF) return decodeUtf16Be(raw, 2);
    }

    int oddZeros = 0;
    int evenZeros = 0;
    const int n = raw.size();
    for (int i = 0; i < n; ++i) {
        if (raw[i] == '\0') {
            if (i % 2 == 0) ++evenZeros;
            else ++oddZeros;
        }
    }
    if (oddZeros > n / 8) return decodeUtf16Le(raw, 0);
    if (evenZeros > n / 8) return decodeUtf16Be(raw, 0);

    return QString::fromUtf8(raw);
}

QString typeToString(SimComponentType type) {
    switch (type) {
        case SimComponentType::Diode: return "Diode";
        case SimComponentType::BJT_NPN: return "NPN";
        case SimComponentType::BJT_PNP: return "PNP";
        case SimComponentType::MOSFET_NMOS: return "NMOS";
        case SimComponentType::MOSFET_PMOS: return "PMOS";
        case SimComponentType::JFET_NJF: return "NJF";
        case SimComponentType::JFET_PJF: return "PJF";
        case SimComponentType::SubcircuitInstance: return "Subcircuit";
        default: return "Model";
    }
}

bool isKicadModelPath(const QString& p) {
    const QString n = QDir::cleanPath(QDir::fromNativeSeparators(p)).toLower();
    return n.contains("/kicad/") || n.endsWith("/kicad");
}
}

ModelLibraryManager& ModelLibraryManager::instance() {
    static ModelLibraryManager instance;
    return instance;
}

ModelLibraryManager::ModelLibraryManager() {
}

ModelLibraryManager::~ModelLibraryManager() {}

void ModelLibraryManager::reload() {
    QWriteLocker locker(&m_lock);
    m_modelIndex.clear();
    m_masterNetlist = SimNetlist();
    locker.unlock();
    
    QStringList paths = ConfigManager::instance().modelPaths();
    bool skipKicad = ConfigManager::instance().kicadDisabled();
    
    for (const QString& path : paths) {
        if (skipKicad && path.contains("kicad", Qt::CaseInsensitive)) continue;
        if (isKicadModelPath(path)) {
            qDebug() << "Skipping KiCad model path:" << path;
            continue;
        }
        
        if (QFileInfo(path).isDir()) {
            scanDirectory(path);
        } else if (QFileInfo(path).isFile()) {
            loadLibraryFile(path);
        }
    }
    
    Q_EMIT libraryReloaded();
}

QVector<SpiceModelInfo> ModelLibraryManager::allModels() const {
    QReadLocker locker(&m_lock);
    return m_modelIndex;
}

QVector<SpiceModelInfo> ModelLibraryManager::search(const QString& query) const {
    QReadLocker locker(&m_lock);
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
    QReadLocker locker(&m_lock);
    return m_masterNetlist.findModel(name.toStdString());
}

const SimSubcircuit* ModelLibraryManager::findSubcircuit(const QString& name) const {
    QReadLocker locker(&m_lock);
    return m_masterNetlist.findSubcircuit(name.toStdString());
}

QString ModelLibraryManager::findLibraryPath(const QString& name) const {
    QReadLocker locker(&m_lock);
    for (const auto& info : m_modelIndex) {
        if (info.name.compare(name, Qt::CaseInsensitive) == 0) {
            return info.libraryPath;
        }
    }
    return QString();
}

void ModelLibraryManager::scanDirectory(const QString& path) {
    if (isKicadModelPath(path)) return;

    QStringList filters;
    filters << "*.lib" << "*.mod" << "*.sub" << "*.sp" << "*.inc" << "*.cmp" << "*.jft" << "*.bjt" << "*.mos";
    
    bool skipKicad = ConfigManager::instance().kicadDisabled();
    QDirIterator countIt(path, filters, QDir::Files, QDirIterator::Subdirectories);
    int total = 0;
    while (countIt.hasNext()) {
        QString f = countIt.next();
        if (skipKicad && f.contains("kicad", Qt::CaseInsensitive)) continue;
        if (isKicadModelPath(f)) continue;
        total++;
    }

    QDirIterator it(path, filters, QDir::Files, QDirIterator::Subdirectories);
    int current = 0;
    while (it.hasNext()) {
        QString f = it.next();
        if (skipKicad && f.contains("kicad", Qt::CaseInsensitive)) continue;
        if (isKicadModelPath(f)) continue;
        
        current++;
        Q_EMIT progressUpdated(QString("Loading model: %1").arg(QFileInfo(f).fileName()), current, total);
        loadLibraryFile(f);
    }
}

void ModelLibraryManager::loadLibraryFile(const QString& path) {
    if (isKicadModelPath(path)) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to open library file:" << path;
        return;
    }
    
    const QString content = decodeSpiceTextInLibrary(file.readAll());
    file.close();

    // Quick sniff for non-SPICE formats
    const QString trimmed = content.trimmed();
    if (trimmed.startsWith("CSI SCH LIB", Qt::CaseInsensitive) || 
        trimmed.startsWith("V2.0", Qt::CaseInsensitive) ||
        trimmed.startsWith("[")) {
        return; // Skip non-SPICE schematic libraries
    }
    
    SimModelParseOptions options;
    options.sourceName = path.toStdString();
    
    // Use a temporary netlist to see what's in THIS file
    SimNetlist tempNetlist;
    std::vector<SimParseDiagnostic> diagnostics;
    
    if (SimModelParser::parseLibrary(tempNetlist, content.toStdString(), options, &diagnostics)) {
        QWriteLocker locker(&m_lock);
        // Add models to index
        for (const auto& [name, model] : tempNetlist.models()) {
            SpiceModelInfo info;
            info.name = QString::fromStdString(name);
            info.type = typeToString(model.type);
            info.libraryPath = path;
            for (const auto& [pname, pval] : model.params) {
                // Formatting very small values using typical scientific notation or normal notation
                QString valStr = QString::number(pval, 'g', 4);
                info.params.append(QString("%1 = %2").arg(QString::fromStdString(pname), valStr));
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
