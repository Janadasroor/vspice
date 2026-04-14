#include "symbol_library.h"
#include "kicad_symbol_importer.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QColor>
#include <QDateTime>
#include <QSaveFile>
#include <QtConcurrent/QtConcurrent>
#include <cmath>

using Flux::Model::SymbolDefinition;
using Flux::Model::SymbolPrimitive;

namespace {
constexpr qreal kExternalPinSnapGrid = 15.0;
constexpr qreal kExternalPinSnapEpsilon = 1e-6;
constexpr qreal kMaxAxisResidualForAutoAlign = 2.0;

qreal wrapMod(qreal value, qreal mod) {
    if (mod <= 0.0) return 0.0;
    qreal r = std::fmod(value, mod);
    if (r < 0.0) r += mod;
    return r;
}

qreal circularDist(qreal a, qreal b, qreal mod) {
    const qreal d = std::abs(a - b);
    return std::min(d, mod - d);
}

qreal axisResidualRms(const QList<qreal>& coords, qreal grid, qreal shift) {
    if (coords.isEmpty() || grid <= 0.0) return 0.0;
    qreal sumSq = 0.0;
    for (qreal c : coords) {
        const qreal d = circularDist(wrapMod(c + shift, grid), 0.0, grid);
        sumSq += d * d;
    }
    return std::sqrt(sumSq / static_cast<qreal>(coords.size()));
}

qreal bestAxisShift(const QList<qreal>& coords, qreal grid) {
    if (coords.isEmpty() || grid <= 0.0) return 0.0;

    QList<qreal> candidates;
    candidates.reserve(coords.size() + 1);
    candidates.append(0.0);
    for (qreal c : coords) candidates.append(wrapMod(c, grid));

    qreal bestR = 0.0;
    qreal bestScore = std::numeric_limits<qreal>::max();
    for (qreal r : candidates) {
        qreal score = 0.0;
        for (qreal c : coords) {
            const qreal d = circularDist(wrapMod(c, grid), r, grid);
            score += d * d;
        }
        if (score < bestScore) {
            bestScore = score;
            bestR = r;
        }
    }

    qreal shift = -bestR;
    if (shift > grid / 2.0) shift -= grid;
    if (shift < -grid / 2.0) shift += grid;
    return shift;
}

bool normalizeExternalSymbolPinGrid(SymbolDefinition& sym) {
    QList<qreal> pinXs;
    QList<qreal> pinYs;
    for (const SymbolPrimitive& prim : sym.primitives()) {
        if (prim.type != SymbolPrimitive::Pin) continue;
        pinXs.append(prim.data.value("x").toDouble());
        pinYs.append(prim.data.value("y").toDouble());
    }

    if (pinXs.isEmpty()) return false;

    qreal dx = bestAxisShift(pinXs, kExternalPinSnapGrid);
    qreal dy = bestAxisShift(pinYs, kExternalPinSnapGrid);

    // Apply only when the symbol is globally close to one grid lattice on BOTH axes.
    // If either axis is poor, leave symbol untouched to preserve native geometry.
    const qreal rmsX = axisResidualRms(pinXs, kExternalPinSnapGrid, dx);
    const qreal rmsY = axisResidualRms(pinYs, kExternalPinSnapGrid, dy);
    if (rmsX > kMaxAxisResidualForAutoAlign || rmsY > kMaxAxisResidualForAutoAlign) {
        return false;
    }

    if (std::abs(dx) <= kExternalPinSnapEpsilon && std::abs(dy) <= kExternalPinSnapEpsilon) {
        return false;
    }

    QList<SymbolPrimitive>& prims = sym.primitives();
    for (SymbolPrimitive& prim : prims) prim.move(dx, dy);
    sym.setReferencePos(sym.referencePos() + QPointF(dx, dy));
    sym.setNamePos(sym.namePos() + QPointF(dx, dy));
    bool changed = false;
    changed = true;
    return changed;
}

QString normalizeBuiltInSymbolKey(const QString& name) {
    QString key = name.trimmed().toLower();
    key.replace("(", "_");
    key.replace(")", "");
    key.replace(" ", "_");
    key.replace("-", "_");
    while (key.contains("__")) key.replace("__", "_");
    if (key.startsWith('_')) key.remove(0, 1);
    if (key.endsWith('_')) key.chop(1);
    return key;
}

QString normalizeLookupKey(const QString& raw) {
    return raw.trimmed().toLower();
}

class SymbolMetadataCache {
public:
    SymbolMetadataCache() {
        const QString baseDir = QDir::homePath() + "/ViospiceLib";
        QDir().mkpath(baseDir);
        m_path = baseDir + "/symbol_metadata_cache.json";
        load();
    }

    QList<SymbolLibrary::SymbolInfo> symbolInfosForFile(const QFileInfo& fileInfo) const {
        if (!fileInfo.exists() || !fileInfo.isFile()) return {};

        const QString key = normalizedKey(fileInfo);
        const QJsonObject entry = m_entries.value(key).toObject();
        if (entry.isEmpty()) return {};
        if (entry.value("size").toVariant().toLongLong() != fileInfo.size()) return {};
        if (entry.value("mtime_ms").toVariant().toLongLong() != fileInfo.lastModified().toMSecsSinceEpoch()) return {};

        QList<SymbolLibrary::SymbolInfo> infos;
        const QJsonArray arr = entry.value("symbols").toArray();
        infos.reserve(arr.size());
        for (const QJsonValue& value : arr) {
            const QJsonObject obj = value.toObject();
            const QString name = obj.value("name").toString().trimmed();
            if (name.isEmpty()) continue;

            SymbolLibrary::SymbolInfo info;
            info.name = name;
            info.description = obj.value("description").toString().trimmed();
            info.tags = obj.value("tags").toString().trimmed();
            infos.append(info);
        }
        return infos;
    }

    void storeSymbolInfos(const QFileInfo& fileInfo, const QList<SymbolLibrary::SymbolInfo>& symbolInfos) {
        if (!fileInfo.exists() || !fileInfo.isFile()) return;

        QJsonArray symbols;
        for (const SymbolLibrary::SymbolInfo& info : symbolInfos) {
            const QString trimmed = info.name.trimmed();
            if (trimmed.isEmpty()) continue;

            QJsonObject obj;
            obj["name"] = trimmed;
            if (!info.description.trimmed().isEmpty()) obj["description"] = info.description.trimmed();
            if (!info.tags.trimmed().isEmpty()) obj["tags"] = info.tags.trimmed();
            symbols.append(obj);
        }

        QJsonObject entry;
        entry["path"] = QDir::cleanPath(QDir::fromNativeSeparators(fileInfo.absoluteFilePath()));
        entry["size"] = QString::number(fileInfo.size());
        entry["mtime_ms"] = QString::number(fileInfo.lastModified().toMSecsSinceEpoch());
        entry["symbols"] = symbols;

        m_entries.insert(normalizedKey(fileInfo), entry);
        m_dirty = true;
    }

    void saveIfDirty() const {
        if (!m_dirty) return;

        QSaveFile file(m_path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qWarning() << "Failed to write symbol metadata cache:" << m_path;
            return;
        }

        QJsonObject root;
        root["version"] = 1;
        root["entries"] = m_entries;
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        if (!file.commit()) {
            qWarning() << "Failed to commit symbol metadata cache:" << m_path;
            return;
        }

        m_dirty = false;
    }

private:
    void load() {
        QFile file(m_path);
        if (!file.exists()) return;
        if (!file.open(QIODevice::ReadOnly)) return;

        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject()) return;

        const QJsonObject root = doc.object();
        m_entries = root.value("entries").toObject();
    }

    static QString normalizedKey(const QFileInfo& fileInfo) {
        return QDir::cleanPath(QDir::fromNativeSeparators(fileInfo.absoluteFilePath()));
    }

    QString m_path;
    mutable bool m_dirty = false;
    QJsonObject m_entries;
};

bool symbolMatchesLookupKey(const SymbolDefinition& sym, const QString& query) {
    const QString q = normalizeLookupKey(query);
    if (q.isEmpty()) return false;

    auto eq = [&](const QString& s) {
        return !s.trimmed().isEmpty() && normalizeLookupKey(s) == q;
    };

    if (eq(sym.name())) return true;
    if (eq(sym.symbolId())) return true;
    for (const QString& alias : sym.aliases()) {
        if (eq(alias)) return true;
    }
    return false;
}

QString suggestedBuiltInFootprint(const SymbolDefinition& sym) {
    const QString key = normalizeBuiltInSymbolKey(sym.name());
    const QString cat = sym.category().trimmed().toLower();
    if (sym.isPowerSymbol() || key.contains("gnd") || key.contains("vcc") || key.contains("vdd") ||
        key.contains("vss") || key.contains("vbat") || key == "3.3v" || key == "5v" || key == "12v") {
        return QString();
    }

    if (key.contains("resistor")) return "Resistor_THT_P7.62mm_Horizontal";
    if (key.contains("capacitor")) return "C_Radial_D5.0mm_P2.00mm";
    if (key.contains("diode")) return "D_THT_P7.62mm_Horizontal";
    if (key.contains("transistor")) return "TO-92_Inline";

    int pinCount = 0;
    for (const SymbolPrimitive& prim : sym.primitives()) {
        if (prim.type == SymbolPrimitive::Pin) ++pinCount;
    }

    if (pinCount <= 0) {
        if (cat == "simulation") return QString();
        return "Resistor_THT_P7.62mm_Horizontal";
    }
    if (pinCount == 2) return "Resistor_THT_P7.62mm_Horizontal";
    if (pinCount == 3) return "TO-92_Inline";
    if (pinCount == 8) return "DIP-8_W7.62mm";
    if (pinCount == 14) return "DIP-14_W7.62mm";
    if (pinCount == 16) return "DIP-16_W7.62mm";
    
    return QString("DIP-%1_W7.62mm").arg(pinCount);
}

SymbolDefinition buildDefaultMosfetSymbol(const QString& name, bool nChannel) {
    SymbolDefinition mos(name);
    mos.setCategory("Semiconductors");
    mos.setReferencePrefix("M");
    mos.setDescription(nChannel ? "N-channel MOSFET" : "P-channel MOSFET");
    mos.setDefaultValue(nChannel ? "2N7000" : "BS250");
    mos.setCustomField("generatedBy", "viospice");
    mos.setCustomField("generatedVersion", "2");

    mos.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, 0), 20, false));
    mos.addPrimitive(SymbolPrimitive::createLine(QPointF(-10, -15), QPointF(-10, 15))); // Gate
    mos.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -12), QPointF(0, -5))); // Drain segment
    mos.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 12), QPointF(0, 5))); // Source segment
    mos.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 0), QPointF(10, 0))); // Bulk
    mos.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -12), QPointF(10, -12)));
    mos.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 12), QPointF(10, 12)));

    if (nChannel) {
        QList<QPointF> arrow = {QPointF(8, 0), QPointF(3.2, -2.4), QPointF(3.2, 2.4)};
        mos.addPrimitive(SymbolPrimitive::createPolygon(arrow, true));
    } else {
        QList<QPointF> arrow = {QPointF(2, 0), QPointF(6.8, -2.4), QPointF(6.8, 2.4)};
        mos.addPrimitive(SymbolPrimitive::createPolygon(arrow, true));
    }

    SymbolPrimitive gPin = SymbolPrimitive::createPin(QPointF(-30, 0), 1, "G");
    gPin.data["length"] = 20.0;
    gPin.data["orientation"] = "Right";
    gPin.data["hideNum"] = true;
    gPin.data["hideName"] = true;
    mos.addPrimitive(gPin);

    SymbolPrimitive dPin = SymbolPrimitive::createPin(QPointF(10, -30), 2, "D");
    dPin.data["length"] = 18.0;
    dPin.data["orientation"] = "Down";
    dPin.data["hideNum"] = true;
    dPin.data["hideName"] = true;
    mos.addPrimitive(dPin);

    SymbolPrimitive sPin = SymbolPrimitive::createPin(QPointF(10, 30), 3, "S");
    sPin.data["length"] = 18.0;
    sPin.data["orientation"] = "Up";
    sPin.data["hideNum"] = true;
    sPin.data["hideName"] = true;
    mos.addPrimitive(sPin);

    QMap<int, QString> mosMapping;
    mosMapping[1] = "D";
    mosMapping[2] = "G";
    mosMapping[3] = "S";
    mos.setSpiceNodeMapping(mosMapping);

    return mos;
}

bool isLegacyBuiltInMosfetSymbol(const SymbolDefinition& sym) {
    if (sym.name() != "Transistor_NMOS" && sym.name() != "Transistor_PMOS") return false;
    const SymbolPrimitive* gatePin = sym.pinPrimitive("1");
    const SymbolPrimitive* drainPin = sym.pinPrimitive("2");
    const SymbolPrimitive* sourcePin = sym.pinPrimitive("3");
    if (!gatePin || !drainPin || !sourcePin) return false;

    return gatePin->data.value("orientation").toString() == "Right" &&
           drainPin->data.value("orientation").toString() == "Right" &&
           sourcePin->data.value("orientation").toString() == "Right" &&
           sym.referencePrefix().trimmed() == "Q";
}

void ensureBuiltInDefaultFootprints(SymbolLibrary* lib) {
    if (!lib) return;
    for (const QString& name : lib->symbolNames()) {
        SymbolDefinition* sym = lib->findSymbol(name);
        if (!sym || !sym->defaultFootprint().trimmed().isEmpty()) continue;
        const QString fp = suggestedBuiltInFootprint(*sym);
        if (!fp.isEmpty()) sym->setDefaultFootprint(fp);
    }
}
} // namespace

// ============ SymbolLibrary ============

SymbolLibrary::SymbolLibrary()
    : m_builtIn(false) {
}

SymbolLibrary::SymbolLibrary(const QString& name, bool builtIn)
    : m_name(name), m_builtIn(builtIn) {
}

void SymbolLibrary::addSymbol(const SymbolDefinition& symbol) {
    QWriteLocker locker(&m_lock);
    SymbolDefinition normalized = symbol;
    if (normalized.symbolId().trimmed().isEmpty()) {
        normalized.setSymbolId(normalizeLookupKey(normalized.name()));
    }
    m_symbols[normalized.name()] = normalized;
}

void SymbolLibrary::removeSymbol(const QString& name) {
    QWriteLocker locker(&m_lock);
    m_symbols.remove(name);
}

SymbolDefinition* SymbolLibrary::findSymbol(const QString& name) {
    QWriteLocker locker(&m_lock);
    auto it = m_symbols.find(name);
    if (it != m_symbols.end()) {
        SymbolDefinition& sym = it.value();
        if (sym.isStub()) {
            SymbolDefinition loaded = KicadSymbolImporter::importSymbol(m_path, name);
            if (!loaded.name().isEmpty()) {
                sym = loaded;
                sym.setStub(false);
                normalizeExternalSymbolPinGrid(sym);
            }
        }
        return &sym;
    }

    // Try aliases/lookup
    for (auto jt = m_symbols.begin(); jt != m_symbols.end(); ++jt) {
        if (symbolMatchesLookupKey(jt.value(), name)) {
            SymbolDefinition& sym = jt.value();
            if (sym.isStub()) {
                SymbolDefinition loaded = KicadSymbolImporter::importSymbol(m_path, sym.name());
                if (!loaded.name().isEmpty()) {
                    sym = loaded;
                    sym.setStub(false);
                    normalizeExternalSymbolPinGrid(sym);
                }
            }
            return &sym;
        }
    }
    return nullptr;
}

const SymbolDefinition* SymbolLibrary::findSymbol(const QString& name) const {
    QReadLocker locker(&m_lock);
    auto it = m_symbols.find(name);
    if (it != m_symbols.end()) {
        const SymbolDefinition& sym = it.value();
        if (sym.isStub()) {
            SymbolDefinition loaded = KicadSymbolImporter::importSymbol(m_path, name);
            if (!loaded.name().isEmpty()) {
                SymbolDefinition& mutableSym = const_cast<SymbolDefinition&>(sym);
                mutableSym = loaded;
                mutableSym.setStub(false);
                normalizeExternalSymbolPinGrid(mutableSym);
            }
        }
        return &it.value();
    }

    for (auto jt = m_symbols.begin(); jt != m_symbols.end(); ++jt) {
        if (symbolMatchesLookupKey(jt.value(), name)) {
            const SymbolDefinition& sym = jt.value();
            if (sym.isStub()) {
                SymbolDefinition loaded = KicadSymbolImporter::importSymbol(m_path, sym.name());
                if (!loaded.name().isEmpty()) {
                    SymbolDefinition& mutableSym = const_cast<SymbolDefinition&>(sym);
                    mutableSym = loaded;
                    mutableSym.setStub(false);
                    normalizeExternalSymbolPinGrid(mutableSym);
                }
            }
            return &jt.value();
        }
    }
    return nullptr;
}

QStringList SymbolLibrary::symbolNames() const {
    QReadLocker locker(&m_lock);
    return m_symbols.keys();
}

QList<SymbolLibrary::SymbolInfo> SymbolLibrary::symbolInfos() const {
    QReadLocker locker(&m_lock);
    QList<SymbolInfo> infos;
    infos.reserve(m_symbols.size());
    for (auto it = m_symbols.cbegin(); it != m_symbols.cend(); ++it) {
        SymbolInfo info;
        info.name = it.key();
        info.category = it.value().category();
        info.description = it.value().description();
        info.tags = it.value().customField("__searchTags");
        info.library = m_name;
        info.libraryPath = m_path;
        info.builtIn = m_builtIn;
        info.stub = it.value().isStub();
        infos.append(info);
    }
    return infos;
}

QList<SymbolDefinition> SymbolLibrary::allSymbols() const {
    ensureLoaded();
    QReadLocker locker(&m_lock);
    return m_symbols.values();
}

void SymbolLibrary::ensureLoaded() const {
    QWriteLocker locker(&m_lock);
    bool hasStubs = false;
    for (const SymbolDefinition& sym : m_symbols) {
        if (sym.isStub()) { hasStubs = true; break; }
    }
    if (!hasStubs) return;

    if (m_path.endsWith(".kicad_sym", Qt::CaseInsensitive)) {
        QMap<QString, SymbolDefinition> loaded = KicadSymbolImporter::importLibrary(m_path);
        for (auto it = loaded.begin(); it != loaded.end(); ++it) {
            if (m_symbols.contains(it.key())) {
                SymbolDefinition& sym = m_symbols[it.key()];
                QString originalCategory = sym.category();
                sym = it.value();
                sym.setStub(false);
                if (sym.category().isEmpty()) sym.setCategory(originalCategory);
                normalizeExternalSymbolPinGrid(sym);
            }
        }
    }
}

QStringList SymbolLibrary::categories() const {
    ensureLoaded();
    QReadLocker locker(&m_lock);
    QSet<QString> cats;
    for (const SymbolDefinition& sym : m_symbols) {
        if (!sym.category().isEmpty()) {
            cats.insert(sym.category());
        }
    }
    return cats.values();
}

QList<SymbolDefinition*> SymbolLibrary::symbolsInCategory(const QString& category) {
    ensureLoaded();
    QList<SymbolDefinition*> result;
    for (auto it = m_symbols.begin(); it != m_symbols.end(); ++it) {
        if (it.value().category() == category) {
            result.append(&it.value());
        }
    }
    return result;
}

bool SymbolLibrary::load(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open library file:" << filePath;
        return false;
    }
    
    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    
    if (!doc.isObject()) {
        qWarning() << "Invalid library file format:" << filePath;
        return false;
    }
    
    QJsonObject root = doc.object();
    QJsonObject libObj = root["library"].toObject();
    
    QWriteLocker locker(&m_lock);
    m_name = libObj["name"].toString();
    m_path = filePath;
    m_symbols.clear();
    
    QJsonArray symbolsArr = libObj["symbols"].toArray();
    for (const QJsonValue& val : symbolsArr) {
        SymbolDefinition sym = SymbolDefinition::fromJson(val.toObject());
        m_symbols[sym.name()] = sym;
    }
    locker.unlock();
    
    qDebug() << "Loaded library:" << m_name << "with" << m_symbols.size() << "symbols";
    return true;
}

bool SymbolLibrary::save(const QString& filePath) {
    QString path = filePath.isEmpty() ? m_path : filePath;
    if (path.isEmpty()) {
        qWarning() << "No path specified for saving library";
        return false;
    }
    
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open file for writing:" << path;
        return false;
    }
    
    QJsonDocument doc(toJson());
    file.write(doc.toJson(QJsonDocument::Indented));
    m_path = path;
    
    qDebug() << "Saved library:" << m_name << "to" << path;
    return true;
}

QJsonObject SymbolLibrary::toJson() const {
    QJsonObject libObj;
    libObj["name"] = m_name;
    libObj["version"] = "1.0";
    
    QJsonArray symbolsArr;
    for (const SymbolDefinition& sym : m_symbols) {
        symbolsArr.append(sym.toJson());
    }
    libObj["symbols"] = symbolsArr;
    
    QJsonObject root;
    root["library"] = libObj;
    return root;
}

SymbolLibrary* SymbolLibrary::fromJson(const QJsonObject& json) {
    QJsonObject libObj = json["library"].toObject();
    SymbolLibrary* lib = new SymbolLibrary(libObj["name"].toString());
    
    QJsonArray symbolsArr = libObj["symbols"].toArray();
    for (const QJsonValue& val : symbolsArr) {
        SymbolDefinition sym = SymbolDefinition::fromJson(val.toObject());
        lib->addSymbol(sym);
    }
    
    return lib;
}

// ============ SymbolLibraryManager ============

SymbolLibraryManager& SymbolLibraryManager::instance() {
    static SymbolLibraryManager inst;
    return inst;
}

SymbolLibraryManager::SymbolLibraryManager() : QObject(nullptr) {
    createDefaultBuiltInLibrary();
}

SymbolLibraryManager::~SymbolLibraryManager() {
    QWriteLocker locker(&m_lock); // Modifies m_libraries
    qDeleteAll(m_libraries);
    m_libraries.clear(); // Ensure list is empty after deleting elements
}

void SymbolLibraryManager::addLibrary(SymbolLibrary* library) {
    QWriteLocker locker(&m_lock); // Modifies m_libraries
    if (library && !m_libraries.contains(library)) {
        m_libraries.append(library);
    }
}

void SymbolLibraryManager::removeLibrary(const QString& name) {
    QWriteLocker locker(&m_lock); // Modifies m_libraries
    for (int i = 0; i < m_libraries.size(); ++i) {
        if (m_libraries[i]->name() == name) {
            delete m_libraries.takeAt(i);
            break;
        }
    }
}

SymbolLibrary* SymbolLibraryManager::findLibrary(const QString& name) {
    QReadLocker locker(&m_lock);
    for (SymbolLibrary* lib : m_libraries) {
        if (lib->name() == name) return lib;
    }
    return nullptr;
}

QList<SymbolLibrary*> SymbolLibraryManager::libraries() const {
    QReadLocker locker(&m_lock);
    return m_libraries;
}

SymbolDefinition* SymbolLibraryManager::findSymbol(const QString& name) {
    // Prefer user libraries over built-in ones when names collide.
    QReadLocker locker(&m_lock);
    for (SymbolLibrary* lib : m_libraries) {
        if (SymbolDefinition* sym = lib->findSymbol(name)) return sym;
    }
    return nullptr;
}

SymbolDefinition* SymbolLibraryManager::findSymbol(const QString& name, const QString& libraryName) {
    QReadLocker locker(&m_lock);
    for (SymbolLibrary* lib : m_libraries) {
        if (lib->name() == libraryName) return lib->findSymbol(name);
    }
    return nullptr;
}

QList<SymbolDefinition*> SymbolLibraryManager::search(const QString& query) {
    QReadLocker locker(&m_lock);
    QList<SymbolDefinition*> results;
    QString q = query.toLower();

    for (int i = 0; i < m_libraries.size(); ++i) {
        SymbolLibrary* lib = m_libraries[i];
        QStringList names = lib->symbolNames();
        for (const QString& name : names) {
            SymbolDefinition* sym = lib->findSymbol(name);
            if (sym && (sym->name().toLower().contains(q) ||
                        sym->description().toLower().contains(q) ||
                        sym->category().toLower().contains(q))) {
                results.append(sym);
            }
        }
    }
    return results;
}

QList<SymbolLibrary::SymbolInfo> SymbolLibraryManager::searchMetadata(const QString& query) const {
    const QString q = query.trimmed().toLower();
    QList<SymbolLibrary::SymbolInfo> results;
    if (q.isEmpty()) return results;

    QReadLocker locker(&m_lock);
    for (SymbolLibrary* lib : m_libraries) {
        if (!lib) continue;
        const QList<SymbolLibrary::SymbolInfo> infos = lib->symbolInfos();
        for (const SymbolLibrary::SymbolInfo& info : infos) {
            if (info.name.toLower().contains(q) ||
                info.description.toLower().contains(q) ||
                info.category.toLower().contains(q) ||
                info.tags.toLower().contains(q)) {
                results.append(info);
            }
        }
    }
    return results;
}

void SymbolLibraryManager::loadBuiltInLibrary() {
    //LibraryIndex::instance().initialize();
    
    // Load from embedded resources
    SymbolLibrary* builtin = new SymbolLibrary("Built-in Standard", true);
    if (builtin->load(":/library/builtin.sclib")) {
        ensureBuiltInDefaultFootprints(builtin);
        addLibrary(builtin);
        for (const QString& name : builtin->symbolNames()) {
            SymbolDefinition* sym = builtin->findSymbol(name);
            //LibraryIndex::instance().addSymbol(name, builtin->name(), sym ? sym->category() : "");
        }
    } else {
        delete builtin;
        qWarning() << "Failed to load built-in symbols from resources!";
        // Fallback to legacy generator if resource load fails
        createDefaultBuiltInLibrary();
    }
}

#include "../../core/config_manager.h"
#include <QDirIterator>
#include <QCoreApplication>

void SymbolLibraryManager::loadUserLibraries(const QString& userLibPath, bool asyncColdIndexing) {
    Q_UNUSED(userLibPath); // We now use ConfigManager paths + default path
    SymbolMetadataCache metadataCache;
    quint64 loadGeneration = 0;
    {
        QWriteLocker locker(&m_lock);
        loadGeneration = ++m_loadGeneration;
    }
    bool didChangeLibraries = false;

    QStringList paths = ConfigManager::instance().symbolPaths();
    
    // Add default user path if not present
    QString defaultPath = QDir::homePath() + "/ViospiceLib/sym";
    if (!paths.contains(defaultPath)) paths.append(defaultPath);

    QString appDir = QCoreApplication::applicationDirPath();
    const QStringList bundledRoots = {
        QDir(appDir).absoluteFilePath("viospicelib"),
        QDir(appDir).absoluteFilePath("../viospicelib")
    };
    auto normalizedPath = [](const QString& p) {
        return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(p).absoluteFilePath()));
    };
    auto isUnderRoot = [&](const QString& p, const QString& root) {
        const QString np = normalizedPath(p);
        QString nr = normalizedPath(root);
        if (!nr.endsWith('/')) nr += '/';
        return np.startsWith(nr);
    };
    auto isBundledKicadPath = [&](const QString& p) {
        const QString np = normalizedPath(p).toLower();
        if (!np.contains("/symbols/kicad")) return false;
        for (const QString& root : bundledRoots) {
            if (isUnderRoot(p, root)) return true;
        }
        return false;
    };

    bool skipKicad = ConfigManager::instance().kicadDisabled();
    const bool basicsOnly = ConfigManager::instance().kicadBasicsOnly();

    auto ensureDefaultVoltageSourceSymbol = [](const QString& baseDir) {
        QDir dir(baseDir);
        if (!dir.exists() && !dir.mkpath(".")) return;

        const QString filePath = dir.filePath("Voltage_Source_DC.viosym");
        bool shouldWrite = !QFile::exists(filePath);
        if (!shouldWrite) {
            QFile inFile(filePath);
            if (inFile.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(inFile.readAll());
                if (doc.isObject()) {
                    QJsonObject obj = doc.object();
                    const QString name = obj.value("name").toString();
                    const QString desc = obj.value("description").toString();
                    const QJsonObject fields = obj.value("customFields").toObject();
                    const QString gen = fields.value("generatedBy").toString();
                    if (name == "Voltage_Source_DC" &&
                        (gen == "viospice" || (gen.isEmpty() && desc == "Independent voltage source"))) {
                        shouldWrite = true;
                    }
                }
            }
        }
        if (!shouldWrite) return;

        SymbolDefinition sym("Voltage_Source_DC");
        sym.setDescription("Independent voltage source");
        sym.setCategory("Simulation");
        sym.setReferencePrefix("V");
        sym.setDefaultValue("5");
        sym.setCustomField("generatedBy", "viospice");
        sym.setCustomField("generatedVersion", "1");

        sym.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -45), QPointF(0, -22.5)));
        sym.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 22.5), QPointF(0, 45)));
        sym.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, 0), 22.5, false));
        SymbolPrimitive plus = SymbolPrimitive::createText("+", QPointF(0, -10), 12, QColor(Qt::black));
        plus.data["hAlign"] = "center";
        plus.data["vAlign"] = "center";
        sym.addPrimitive(plus);

        SymbolPrimitive minus = SymbolPrimitive::createText("-", QPointF(0, 10), 12, QColor(Qt::black));
        minus.data["hAlign"] = "center";
        minus.data["vAlign"] = "center";
        sym.addPrimitive(minus);

        SymbolPrimitive p1 = SymbolPrimitive::createPin(QPointF(0, -45), 1, "+", "Up", 0.0);
        p1.data["length"] = 0.0;
        p1.data["hideNum"] = true;
        p1.data["hideName"] = true;
        sym.addPrimitive(p1);

        SymbolPrimitive p2 = SymbolPrimitive::createPin(QPointF(0, 45), 2, "-", "Down", 0.0);
        p2.data["length"] = 0.0;
        p2.data["hideNum"] = true;
        p2.data["hideName"] = true;
        sym.addPrimitive(p2);

        QJsonDocument doc(sym.toJson());
        QFile file(filePath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write(doc.toJson(QJsonDocument::Indented));
        }
    };

    auto ensureDefaultNmos4Symbol = [](const QString& baseDir) {
        QDir dir(baseDir);
        if (!dir.exists() && !dir.mkpath(".")) return;

        const QString filePath = dir.filePath("nmos4.viosym");
        bool shouldWrite = !QFile::exists(filePath);
        if (!shouldWrite) {
            QFile inFile(filePath);
            if (inFile.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(inFile.readAll());
                if (doc.isObject()) {
                    const QJsonObject obj = doc.object();
                    const QString name = obj.value("name").toString().trimmed();
                    const QString generatedBy = obj.value("customFields").toObject().value("generatedBy").toString();
                    if (name.compare("nmos4", Qt::CaseInsensitive) == 0 && generatedBy == "viospice") {
                        shouldWrite = true;
                    }
                }
            }
        }
        if (!shouldWrite) return;

        SymbolDefinition sym("nmos4");
        sym.setDescription("LTspice-style 4-terminal NMOS");
        sym.setCategory("Semiconductors");
        sym.setReferencePrefix("M");
        sym.setDefaultValue("2N7000");
        sym.setCustomField("generatedBy", "viospice");
        sym.setCustomField("generatedVersion", "1");

        sym.addPrimitive(SymbolPrimitive::createLine(QPointF(-12, -24), QPointF(-12, 24)));
        sym.addPrimitive(SymbolPrimitive::createLine(QPointF(6, -18), QPointF(6, -6)));
        sym.addPrimitive(SymbolPrimitive::createLine(QPointF(6, 6), QPointF(6, 18)));
        sym.addPrimitive(SymbolPrimitive::createLine(QPointF(6, -6), QPointF(6, 6)));
        sym.addPrimitive(SymbolPrimitive::createLine(QPointF(6, -18), QPointF(18, -18)));
        sym.addPrimitive(SymbolPrimitive::createLine(QPointF(6, 18), QPointF(18, 18)));
        sym.addPrimitive(SymbolPrimitive::createLine(QPointF(6, 0), QPointF(18, 0)));

        QList<QPointF> arrow = {QPointF(14, 0), QPointF(9.5, -2.2), QPointF(9.5, 2.2)};
        sym.addPrimitive(SymbolPrimitive::createPolygon(arrow, true));

        SymbolPrimitive gPin = SymbolPrimitive::createPin(QPointF(-30, 0), 1, "G");
        gPin.data["length"] = 18.0;
        gPin.data["orientation"] = "Right";
        sym.addPrimitive(gPin);

        SymbolPrimitive dPin = SymbolPrimitive::createPin(QPointF(18, -30), 2, "D");
        dPin.data["length"] = 12.0;
        dPin.data["orientation"] = "Down";
        sym.addPrimitive(dPin);

        SymbolPrimitive sPin = SymbolPrimitive::createPin(QPointF(18, 30), 3, "S");
        sPin.data["length"] = 12.0;
        sPin.data["orientation"] = "Up";
        sym.addPrimitive(sPin);

        SymbolPrimitive bPin = SymbolPrimitive::createPin(QPointF(36, 0), 4, "B");
        bPin.data["length"] = 18.0;
        bPin.data["orientation"] = "Left";
        sym.addPrimitive(bPin);

        QMap<int, QString> mosMapping;
        mosMapping[1] = "D";
        mosMapping[2] = "G";
        mosMapping[3] = "S";
        mosMapping[4] = "B";
        sym.setSpiceNodeMapping(mosMapping);

        QJsonDocument doc(sym.toJson());
        QFile file(filePath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write(doc.toJson(QJsonDocument::Indented));
        }
    };

    auto ensureDefaultPmos4Symbol = [](const QString& baseDir) {
        QDir dir(baseDir);
        if (!dir.exists() && !dir.mkpath(".")) return;

        const QString filePath = dir.filePath("pmos4.viosym");
        bool shouldWrite = !QFile::exists(filePath);
        if (!shouldWrite) {
            QFile inFile(filePath);
            if (inFile.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(inFile.readAll());
                if (doc.isObject()) {
                    const QJsonObject obj = doc.object();
                    const QString name = obj.value("name").toString().trimmed();
                    const QString generatedBy = obj.value("customFields").toObject().value("generatedBy").toString();
                    if (name.compare("pmos4", Qt::CaseInsensitive) == 0 && generatedBy == "viospice") {
                        shouldWrite = true;
                    }
                }
            }
        }
        if (!shouldWrite) return;

        SymbolDefinition sym("pmos4");
        sym.setDescription("LTspice-style 4-terminal PMOS");
        sym.setCategory("Semiconductors");
        sym.setReferencePrefix("M");
        sym.setDefaultValue("BS250");
        sym.setCustomField("generatedBy", "viospice");
        sym.setCustomField("generatedVersion", "1");

        sym.addPrimitive(SymbolPrimitive::createLine(QPointF(-12, -24), QPointF(-12, 24)));
        sym.addPrimitive(SymbolPrimitive::createLine(QPointF(6, -18), QPointF(6, -6)));
        sym.addPrimitive(SymbolPrimitive::createLine(QPointF(6, 6), QPointF(6, 18)));
        sym.addPrimitive(SymbolPrimitive::createLine(QPointF(6, -6), QPointF(6, 6)));
        sym.addPrimitive(SymbolPrimitive::createLine(QPointF(6, -18), QPointF(18, -18)));
        sym.addPrimitive(SymbolPrimitive::createLine(QPointF(6, 18), QPointF(18, 18)));
        sym.addPrimitive(SymbolPrimitive::createLine(QPointF(6, 0), QPointF(18, 0)));

        QList<QPointF> arrow = {QPointF(10.5, 0), QPointF(15.0, -2.2), QPointF(15.0, 2.2)};
        sym.addPrimitive(SymbolPrimitive::createPolygon(arrow, true));

        SymbolPrimitive gPin = SymbolPrimitive::createPin(QPointF(-30, 0), 1, "G");
        gPin.data["length"] = 18.0;
        gPin.data["orientation"] = "Right";
        sym.addPrimitive(gPin);

        SymbolPrimitive dPin = SymbolPrimitive::createPin(QPointF(18, -30), 2, "D");
        dPin.data["length"] = 12.0;
        dPin.data["orientation"] = "Down";
        sym.addPrimitive(dPin);

        SymbolPrimitive sPin = SymbolPrimitive::createPin(QPointF(18, 30), 3, "S");
        sPin.data["length"] = 12.0;
        sPin.data["orientation"] = "Up";
        sym.addPrimitive(sPin);

        SymbolPrimitive bPin = SymbolPrimitive::createPin(QPointF(36, 0), 4, "B");
        bPin.data["length"] = 18.0;
        bPin.data["orientation"] = "Left";
        sym.addPrimitive(bPin);

        QMap<int, QString> mosMapping;
        mosMapping[1] = "D";
        mosMapping[2] = "G";
        mosMapping[3] = "S";
        mosMapping[4] = "B";
        sym.setSpiceNodeMapping(mosMapping);

        QJsonDocument doc(sym.toJson());
        QFile file(filePath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write(doc.toJson(QJsonDocument::Indented));
        }
    };

    auto ensureDefaultNpn4Symbol = [](const QString& baseDir) {
        QDir dir(baseDir);
        if (!dir.exists() && !dir.mkpath(".")) return;

        const QString filePath = dir.filePath("npn4.viosym");
        bool shouldWrite = !QFile::exists(filePath);
        if (!shouldWrite) {
            QFile inFile(filePath);
            if (inFile.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(inFile.readAll());
                if (doc.isObject()) {
                    const QJsonObject obj = doc.object();
                    const QString name = obj.value("name").toString().trimmed();
                    const QString generatedBy = obj.value("customFields").toObject().value("generatedBy").toString();
                    if (name.compare("npn4", Qt::CaseInsensitive) == 0 && generatedBy == "viospice") {
                        shouldWrite = true;
                    }
                }
            }
        }
        if (!shouldWrite) return;

        SymbolDefinition sym("npn4");
        sym.setDescription("LTspice-style 4-terminal NPN BJT");
        sym.setCategory("Semiconductors");
        sym.setReferencePrefix("Q");
        sym.setDefaultValue("2N2222");
        sym.setCustomField("generatedBy", "viospice");
        sym.setCustomField("generatedVersion", "1");

        sym.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, 0), 18.0, false));
        sym.addPrimitive(SymbolPrimitive::createLine(QPointF(-5, -6), QPointF(-5, 6)));
        sym.addPrimitive(SymbolPrimitive::createLine(QPointF(-5, 0), QPointF(-22, 0)));
        sym.addPrimitive(SymbolPrimitive::createLine(QPointF(-5, -4), QPointF(10, -16)));
        sym.addPrimitive(SymbolPrimitive::createLine(QPointF(-5, 4), QPointF(10, 16)));

        QList<QPointF> arrow = {QPointF(7.5, 12.5), QPointF(12.5, 11.0), QPointF(9.5, 16.0)};
        sym.addPrimitive(SymbolPrimitive::createPolygon(arrow, true));

        SymbolPrimitive cPin = SymbolPrimitive::createPin(QPointF(14, -28), 1, "C");
        cPin.data["length"] = 12.0;
        cPin.data["orientation"] = "Down";
        sym.addPrimitive(cPin);

        SymbolPrimitive bPin = SymbolPrimitive::createPin(QPointF(-34, 0), 2, "B");
        bPin.data["length"] = 12.0;
        bPin.data["orientation"] = "Right";
        sym.addPrimitive(bPin);

        SymbolPrimitive ePin = SymbolPrimitive::createPin(QPointF(14, 28), 3, "E");
        ePin.data["length"] = 12.0;
        ePin.data["orientation"] = "Up";
        sym.addPrimitive(ePin);

        SymbolPrimitive sPin = SymbolPrimitive::createPin(QPointF(34, 0), 4, "S");
        sPin.data["length"] = 12.0;
        sPin.data["orientation"] = "Left";
        sym.addPrimitive(sPin);

        QMap<int, QString> bjtMapping;
        bjtMapping[1] = "C";
        bjtMapping[2] = "B";
        bjtMapping[3] = "E";
        bjtMapping[4] = "S";
        sym.setSpiceNodeMapping(bjtMapping);

        QJsonDocument doc(sym.toJson());
        QFile file(filePath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write(doc.toJson(QJsonDocument::Indented));
        }
    };

    auto ensureDefaultPnp4Symbol = [](const QString& baseDir) {
        QDir dir(baseDir);
        if (!dir.exists() && !dir.mkpath(".")) return;

        const QString filePath = dir.filePath("pnp4.viosym");
        bool shouldWrite = !QFile::exists(filePath);
        if (!shouldWrite) {
            QFile inFile(filePath);
            if (inFile.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(inFile.readAll());
                if (doc.isObject()) {
                    const QJsonObject obj = doc.object();
                    const QString name = obj.value("name").toString().trimmed();
                    const QString generatedBy = obj.value("customFields").toObject().value("generatedBy").toString();
                    if (name.compare("pnp4", Qt::CaseInsensitive) == 0 && generatedBy == "viospice") {
                        shouldWrite = true;
                    }
                }
            }
        }
        if (!shouldWrite) return;

        SymbolDefinition sym("pnp4");
        sym.setDescription("LTspice-style 4-terminal PNP BJT");
        sym.setCategory("Semiconductors");
        sym.setReferencePrefix("Q");
        sym.setDefaultValue("2N3906");
        sym.setCustomField("generatedBy", "viospice");
        sym.setCustomField("generatedVersion", "1");

        sym.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, 0), 18.0, false));
        sym.addPrimitive(SymbolPrimitive::createLine(QPointF(-5, -6), QPointF(-5, 6)));
        sym.addPrimitive(SymbolPrimitive::createLine(QPointF(-5, 0), QPointF(-22, 0)));
        sym.addPrimitive(SymbolPrimitive::createLine(QPointF(-5, -4), QPointF(10, -16)));
        sym.addPrimitive(SymbolPrimitive::createLine(QPointF(-5, 4), QPointF(10, 16)));

        QList<QPointF> arrow = {QPointF(11.5, 18.0), QPointF(8.5, 13.0), QPointF(13.5, 14.5)};
        sym.addPrimitive(SymbolPrimitive::createPolygon(arrow, true));

        SymbolPrimitive cPin = SymbolPrimitive::createPin(QPointF(14, -28), 1, "C");
        cPin.data["length"] = 12.0;
        cPin.data["orientation"] = "Down";
        sym.addPrimitive(cPin);

        SymbolPrimitive bPin = SymbolPrimitive::createPin(QPointF(-34, 0), 2, "B");
        bPin.data["length"] = 12.0;
        bPin.data["orientation"] = "Right";
        sym.addPrimitive(bPin);

        SymbolPrimitive ePin = SymbolPrimitive::createPin(QPointF(14, 28), 3, "E");
        ePin.data["length"] = 12.0;
        ePin.data["orientation"] = "Up";
        sym.addPrimitive(ePin);

        SymbolPrimitive sPin = SymbolPrimitive::createPin(QPointF(34, 0), 4, "S");
        sPin.data["length"] = 12.0;
        sPin.data["orientation"] = "Left";
        sym.addPrimitive(sPin);

        QMap<int, QString> bjtMapping;
        bjtMapping[1] = "C";
        bjtMapping[2] = "B";
        bjtMapping[3] = "E";
        bjtMapping[4] = "S";
        sym.setSpiceNodeMapping(bjtMapping);

        QJsonDocument doc(sym.toJson());
        QFile file(filePath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write(doc.toJson(QJsonDocument::Indented));
        }
    };
    ensureDefaultVoltageSourceSymbol(defaultPath);
    ensureDefaultNmos4Symbol(defaultPath);
    ensureDefaultPmos4Symbol(defaultPath);
    ensureDefaultNpn4Symbol(defaultPath);
    ensureDefaultPnp4Symbol(defaultPath);

    QMap<QString, SymbolLibrary*> looseLibs;
    QStringList deferredKicadFiles;
    auto shouldIncludeBasicsLibrary = [&](const QString& libName) {
        if (!basicsOnly) return true;
        static const QSet<QString> basics = {
            "Device", "Connector", "Connector_Generic", "Connector_Audio",
            "power", "Simulation_SPICE",
            "Diode", "Diode_Bridge", "Rectifier",
            "Transistor_BJT", "Transistor_FET", "Transistor_IGBT",
            "Amplifier_Operational", "Amplifier_Audio", "Amplifier_Buffer",
            "Amplifier_Instrumentation", "Amplifier_Difference", "Amplifier_Video",
            "Reference_Voltage", "Regulator_Linear", "Regulator_Switching",
            "Switch", "Jumper", "Logic_74xx", "Logic_4xxx", "analog_sw"
        };
        return basics.contains(libName);
    };

    for (const QString& path : paths) {
        if (isBundledKicadPath(path)) {
            qDebug() << "Skipping bundled KiCad path:" << path;
            continue;
        }
        QDir dir(path);
        if (!dir.exists()) continue;

        QDirIterator it(path, QStringList() << "*.sclib", QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString filePath = it.next();
            
            // Check if already loaded to avoid duplicates
            bool loaded = false;
            for (SymbolLibrary* lib : m_libraries) {
                if (lib->path() == filePath) { loaded = true; break; }
            }
            if (loaded) continue;

            SymbolLibrary* lib = new SymbolLibrary();
            if (lib->load(filePath)) {
                for (const QString& name : lib->symbolNames()) {
                    SymbolDefinition* sym = lib->findSymbol(name);
                    if (sym) normalizeExternalSymbolPinGrid(*sym);
                }
                addLibrary(lib);
                didChangeLibraries = true;
                // Index symbols
                for (const QString& name : lib->symbolNames()) {
                    SymbolDefinition* sym = lib->findSymbol(name);
                    //LibraryIndex::instance().addSymbol(name, lib->name(), sym ? sym->category() : "");
                }
                qDebug() << "Loaded external library:" << filePath;
            } else {
                delete lib;
                qWarning() << "Failed to load library:" << filePath;
            }
        }

        QDirIterator symIt(path, QStringList() << "*.viosym", QDir::Files, QDirIterator::Subdirectories);
        while (symIt.hasNext()) {
            QString filePath = symIt.next();
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly)) continue;
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (!doc.isObject()) continue;
            QJsonObject obj = doc.object();
            if (obj.contains("library")) continue;
            SymbolDefinition sym = SymbolDefinition::fromJson(obj);
            normalizeExternalSymbolPinGrid(sym);
            if (sym.name().trimmed().isEmpty()) {
                sym.setName(QFileInfo(filePath).completeBaseName());
            }

            // If the .viosym is inside a subdirectory, use that directory name as category
            const QString parentDir = QFileInfo(filePath).absolutePath();
            if (parentDir != path) {
                sym.setCategory(QFileInfo(parentDir).fileName());
            }

            SymbolLibrary* lib = nullptr;
            const QString libKey = parentDir;
            if (looseLibs.contains(libKey)) {
                lib = looseLibs.value(libKey);
            } else {
                // Use directory name as library name (no "Symbols:" prefix for subdirs)
                const QString dirName = QFileInfo(libKey).fileName();
                const QString libName = dirName;
                lib = new SymbolLibrary(libName, false);
                lib->setPath(libKey);
                addLibrary(lib);
                didChangeLibraries = true;
                looseLibs.insert(libKey, lib);
            }
            lib->addSymbol(sym);
        }

        if (!skipKicad) {
            QDirIterator countIt(path, QStringList() << "*.kicad_sym", QDir::Files, QDirIterator::Subdirectories);
            int totalKicad = 0;
            while (countIt.hasNext()) { countIt.next(); totalKicad++; }

            QDirIterator kicadIt(path, QStringList() << "*.kicad_sym", QDir::Files, QDirIterator::Subdirectories);
            int currentKicad = 0;
            while (kicadIt.hasNext()) {
                QString filePath = kicadIt.next();
                if (isBundledKicadPath(filePath)) {
                    continue;
                }
                currentKicad++;
                
                const QString libName = QFileInfo(filePath).completeBaseName();

                if (!shouldIncludeBasicsLibrary(libName)) continue;

                Q_EMIT progressUpdated(QString("Loading KiCad library: %1").arg(libName), currentKicad, totalKicad);

                // Check if already loaded...
                bool alreadyLoaded = false;
                for (SymbolLibrary* lib : m_libraries) {
                    if (lib->path() == filePath) { alreadyLoaded = true; break; }
                }
                if (alreadyLoaded) continue;

                SymbolLibrary* lib = new SymbolLibrary(libName, false);
                lib->setPath(filePath);

                const QFileInfo fileInfo(filePath);
                QList<SymbolLibrary::SymbolInfo> symbolInfos = metadataCache.symbolInfosForFile(fileInfo);
                if (symbolInfos.isEmpty()) {
                    if (asyncColdIndexing) {
                        deferredKicadFiles.append(filePath);
                        continue;
                    }

                    const QList<KicadSymbolImporter::SymbolMetadata> extracted =
                        KicadSymbolImporter::getSymbolMetadata(filePath);
                    symbolInfos.reserve(extracted.size());
                    for (const KicadSymbolImporter::SymbolMetadata& meta : extracted) {
                        SymbolLibrary::SymbolInfo info;
                        info.name = meta.name;
                        info.description = meta.description;
                        info.tags = meta.keywords;
                        symbolInfos.append(info);
                    }
                    if (!symbolInfos.isEmpty()) {
                        metadataCache.storeSymbolInfos(fileInfo, symbolInfos);
                    }
                }
                for (const SymbolLibrary::SymbolInfo& info : symbolInfos) {
                    SymbolDefinition stub(info.name);
                    stub.setStub(true);
                    stub.setCategory(libName); // Default category is library name
                    if (!info.description.isEmpty()) stub.setDescription(info.description);
                    if (!info.tags.isEmpty()) stub.setCustomField("__searchTags", info.tags);
                    lib->addSymbol(stub);
                }
                if (lib->symbolCount() > 0) {
                    addLibrary(lib);
                    didChangeLibraries = true;
                    qDebug() << "Loaded KiCad library:" << filePath << "with" << lib->symbolCount() << "symbols";
                } else {
                    delete lib;
                }
            }
        }
    }

    metadataCache.saveIfDirty();
    if (didChangeLibraries) Q_EMIT librariesChanged();
    if (deferredKicadFiles.isEmpty()) {
        Q_EMIT loadingFinished();
        return;
    }

    [[maybe_unused]] auto future = QtConcurrent::run([this, deferredKicadFiles, loadGeneration]() {
        SymbolMetadataCache asyncCache;
        QList<SymbolLibrary*> loadedLibraries;
        loadedLibraries.reserve(deferredKicadFiles.size());

        for (int i = 0; i < deferredKicadFiles.size(); ++i) {
            if (!isLoadGenerationCurrent(loadGeneration)) {
                qDeleteAll(loadedLibraries);
                return;
            }

            const QString filePath = deferredKicadFiles.at(i);
            const QString libName = QFileInfo(filePath).completeBaseName();
            Q_EMIT progressUpdated(QString("Indexing KiCad library: %1").arg(libName), i + 1, deferredKicadFiles.size());

            const QList<KicadSymbolImporter::SymbolMetadata> extracted =
                KicadSymbolImporter::getSymbolMetadata(filePath);
            QList<SymbolLibrary::SymbolInfo> symbolInfos;
            symbolInfos.reserve(extracted.size());
            for (const KicadSymbolImporter::SymbolMetadata& meta : extracted) {
                SymbolLibrary::SymbolInfo info;
                info.name = meta.name;
                info.description = meta.description;
                info.tags = meta.keywords;
                symbolInfos.append(info);
            }
            if (symbolInfos.isEmpty()) continue;

            asyncCache.storeSymbolInfos(QFileInfo(filePath), symbolInfos);

            SymbolLibrary* lib = new SymbolLibrary(libName, false);
            lib->setPath(filePath);
            for (const SymbolLibrary::SymbolInfo& info : symbolInfos) {
                SymbolDefinition stub(info.name);
                stub.setStub(true);
                stub.setCategory(libName);
                if (!info.description.isEmpty()) stub.setDescription(info.description);
                if (!info.tags.isEmpty()) stub.setCustomField("__searchTags", info.tags);
                lib->addSymbol(stub);
            }
            if (lib->symbolCount() > 0) loadedLibraries.append(lib);
            else delete lib;
        }

        asyncCache.saveIfDirty();

        QMetaObject::invokeMethod(this, [this, loadGeneration, loadedLibraries]() mutable {
            if (!isLoadGenerationCurrent(loadGeneration)) {
                qDeleteAll(loadedLibraries);
                return;
            }

            bool changed = false;
            for (SymbolLibrary* lib : loadedLibraries) {
                bool exists = false;
                for (SymbolLibrary* existing : m_libraries) {
                    if (existing && existing->path() == lib->path()) {
                        exists = true;
                        break;
                    }
                }
                if (exists) {
                    delete lib;
                    continue;
                }
                addLibrary(lib);
                changed = true;
            }
            if (changed) Q_EMIT this->librariesChanged();
            Q_EMIT loadingFinished();
        }, Qt::QueuedConnection);
    });
}

void SymbolLibraryManager::reloadUserLibraries(bool asyncColdIndexing) {
    QWriteLocker locker(&m_lock);
    ++m_loadGeneration;
    for (int i = m_libraries.size() - 1; i >= 0; --i) {
        if (!m_libraries[i]->isBuiltIn()) {
            delete m_libraries.takeAt(i);
        }
    }
    locker.unlock();
    loadUserLibraries(QDir::homePath() + "/ViospiceLib/sym", asyncColdIndexing);
}

bool SymbolLibraryManager::isLoadGenerationCurrent(quint64 generation) const {
    QReadLocker locker(&m_lock);
    return m_loadGeneration == generation;
}

QStringList SymbolLibraryManager::allCategories() const {
    QReadLocker locker(&m_lock);
    QSet<QString> cats;
    for (SymbolLibrary* lib : m_libraries) {
        if (!lib) continue;
        const QList<SymbolLibrary::SymbolInfo> infos = lib->symbolInfos();
        for (const SymbolLibrary::SymbolInfo& info : infos) {
            if (!info.category.isEmpty()) {
                cats.insert(info.category);
            }
        }
    }
    return cats.values();
}

void SymbolLibraryManager::createDefaultBuiltInLibrary() {
    QMap<QString, SymbolLibrary*> catLibs;

    auto addSym = [&](const SymbolDefinition& sym) {
        QString cat = sym.category();
        if (cat.isEmpty()) cat = "Uncategorized";
        if (!catLibs.contains(cat)) {
            catLibs[cat] = new SymbolLibrary(cat, true);
        }
        catLibs[cat]->addSymbol(sym);
    };

    // === Resistor ===
    SymbolDefinition resistor("Resistor");
    resistor.setCategory("Passives");
    resistor.setReferencePrefix("R");
    resistor.setDescription("Standard resistor symbol");
    resistor.addPrimitive(SymbolPrimitive::createRect(QRectF(-30, -8, 60, 16), false));
    resistor.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 0), 1, "1"));
    resistor.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 0), 2, "2"));
    // Pins default to Right orientation. Pin 1 (at -45) goes to -30 (edge).
    // Pin 2 (at 45) needs to go to 30. So it needs Left orientation.
    resistor.primitives().last().data["orientation"] = "Left";
    resistor.primitives().last().data["length"] = 15.0;
    resistor.primitives()[resistor.primitives().size()-2].data["length"] = 15.0;
    addSym(resistor);
    
    // === Capacitor ===
    SymbolDefinition capacitor("Capacitor");
    capacitor.setCategory("Passives");
    capacitor.setReferencePrefix("C");
    capacitor.setDescription("Standard capacitor symbol");
    capacitor.addPrimitive(SymbolPrimitive::createLine(QPointF(-5, -15), QPointF(-5, 15)));
    capacitor.addPrimitive(SymbolPrimitive::createLine(QPointF(5, -15), QPointF(5, 15)));
    capacitor.addPrimitive(SymbolPrimitive::createPin(QPointF(-40, 0), 1, "1"));
    capacitor.addPrimitive(SymbolPrimitive::createPin(QPointF(40, 0), 2, "2"));
    capacitor.primitives().last().data["orientation"] = "Left";
    capacitor.primitives().last().data["length"] = 35.0;
    capacitor.primitives()[capacitor.primitives().size()-2].data["length"] = 35.0;
    addSym(capacitor);
    
    // Inductor
    SymbolDefinition inductor("Inductor");
    inductor.setCategory("Passives");
    inductor.setReferencePrefix("L");
    inductor.setDescription("Standard inductor symbol");
    inductor.addPrimitive(SymbolPrimitive::createLine(QPointF(-40, 0), QPointF(-30, 0)));
    inductor.addPrimitive(SymbolPrimitive::createArc(QRectF(-30, -7.5, 15, 15), 0, 180));
    inductor.addPrimitive(SymbolPrimitive::createArc(QRectF(-15, -7.5, 15, 15), 0, 180));
    inductor.addPrimitive(SymbolPrimitive::createArc(QRectF(0, -7.5, 15, 15), 0, 180));
    inductor.addPrimitive(SymbolPrimitive::createArc(QRectF(15, -7.5, 15, 15), 0, 180));
    inductor.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 0), QPointF(40, 0)));
    inductor.addPrimitive(SymbolPrimitive::createPin(QPointF(-40, 0), 1, "1"));
    inductor.addPrimitive(SymbolPrimitive::createPin(QPointF(40, 0), 2, "2"));
    inductor.primitives()[inductor.primitives().size()-2].data["orientation"] = "Right";
    inductor.primitives().last().data["orientation"] = "Left";
    inductor.primitives()[inductor.primitives().size()-2].data["length"] = 10.0;
    inductor.primitives().last().data["length"] = 10.0;
    addSym(inductor);
    
    // Diode
    SymbolDefinition diode("Diode");
    diode.setCategory("Semiconductors");
    diode.setReferencePrefix("D");
    diode.setDescription("Standard diode symbol");
    QList<QPointF> triangle = {QPointF(-10, -12), QPointF(-10, 12), QPointF(10, 0)};
    diode.addPrimitive(SymbolPrimitive::createPolygon(triangle, true));
    diode.addPrimitive(SymbolPrimitive::createLine(QPointF(10, -12), QPointF(10, 12)));
    diode.addPrimitive(SymbolPrimitive::createPin(QPointF(-40, 0), 1, "A"));
    diode.addPrimitive(SymbolPrimitive::createPin(QPointF(40, 0), 2, "K"));
    diode.primitives().last().data["orientation"] = "Left";
    diode.primitives().last().data["length"] = 30.0;
    diode.primitives()[diode.primitives().size()-2].data["length"] = 30.0;
    addSym(diode);
    
    // NPN
    SymbolDefinition npn("NPN Transistor");
    npn.setCategory("Semiconductors");
    npn.setReferencePrefix("Q");
    npn.setDescription("NPN bipolar junction transistor");
    npn.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, 0), 20, false));
    npn.addPrimitive(SymbolPrimitive::createLine(QPointF(-10, -12), QPointF(-10, 12)));
    npn.addPrimitive(SymbolPrimitive::createLine(QPointF(-10, -8), QPointF(8, -18)));
    npn.addPrimitive(SymbolPrimitive::createLine(QPointF(-10, 8), QPointF(8, 18)));
    // Add emitter arrow for NPN (pointing outwards from base).
    QList<QPointF> npnArrow = {QPointF(8, 18), QPointF(1.4, 17.5), QPointF(4.1, 12.6)};
    npn.addPrimitive(SymbolPrimitive::createPolygon(npnArrow, true));
    SymbolPrimitive bPin = SymbolPrimitive::createPin(QPointF(-30, 0), 1, "B");
    bPin.data["length"] = 20.0;
    bPin.data["orientation"] = "Right";
    npn.addPrimitive(bPin);
    SymbolPrimitive cPin = SymbolPrimitive::createPin(QPointF(8, -30), 2, "C");
    cPin.data["length"] = 12.0;
    cPin.data["orientation"] = "Down";
    npn.addPrimitive(cPin);
    SymbolPrimitive ePin = SymbolPrimitive::createPin(QPointF(8, 30), 3, "E");
    ePin.data["length"] = 12.0;
    ePin.data["orientation"] = "Up";
    npn.addPrimitive(ePin);
    
    // SPICE mapping: 1:C, 2:B, 3:E (Standard ngspice order)
    QMap<int, QString> npnMapping;
    npnMapping[1] = "C";
    npnMapping[2] = "B";
    npnMapping[3] = "E";
    npnMapping[4] = "1"; // For multi-pin variants if any
    npn.setSpiceNodeMapping(npnMapping);

    addSym(npn);
    
    // IC
    SymbolDefinition ic("IC");
    ic.setCategory("Integrated Circuits");
    ic.setReferencePrefix("U");
    ic.setDescription("Generic 8-pin IC");
    ic.addPrimitive(SymbolPrimitive::createRect(QRectF(-30, -40, 60, 80), false));
    for (int i = 0; i < 4; ++i) {
        qreal y = -30 + i * 20;
        SymbolPrimitive pin = SymbolPrimitive::createPin(QPointF(-45, y), i + 1, QString::number(i + 1));
        pin.data["length"] = 15.0;
        pin.data["orientation"] = "Right";
        ic.addPrimitive(pin);
    }
    for (int i = 0; i < 4; ++i) {
        qreal y = 30 - i * 20;
        SymbolPrimitive pin = SymbolPrimitive::createPin(QPointF(45, y), 5 + i, QString::number(5 + i));
        pin.data["length"] = 15.0;
        pin.data["orientation"] = "Left";
        ic.addPrimitive(pin);
    }
    // === Capacitor (Polarized) ===
    SymbolDefinition capPolar("Capacitor_Polarized");
    capPolar.setCategory("Passives");
    capPolar.setReferencePrefix("C");
    capPolar.addPrimitive(SymbolPrimitive::createLine(QPointF(-5, -15), QPointF(-5, 15)));
    capPolar.addPrimitive(SymbolPrimitive::createArc(QRectF(2, -15, 6, 30), 90, 180));
    capPolar.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, -10), QPointF(-8, -10))); // Plus sign
    capPolar.addPrimitive(SymbolPrimitive::createLine(QPointF(-11.5, -13.5), QPointF(-11.5, -6.5)));
    capPolar.addPrimitive(SymbolPrimitive::createPin(QPointF(-40, 0), 1, "1"));
    capPolar.addPrimitive(SymbolPrimitive::createPin(QPointF(40, 0), 2, "2"));
    addSym(capPolar);

    // === PNP ===
    SymbolDefinition pnp("Transistor_PNP");
    pnp.setCategory("Semiconductors");
    pnp.setReferencePrefix("Q");
    pnp.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, 0), 20, false));
    pnp.addPrimitive(SymbolPrimitive::createLine(QPointF(-10, -12), QPointF(-10, 12)));
    pnp.addPrimitive(SymbolPrimitive::createLine(QPointF(-10, -8), QPointF(8, -18)));
    pnp.addPrimitive(SymbolPrimitive::createLine(QPointF(-10, 8), QPointF(8, 18)));
    // Add arrow for PNP (pointing towards base)
    QList<QPointF> arrow = {QPointF(2.8, 15.1), QPointF(4.0, 19.0), QPointF(6.7, 14.1)};
    pnp.addPrimitive(SymbolPrimitive::createPolygon(arrow, true));
    pnp.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, 0), 1, "B"));
    pnp.addPrimitive(SymbolPrimitive::createPin(QPointF(8, -30), 2, "C"));
    pnp.addPrimitive(SymbolPrimitive::createPin(QPointF(8, 30), 3, "E"));

    // SPICE mapping: 1:C, 2:B, 3:E
    QMap<int, QString> pnpMapping;
    pnpMapping[1] = "C";
    pnpMapping[2] = "B";
    pnpMapping[3] = "E";
    pnp.setSpiceNodeMapping(pnpMapping);

    addSym(pnp);

    // === MOSFETs ===
    addSym(buildDefaultMosfetSymbol("Transistor_NMOS", true));
    addSym(buildDefaultMosfetSymbol("Transistor_PMOS", false));

    // === Power Markers ===
    auto addPower = [&](const QString& name, bool isGnd) {
        SymbolDefinition pwr(name);
        pwr.setCategory("Power");
        pwr.setReferencePrefix("#PWR");
        if (isGnd) {
            pwr.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 0), QPointF(0, 10)));
            pwr.addPrimitive(SymbolPrimitive::createLine(QPointF(-10, 10), QPointF(10, 10)));
            pwr.addPrimitive(SymbolPrimitive::createLine(QPointF(-6, 14), QPointF(6, 14)));
            pwr.addPrimitive(SymbolPrimitive::createLine(QPointF(-2, 18), QPointF(2, 18)));
            pwr.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 0), 1, "GND"));
        } else {
            pwr.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 0), QPointF(0, -10)));
            pwr.addPrimitive(SymbolPrimitive::createLine(QPointF(-8, -10), QPointF(0, -20)));
            pwr.addPrimitive(SymbolPrimitive::createLine(QPointF(8, -10), QPointF(0, -20)));
            pwr.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 0), 1, name));
        }
        addSym(pwr);
    };
    addPower("GND", true);
    addPower("VCC", false);
    addPower("VDD", false);
    addPower("3.3V", false);
    addPower("5V", false);
    addPower("12V", false);

    // === Resistor Aliases/Styles ===
    SymbolDefinition resUS("Resistor_US");
    resUS.setCategory("Passives");
    resUS.setReferencePrefix("R");
    resUS.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 0), QPointF(-25, 10)));
    resUS.addPrimitive(SymbolPrimitive::createLine(QPointF(-25, 10), QPointF(-15, -10)));
    resUS.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, -10), QPointF(-5, 10)));
    resUS.addPrimitive(SymbolPrimitive::createLine(QPointF(-5, 10), QPointF(5, -10)));
    resUS.addPrimitive(SymbolPrimitive::createLine(QPointF(5, -10), QPointF(15, 10)));
    resUS.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 10), QPointF(25, -10)));
    resUS.addPrimitive(SymbolPrimitive::createLine(QPointF(25, -10), QPointF(30, 0)));
    resUS.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 0), 1, "1"));
    resUS.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 0), 2, "2"));
    addSym(resUS);

    SymbolDefinition resIEC("Resistor_IEC");
    resIEC.setCategory("Passives");
    resIEC.setReferencePrefix("R");
    resIEC.addPrimitive(SymbolPrimitive::createRect(QRectF(-30, -10, 60, 20), false));
    resIEC.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 0), 1, "1"));
    resIEC.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 0), 2, "2"));
    addSym(resIEC);

    // === Transformer ===
    SymbolDefinition trans("Transformer");
    trans.setCategory("Passives");
    trans.setReferencePrefix("T");
    // Primary
    trans.addPrimitive(SymbolPrimitive::createArc(QRectF(-20, -15, 10, 10), 90, 180));
    trans.addPrimitive(SymbolPrimitive::createArc(QRectF(-20, -5, 10, 10), 90, 180));
    trans.addPrimitive(SymbolPrimitive::createArc(QRectF(-20, 5, 10, 10), 90, 180));
    // Core
    trans.addPrimitive(SymbolPrimitive::createLine(QPointF(-5, -15), QPointF(-5, 15)));
    trans.addPrimitive(SymbolPrimitive::createLine(QPointF(5, -15), QPointF(5, 15)));
    // Secondary
    trans.addPrimitive(SymbolPrimitive::createArc(QRectF(10, -15, 10, 10), 270, 180));
    trans.addPrimitive(SymbolPrimitive::createArc(QRectF(10, -5, 10, 10), 270, 180));
    trans.addPrimitive(SymbolPrimitive::createArc(QRectF(10, 5, 10, 10), 270, 180));
    // Pins
    trans.addPrimitive(SymbolPrimitive::createPin(QPointF(-35, -10), 1, "P1"));
    trans.addPrimitive(SymbolPrimitive::createPin(QPointF(-35, 10), 2, "P2"));
    trans.addPrimitive(SymbolPrimitive::createPin(QPointF(35, -10), 3, "S1"));
    trans.addPrimitive(SymbolPrimitive::createPin(QPointF(35, 10), 4, "S2"));
    addSym(trans);

    // === RAM ===
    // === RAM ===
    SymbolDefinition ram("RAM");
    ram.setCategory("Integrated Circuits");
    ram.setReferencePrefix("U");
    ram.addPrimitive(SymbolPrimitive::createRect(QRectF(-40, -60, 80, 120), false));
    for (int i = 0; i < 8; i++) {
        ram.addPrimitive(SymbolPrimitive::createPin(QPointF(-55, -50 + i*10), i, "A"+QString::number(i)));
        ram.addPrimitive(SymbolPrimitive::createPin(QPointF(55, -50 + i*10), i+8, "D"+QString::number(i)));
    }
    addSym(ram);

    // === Specialized Diodes ===
    auto addSpecialDiode = [&](const QString& name, const QString& type) {
        SymbolDefinition d(name);
        d.setCategory("Semiconductors");
        d.setReferencePrefix("D");
        QList<QPointF> triangle = {QPointF(-10, -12), QPointF(-10, 12), QPointF(10, 0)};
        d.addPrimitive(SymbolPrimitive::createPolygon(triangle, true));
        
        if (type == "Zener") {
            d.addPrimitive(SymbolPrimitive::createLine(QPointF(10, -12), QPointF(10, 12)));
            d.addPrimitive(SymbolPrimitive::createLine(QPointF(10, -12), QPointF(15, -12)));
            d.addPrimitive(SymbolPrimitive::createLine(QPointF(5, 12), QPointF(10, 12)));
        } else if (type == "Schottky") {
            d.addPrimitive(SymbolPrimitive::createLine(QPointF(10, -12), QPointF(10, 12)));
            d.addPrimitive(SymbolPrimitive::createLine(QPointF(10, -12), QPointF(15, -12)));
            d.addPrimitive(SymbolPrimitive::createLine(QPointF(15, -12), QPointF(15, -8)));
            d.addPrimitive(SymbolPrimitive::createLine(QPointF(5, 12), QPointF(10, 12)));
            d.addPrimitive(SymbolPrimitive::createLine(QPointF(5, 8), QPointF(5, 12)));
        } else if (type == "LED") {
            d.addPrimitive(SymbolPrimitive::createLine(QPointF(10, -12), QPointF(10, 12)));
            // Arrows
            d.addPrimitive(SymbolPrimitive::createLine(QPointF(5, -15), QPointF(15, -25)));
            d.addPrimitive(SymbolPrimitive::createLine(QPointF(12, -15), QPointF(22, -25)));
        }
        
        d.addPrimitive(SymbolPrimitive::createPin(QPointF(-40, 0), 1, "A"));
        d.addPrimitive(SymbolPrimitive::createPin(QPointF(40, 0), 2, "K"));
        addSym(d);
    };
    addSpecialDiode("Diode_Zener", "Zener");
    addSpecialDiode("Diode_Schottky", "Schottky");
    addSpecialDiode("LED", "LED");

    // === Op-Amp ===
    SymbolDefinition opamp("OpAmp");
    opamp.setCategory("Integrated Circuits");
    opamp.setReferencePrefix("U");
    QList<QPointF> tri = {QPointF(-30, -30), QPointF(-30, 30), QPointF(30, 0)};
    opamp.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
    opamp.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, -15), 1, "-"));
    opamp.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 15), 2, "+"));
    opamp.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 0), 3, "OUT"));
    opamp.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -30), 4, "V+"));
    opamp.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 30), 5, "V-"));
    addSym(opamp);

    // === Logic Gates ===
    auto makeDigitalPin = [](const QPointF& pos, int number, const QString& name,
                             const QString& orientation, const QString& direction,
                             qreal length = 15.0) {
        SymbolPrimitive pin = SymbolPrimitive::createPin(pos, number, name);
        pin.data["orientation"] = orientation;
        pin.data["length"] = length;
        pin.data["signalDomain"] = "digital_event";
        pin.data["signalDirection"] = direction;
        return pin;
    };

    auto addDigitalBlockText = [](SymbolDefinition& def, const QString& textValue, const QPointF& pos,
                                  int size, const QString& hAlign = "center", const QString& vAlign = "center") {
        SymbolPrimitive text = SymbolPrimitive::createText(textValue, pos, size, QColor(Qt::black));
        text.data["hAlign"] = hAlign;
        text.data["vAlign"] = vAlign;
        def.addPrimitive(text);
    };

    auto addDigitalMarker = [&](SymbolDefinition& def, const QString& pinName, const QPointF& anchor) {
        const QString upper = pinName.trimmed().toUpper();
        if (upper == "CLK" || upper == "CLOCK" || upper == "CK" || upper == "C") {
            def.addPrimitive(SymbolPrimitive::createLine(anchor + QPointF(-8, -6), anchor + QPointF(0, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(anchor + QPointF(-8, 6), anchor + QPointF(0, 0)));
        } else if (upper == "EN" || upper == "G" || upper == "GATE") {
            def.addPrimitive(SymbolPrimitive::createLine(anchor + QPointF(-8, -6), anchor + QPointF(0, -6)));
            def.addPrimitive(SymbolPrimitive::createLine(anchor + QPointF(-8, 6), anchor + QPointF(0, 6)));
            def.addPrimitive(SymbolPrimitive::createLine(anchor + QPointF(0, -6), anchor + QPointF(0, 6)));
        }
    };

    auto addDigitalBlock = [&](const QString& name,
                               const QString& label,
                               const QString& description,
                               const QStringList& aliases,
                               const QString& spiceModel,
                               const QList<SymbolPrimitive>& pins,
                               qreal bodyHeight,
                               bool invertPrimaryOutput = false,
                               bool invertSecondaryOutput = false) {
        SymbolDefinition block(name);
        block.setCategory("Logic");
        block.setReferencePrefix("U");
        block.setDescription(description);
        block.setAliases(aliases);
        block.setSpiceModelName(spiceModel);

        const qreal halfHeight = bodyHeight / 2.0;
        const qreal bodyLeft = -28.0;
        const qreal bodyWidth = 56.0;
        block.addPrimitive(SymbolPrimitive::createRect(QRectF(bodyLeft, -halfHeight, bodyWidth, bodyHeight), false));
        block.addPrimitive(SymbolPrimitive::createLine(QPointF(bodyLeft, -halfHeight + 14.0), QPointF(bodyLeft + bodyWidth, -halfHeight + 14.0)));
        addDigitalBlockText(block, spiceModel, QPointF(0, -halfHeight + 7.0), 8);
        addDigitalBlockText(block, label, QPointF(0, 6.0), 10);

        int outputIndex = 0;
        for (const SymbolPrimitive& pin : pins) {
            block.addPrimitive(pin);

            const QString direction = pin.data.value("signalDirection").toString();
            const QString pinName = pin.data.value("name").toString();
            const QPointF pinPos(pin.data.value("x").toDouble(), pin.data.value("y").toDouble());
            if (direction == "input") {
                addDigitalMarker(block, pinName, QPointF(bodyLeft, pinPos.y()));
            } else if (direction == "output") {
                const bool invertThis = (outputIndex == 0) ? invertPrimaryOutput : invertSecondaryOutput;
                if (invertThis) {
                    block.addPrimitive(SymbolPrimitive::createCircle(QPointF(bodyLeft + bodyWidth + 4.0, pinPos.y()), 3.0, false));
                }
                ++outputIndex;
            }
        }

        addSym(block);
    };

    auto addGate = [&](const QString& name, const QString& type) {
        const bool singleInputGate = (type == "NOT" || type == "BUF");
        const bool bubbleOutput = (type == "NAND" || type == "NOR" || type == "XNOR" || type == "NOT");
        QList<SymbolPrimitive> pins;
        pins << makeDigitalPin(QPointF(-45, singleInputGate ? 0.0 : -10.0), 1, "A", "Right", "input");
        if (!singleInputGate) {
            pins << makeDigitalPin(QPointF(-45, 10.0), 2, "B", "Right", "input");
        }
        pins << makeDigitalPin(QPointF(45, 0.0), 3, "Y", "Left", "output", bubbleOutput ? 17.0 : 15.0);

        addDigitalBlock(name,
                        type,
                        QString("Native digital %1 gate").arg(type.toLower()),
                        {type + " Gate", type},
                        type == "NOT" ? QString("NOT") : type,
                        pins,
                        singleInputGate ? 36.0 : 44.0,
                        bubbleOutput);
    };
    addGate("Gate_AND", "AND");
    addGate("Gate_OR", "OR");
    addGate("Gate_XOR", "XOR");
    addGate("Gate_XNOR", "XNOR");
    addGate("Gate_NAND", "NAND");
    addGate("Gate_NOR", "NOR");
    addGate("Gate_BUF", "BUF");
    addGate("Gate_NOT", "NOT");

    auto addSequentialLogic = [&](const QString& name,
                                  const QString& label,
                                  const QString& description,
                                  const QStringList& aliases,
                                  const QString& spiceModel,
                                  const QList<SymbolPrimitive>& pins,
                                  qreal bodyHeight) {
        addDigitalBlock(name, label, description, aliases, spiceModel, pins, bodyHeight, false, true);
    };

    addSequentialLogic(
        "D_FlipFlop",
        "D FF",
        "Native digital edge-triggered D flip-flop with asynchronous set/reset and complementary outputs",
        {"D Flip-Flop", "D Flip Flop", "DFF", "Gate_DFF"},
        "DFF",
        {
            makeDigitalPin(QPointF(-40, -20), 1, "D", "Right", "input"),
            makeDigitalPin(QPointF(-40, -5), 2, "CLK", "Right", "input"),
            makeDigitalPin(QPointF(-40, 10), 3, "SET", "Right", "input"),
            makeDigitalPin(QPointF(-40, 25), 4, "RESET", "Right", "input"),
            makeDigitalPin(QPointF(40, -10), 5, "Q", "Left", "output"),
            makeDigitalPin(QPointF(40, 10), 6, "QN", "Left", "output"),
        },
        70.0);

    addSequentialLogic(
        "JK_FlipFlop",
        "JK FF",
        "Native digital edge-triggered JK flip-flop with asynchronous set/reset and complementary outputs",
        {"JK Flip-Flop", "JK Flip Flop", "JKFF", "Gate_JKFF"},
        "JKFF",
        {
            makeDigitalPin(QPointF(-40, -25), 1, "J", "Right", "input"),
            makeDigitalPin(QPointF(-40, -10), 2, "K", "Right", "input"),
            makeDigitalPin(QPointF(-40, 5), 3, "CLK", "Right", "input"),
            makeDigitalPin(QPointF(-40, 20), 4, "SET", "Right", "input"),
            makeDigitalPin(QPointF(-40, 35), 5, "RESET", "Right", "input"),
            makeDigitalPin(QPointF(40, -10), 6, "Q", "Left", "output"),
            makeDigitalPin(QPointF(40, 10), 7, "QN", "Left", "output"),
        },
        90.0);

    addSequentialLogic(
        "T_FlipFlop",
        "T FF",
        "Native digital edge-triggered toggle flip-flop with asynchronous set/reset and complementary outputs",
        {"T Flip-Flop", "T Flip Flop", "TFF", "Toggle Flip-Flop", "Gate_TFF"},
        "TFF",
        {
            makeDigitalPin(QPointF(-40, -20), 1, "T", "Right", "input"),
            makeDigitalPin(QPointF(-40, -5), 2, "CLK", "Right", "input"),
            makeDigitalPin(QPointF(-40, 10), 3, "SET", "Right", "input"),
            makeDigitalPin(QPointF(-40, 25), 4, "RESET", "Right", "input"),
            makeDigitalPin(QPointF(40, -10), 5, "Q", "Left", "output"),
            makeDigitalPin(QPointF(40, 10), 6, "QN", "Left", "output"),
        },
        70.0);

    addSequentialLogic(
        "SR_FlipFlop",
        "SR FF",
        "Native digital edge-triggered set-reset flip-flop with asynchronous set/reset and complementary outputs",
        {"SR Flip-Flop", "SR Flip Flop", "Set-Reset Flip-Flop", "SRFF", "Gate_SRFF"},
        "SRFF",
        {
            makeDigitalPin(QPointF(-40, -25), 1, "S", "Right", "input"),
            makeDigitalPin(QPointF(-40, -10), 2, "R", "Right", "input"),
            makeDigitalPin(QPointF(-40, 5), 3, "CLK", "Right", "input"),
            makeDigitalPin(QPointF(-40, 20), 4, "SET", "Right", "input"),
            makeDigitalPin(QPointF(-40, 35), 5, "RESET", "Right", "input"),
            makeDigitalPin(QPointF(40, -10), 6, "Q", "Left", "output"),
            makeDigitalPin(QPointF(40, 10), 7, "QN", "Left", "output"),
        },
        90.0);

    addSequentialLogic(
        "D_Latch",
        "D LAT",
        "Native digital level-sensitive D latch with asynchronous set/reset and complementary outputs",
        {"D Latch", "DLATCH", "Gate_DLATCH"},
        "DLATCH",
        {
            makeDigitalPin(QPointF(-40, -20), 1, "D", "Right", "input"),
            makeDigitalPin(QPointF(-40, -5), 2, "EN", "Right", "input"),
            makeDigitalPin(QPointF(-40, 10), 3, "SET", "Right", "input"),
            makeDigitalPin(QPointF(-40, 25), 4, "RESET", "Right", "input"),
            makeDigitalPin(QPointF(40, -10), 5, "Q", "Left", "output"),
            makeDigitalPin(QPointF(40, 10), 6, "QN", "Left", "output"),
        },
        70.0);

    addSequentialLogic(
        "SR_Latch",
        "SR LAT",
        "Native digital level-sensitive set-reset latch with enable, asynchronous set/reset, and complementary outputs",
        {"SR Latch", "Set-Reset Latch", "SRLATCH", "Gate_SRLATCH"},
        "SRLATCH",
        {
            makeDigitalPin(QPointF(-40, -25), 1, "S", "Right", "input"),
            makeDigitalPin(QPointF(-40, -10), 2, "R", "Right", "input"),
            makeDigitalPin(QPointF(-40, 5), 3, "EN", "Right", "input"),
            makeDigitalPin(QPointF(-40, 20), 4, "SET", "Right", "input"),
            makeDigitalPin(QPointF(-40, 35), 5, "RESET", "Right", "input"),
            makeDigitalPin(QPointF(40, -10), 6, "Q", "Left", "output"),
            makeDigitalPin(QPointF(40, 10), 7, "QN", "Left", "output"),
        },
        90.0);

    addDigitalBlock(
        "Counter",
        "COUNT",
        "Native digital counter with clock input and primary output",
        {"COUNTER", "Gate_COUNTER"},
        "COUNTER",
        {
            makeDigitalPin(QPointF(-45, 0.0), 1, "CLK", "Right", "input"),
            makeDigitalPin(QPointF(45, 0.0), 2, "Q", "Left", "output"),
        },
        36.0);

    addDigitalBlock(
        "Schmitt_Trigger",
        "SCHMITT",
        "Native digital Schmitt trigger input buffer",
        {"SCHMITT", "Schmitt Trigger", "Gate_SCHMITT"},
        "SCHMITT",
        {
            makeDigitalPin(QPointF(-45, 0.0), 1, "A", "Right", "input"),
            makeDigitalPin(QPointF(45, 0.0), 2, "Y", "Left", "output"),
        },
        36.0);

    addDigitalBlock(
        "TriState_Buffer",
        "TRI",
        "Native digital tri-state buffer with input, enable, and output",
        {"TRISTATE", "Tri-State Buffer", "Gate_TRISTATE"},
        "TRISTATE",
        {
            makeDigitalPin(QPointF(-45, -10.0), 1, "A", "Right", "input"),
            makeDigitalPin(QPointF(-45, 10.0), 2, "EN", "Right", "input"),
            makeDigitalPin(QPointF(45, 0.0), 3, "Y", "Left", "output"),
        },
        44.0);

    addDigitalBlock(
        "RAM_Native",
        "RAM",
        "Native digital RAM block with address, data in/out, chip select and write enable",
        {"RAM Native", "Native RAM", "RAM_DIGITAL"},
        "RAM",
        {
            makeDigitalPin(QPointF(-45, -25.0), 1, "A", "Right", "input"),
            makeDigitalPin(QPointF(-45, -10.0), 2, "DI", "Right", "input"),
            makeDigitalPin(QPointF(-45, 5.0), 3, "CS", "Right", "input"),
            makeDigitalPin(QPointF(-45, 20.0), 4, "WE", "Right", "input"),
            makeDigitalPin(QPointF(45, -10.0), 5, "DO", "Left", "output"),
            makeDigitalPin(QPointF(45, 10.0), 6, "DO_BAR", "Left", "output"),
        },
        70.0);

    auto addRamSymbol = [&](const QString& name,
                            const QString& label,
                            const QString& description,
                            int dataWidth,
                            int addressWidth) {
        SymbolDefinition ram(name);
        ram.setCategory("Logic");
        ram.setReferencePrefix("U");
        ram.setDescription(description);
        ram.setAliases({label, QString("RAM"), QString("Memory")});
        ram.setSpiceModelName("RAM");
        ram.addPrimitive(SymbolPrimitive::createRect(QRectF(-35, -55, 70, 110), false));

        SymbolPrimitive text = SymbolPrimitive::createText(label, QPointF(0, -35), 10, QColor(Qt::black));
        text.data["hAlign"] = "center";
        text.data["vAlign"] = "center";
        ram.addPrimitive(text);

        SymbolPrimitive bodyText = SymbolPrimitive::createText(QString("%1-bit").arg(dataWidth), QPointF(0, -15), 9, QColor(Qt::black));
        bodyText.data["hAlign"] = "center";
        bodyText.data["vAlign"] = "center";
        ram.addPrimitive(bodyText);

        auto makeVectorPin = [&](const QPointF& pos, int number, const QString& pinName,
                                 const QString& orientation, const QString& direction,
                                 const QString& group, int index) {
            SymbolPrimitive pin = makeDigitalPin(pos, number, pinName, orientation, direction);
            pin.data["signalVectorGroup"] = group;
            pin.data["signalVectorIndex"] = index;
            return pin;
        };

        int pinNumber = 1;
        for (int i = 0; i < dataWidth; ++i) {
            ram.addPrimitive(makeVectorPin(QPointF(-55, -45 + i * 10), pinNumber++, QString("DI%1").arg(i), "Right", "input", "data_in", i));
        }
        for (int i = 0; i < dataWidth; ++i) {
            ram.addPrimitive(makeVectorPin(QPointF(55, -45 + i * 10), pinNumber++, QString("DO%1").arg(i), "Left", "output", "data_out", i));
        }
        for (int i = 0; i < addressWidth; ++i) {
            ram.addPrimitive(makeVectorPin(QPointF(-15 + i * 10, 75), pinNumber++, QString("A%1").arg(i), "Up", "input", "address", i));
        }

        ram.addPrimitive(makeDigitalPin(QPointF(-55, 55), pinNumber++, "WE", "Right", "input"));
        SymbolPrimitive cs = makeVectorPin(QPointF(55, 55), pinNumber++, "CS0", "Left", "input", "select", 0);
        ram.addPrimitive(cs);

        addSym(ram);
    };

    // Legacy XSPICE RAM variants intentionally not exposed in the default built-in
    // library path now that native digital RAM is available.

    // === Behavioral Sources ===
    auto addBehSource = [&](const QString& name, bool isVoltage) {
        SymbolDefinition s(name);
        s.setCategory("Simulation");
        s.setReferencePrefix(isVoltage ? "B" : "B");
        s.setDescription(isVoltage ? "Behavioral Voltage Source" : "Behavioral Current Source");
        s.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, 0), 22.5, false));
        s.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -45), QPointF(0, -22.5)));
        s.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 45), QPointF(0, -22.5))); // Fixed end point below
        s.primitives().last().data["y2"] = 22.5; 
        
        SymbolPrimitive text = SymbolPrimitive::createText(isVoltage ? "V=..." : "I=...", QPointF(0, 0), 10, QColor(Qt::black));
        text.data["hAlign"] = "center";
        text.data["vAlign"] = "center";
        s.addPrimitive(text);
        
        s.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -45), 1, "1"));
        s.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 45), 2, "2"));
        addSym(s);
    };
    addBehSource("Voltage_Source_Behavioral", true);
    addBehSource("Current_Source_Behavioral", false);
    // Aliases
    SymbolDefinition bv = SymbolDefinition("BV"); 
    bv.setCategory("Simulation");
    bv.setReferencePrefix("B");
    bv.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, 0), 22.5, false));
    bv.addPrimitive(SymbolPrimitive::createText("V=...", QPointF(0, 0), 10, QColor(Qt::black)));
    bv.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -45), 1, "1"));
    bv.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 45), 2, "2"));
    addSym(bv);

    SymbolDefinition bi = SymbolDefinition("BI");
    bi.setCategory("Simulation");
    bi.setReferencePrefix("B");
    bi.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, 0), 22.5, false));
    bi.addPrimitive(SymbolPrimitive::createText("I=...", QPointF(0, 0), 10, QColor(Qt::black)));
    bi.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -45), 1, "1"));
    bi.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 45), 2, "2"));
    addSym(bi);

    // === Controlled Sources (E, G, F, H) ===
    auto addControlledSource = [&](const QString& name, const QString& label, const QString& prefix) {
        SymbolDefinition s(name);
        s.setCategory("Simulation");
        s.setReferencePrefix(prefix);
        s.addPrimitive(SymbolPrimitive::createRect(QRectF(-25, -25, 50, 50), false));
        SymbolPrimitive text = SymbolPrimitive::createText(label, QPointF(0, 0), 12, QColor(Qt::black));
        text.data["hAlign"] = "center";
        text.data["vAlign"] = "center";
        s.addPrimitive(text);
        
        // Keep control pins on the left and output pins on the right, but number
        // them in SPICE order so the netlister emits:
        // E/G ref O+ O- C+ C- gain
        s.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, -15), 3, "C+"));
        s.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 15), 4, "C-"));
        s.addPrimitive(SymbolPrimitive::createPin(QPointF(45, -15), 1, "O+"));
        s.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 15), 2, "O-"));
        
        addSym(s);
    };
    addControlledSource("E", "E", "E");
    addControlledSource("G", "G", "G");
    addControlledSource("F", "F", "F");
    addControlledSource("H", "H", "H");
    
    // === Tuning Slider ===
    SymbolDefinition slider("Tuning Slider");
    slider.setCategory("Simulation");
    slider.setReferencePrefix("A");
    slider.setDescription("Real-time parameter tuning control");
    
    // Chassis
    slider.addPrimitive(SymbolPrimitive::createRect(QRectF(0, 0, 120, 35), false));
    // Header line
    slider.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 16), QPointF(120, 16)));
    // Readout box
    slider.addPrimitive(SymbolPrimitive::createRect(QRectF(75, 3, 40, 12), false));
    // Track
    slider.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 25), QPointF(105, 25)));
    // Knob (as a small rect)
    SymbolPrimitive knob = SymbolPrimitive::createRect(QRectF(56, 18, 8, 14), true);
    slider.addPrimitive(knob);

    SymbolPrimitive sliderText = SymbolPrimitive::createText("Tuning", QPointF(8, 12), 8, QColor(Qt::black));
    slider.addPrimitive(sliderText);
    addSym(slider);
    
    // === Voltage Regulator ===

    QString baseDir = QDir::homePath() + "/ViospiceLib/sym";
    for (auto it = catLibs.begin(); it != catLibs.end(); ++it) {
        QString cat = it.key();
        SymbolLibrary* lib = it.value();
        QString filePath = baseDir + "/" + cat + ".sclib";

        if (QFile::exists(filePath)) {
            SymbolLibrary existing(cat, true);
            if (existing.load(filePath)) {
                bool addedMissingSymbol = false;
                const QList<SymbolDefinition> defaults = lib->allSymbols();
                for (const SymbolDefinition& symbol : defaults) {
                    SymbolDefinition* current = existing.findSymbol(symbol.name());
                    if (!current) {
                        existing.addSymbol(symbol);
                        addedMissingSymbol = true;
                        continue;
                    }

                    if (isLegacyBuiltInMosfetSymbol(*current)) {
                        existing.addSymbol(symbol);
                        addedMissingSymbol = true;
                    }
                }
                if (addedMissingSymbol) {
                    existing.save(filePath);
                }
            }
        } else {
            lib->save(filePath);
        }
        delete lib; // Clean up since loadUserLibraries will actually load them into memory
    }
}
