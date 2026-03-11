#include "symbol_library.h"
//#include "../core/library_index.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDir>
#include <QDebug>

using Flux::Model::SymbolDefinition;
using Flux::Model::SymbolPrimitive;

namespace {
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

QString suggestedBuiltInFootprint(const SymbolDefinition& sym) {
    const QString key = normalizeBuiltInSymbolKey(sym.name());
    const QString cat = sym.category().trimmed().toLower();
    if (sym.isPowerSymbol() || key.contains("gnd") || key.contains("vcc") || key.contains("vdd") ||
        key.contains("vss") || key.contains("vbat") || key == "3.3v" || key == "5v" || key == "12v") {
        return QString();
    }

    if (key.contains("capacitor")) return "C_0603";

    int pinCount = 0;
    for (const SymbolPrimitive& prim : sym.primitives()) {
        if (prim.type == SymbolPrimitive::Pin) ++pinCount;
    }

    if (pinCount <= 0) {
        if (cat == "simulation") return QString();
        return "R_0805";
    }
    if (pinCount <= 2) return "R_0805";
    return "DIP-8";
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
    m_symbols[symbol.name()] = symbol;
}

void SymbolLibrary::removeSymbol(const QString& name) {
    m_symbols.remove(name);
}

SymbolDefinition* SymbolLibrary::findSymbol(const QString& name) {
    auto it = m_symbols.find(name);
    return it != m_symbols.end() ? &it.value() : nullptr;
}

const SymbolDefinition* SymbolLibrary::findSymbol(const QString& name) const {
    auto it = m_symbols.find(name);
    return it != m_symbols.end() ? &it.value() : nullptr;
}

QStringList SymbolLibrary::symbolNames() const {
    return m_symbols.keys();
}

QStringList SymbolLibrary::categories() const {
    QSet<QString> cats;
    for (const SymbolDefinition& sym : m_symbols) {
        if (!sym.category().isEmpty()) {
            cats.insert(sym.category());
        }
    }
    return cats.values();
}

QList<SymbolDefinition*> SymbolLibrary::symbolsInCategory(const QString& category) {
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
    
    m_name = libObj["name"].toString();
    m_path = filePath;
    m_symbols.clear();
    
    QJsonArray symbolsArr = libObj["symbols"].toArray();
    for (const QJsonValue& val : symbolsArr) {
        SymbolDefinition sym = SymbolDefinition::fromJson(val.toObject());
        m_symbols[sym.name()] = sym;
    }
    
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

SymbolLibrary SymbolLibrary::fromJson(const QJsonObject& json) {
    SymbolLibrary lib;
    QJsonObject libObj = json["library"].toObject();
    lib.m_name = libObj["name"].toString();
    
    QJsonArray symbolsArr = libObj["symbols"].toArray();
    for (const QJsonValue& val : symbolsArr) {
        SymbolDefinition sym = SymbolDefinition::fromJson(val.toObject());
        lib.m_symbols[sym.name()] = sym;
    }
    
    return lib;
}

// ============ SymbolLibraryManager ============

SymbolLibraryManager& SymbolLibraryManager::instance() {
    static SymbolLibraryManager inst;
    return inst;
}

SymbolLibraryManager::SymbolLibraryManager() {
    loadBuiltInLibrary();
    loadUserLibraries(QDir::homePath() + "/.viora_eda/symbols");
}

SymbolLibraryManager::~SymbolLibraryManager() {
    qDeleteAll(m_libraries);
}

void SymbolLibraryManager::addLibrary(SymbolLibrary* library) {
    if (library && !m_libraries.contains(library)) {
        m_libraries.append(library);
    }
}

void SymbolLibraryManager::removeLibrary(const QString& name) {
    for (int i = 0; i < m_libraries.size(); ++i) {
        if (m_libraries[i]->name() == name) {
            delete m_libraries.takeAt(i);
            break;
        }
    }
}

SymbolLibrary* SymbolLibraryManager::findLibrary(const QString& name) {
    for (SymbolLibrary* lib : m_libraries) {
        if (lib->name() == name) return lib;
    }
    return nullptr;
}

SymbolDefinition* SymbolLibraryManager::findSymbol(const QString& name) {
    for (SymbolLibrary* lib : m_libraries) {
        if (SymbolDefinition* sym = lib->findSymbol(name)) {
            return sym;
        }
    }
    return nullptr;
}

SymbolDefinition* SymbolLibraryManager::findSymbol(const QString& name, const QString& libraryName) {
    if (libraryName.isEmpty()) return findSymbol(name);
    SymbolLibrary* lib = findLibrary(libraryName);
    return lib ? lib->findSymbol(name) : nullptr;
}

QList<SymbolDefinition*> SymbolLibraryManager::search(const QString& query) {
    QList<SymbolDefinition*> results;
    QString q = query.toLower();
    
    for (SymbolLibrary* lib : m_libraries) {
        for (const QString& name : lib->symbolNames()) {
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

void SymbolLibraryManager::loadUserLibraries(const QString& userLibPath) {
    Q_UNUSED(userLibPath); // We now use ConfigManager paths + default path

    QStringList paths = ConfigManager::instance().symbolPaths();
    
    // Add default user path if not present (optional, but good for UX)
    QString defaultPath = QDir::homePath() + "/.viora_eda/symbols";
    if (!paths.contains(defaultPath)) paths.append(defaultPath);

    for (const QString& path : paths) {
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
                addLibrary(lib);
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
    }
}

QStringList SymbolLibraryManager::allCategories() const {
    QSet<QString> cats;
    for (SymbolLibrary* lib : m_libraries) {
        for (const QString& cat : lib->categories()) {
            cats.insert(cat);
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
    auto addMOSFET = [&](const QString& name, bool nChannel) {
        SymbolDefinition mos(name);
        mos.setCategory("Semiconductors");
        mos.setReferencePrefix("Q");
        mos.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, 0), 20, false));
        mos.addPrimitive(SymbolPrimitive::createLine(QPointF(-10, -15), QPointF(-10, 15))); // Gate
        mos.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -12), QPointF(0, -5))); // Drain segment
        mos.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 12), QPointF(0, 5))); // Source segment
        mos.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 0), QPointF(10, 0))); // Bulk
        mos.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -12), QPointF(10, -12)));
        mos.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 12), QPointF(10, 12)));
        
        // Arrow
        if (nChannel) {
            QList<QPointF> arrow = {QPointF(8, 0), QPointF(3.2, -2.4), QPointF(3.2, 2.4)};
            mos.addPrimitive(SymbolPrimitive::createPolygon(arrow, true));
        } else {
            QList<QPointF> arrow = {QPointF(2, 0), QPointF(6.8, -2.4), QPointF(6.8, 2.4)};
            mos.addPrimitive(SymbolPrimitive::createPolygon(arrow, true));
        }

        mos.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, 0), 1, "G"));
        mos.addPrimitive(SymbolPrimitive::createPin(QPointF(10, -30), 2, "D"));
        mos.addPrimitive(SymbolPrimitive::createPin(QPointF(10, 30), 3, "S"));

        // SPICE mapping: 1:D, 2:G, 3:S
        QMap<int, QString> mosMapping;
        mosMapping[1] = "D";
        mosMapping[2] = "G";
        mosMapping[3] = "S";
        mos.setSpiceNodeMapping(mosMapping);

        addSym(mos);
    };
    addMOSFET("Transistor_NMOS", true);
    addMOSFET("Transistor_PMOS", false);

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
    auto addGate = [&](const QString& name, const QString& type) {
        SymbolDefinition gate(name);
        gate.setCategory("Logic");
        gate.setReferencePrefix("U");
        
        const bool bubbleOutput = (type == "NAND" || type == "NOR");

        if (type == "AND" || type == "NAND") {
            gate.addPrimitive(SymbolPrimitive::createArc(QRectF(-10, -20, 40, 40), -90, 180));
            gate.addPrimitive(SymbolPrimitive::createLine(QPointF(-10, -20), QPointF(10, -20)));
            gate.addPrimitive(SymbolPrimitive::createLine(QPointF(-10, 20), QPointF(10, 20)));
            gate.addPrimitive(SymbolPrimitive::createLine(QPointF(-10, -20), QPointF(-10, 20)));
        } else if (type == "OR" || type == "NOR") {
            // OR gate body as Bezier curves for stable rendering/rotation.
            gate.addPrimitive(SymbolPrimitive::createBezier(
                QPointF(-30, -20), QPointF(-5, -22), QPointF(12, -12), QPointF(30, 0)));
            gate.addPrimitive(SymbolPrimitive::createBezier(
                QPointF(-30, 20), QPointF(-5, 22), QPointF(12, 12), QPointF(30, 0)));
            gate.addPrimitive(SymbolPrimitive::createBezier(
                QPointF(-30, -20), QPointF(-12, -10), QPointF(-12, 10), QPointF(-30, 20)));
        } else if (type == "XOR") {
            gate.addPrimitive(SymbolPrimitive::createBezier(
                QPointF(-30, -20), QPointF(-5, -22), QPointF(12, -12), QPointF(30, 0)));
            gate.addPrimitive(SymbolPrimitive::createBezier(
                QPointF(-30, 20), QPointF(-5, 22), QPointF(12, 12), QPointF(30, 0)));
            gate.addPrimitive(SymbolPrimitive::createBezier(
                QPointF(-30, -20), QPointF(-12, -10), QPointF(-12, 10), QPointF(-30, 20)));
            // XOR extra front arc
            gate.addPrimitive(SymbolPrimitive::createBezier(
                QPointF(-35, -20), QPointF(-17, -10), QPointF(-17, 10), QPointF(-35, 20)));
        } else if (type == "NOT") {
            // Draw triangle using lines for robust rendering across old/new polygon loaders.
            gate.addPrimitive(SymbolPrimitive::createLine(QPointF(-20, -15), QPointF(-20, 15)));
            gate.addPrimitive(SymbolPrimitive::createLine(QPointF(-20, -15), QPointF(5, 0)));
            gate.addPrimitive(SymbolPrimitive::createLine(QPointF(-20, 15), QPointF(5, 0)));
            gate.addPrimitive(SymbolPrimitive::createCircle(QPointF(10, 0), 5, false));
            gate.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 0), QPointF(30, 0)));
        }

        if (bubbleOutput) {
            gate.addPrimitive(SymbolPrimitive::createCircle(QPointF(35, 0), 5, false));
        }
        
        const qreal inAY = (type == "NOT") ? 0.0 : -10.0;
        SymbolPrimitive inA = SymbolPrimitive::createPin(QPointF(-45, inAY), 1, "A");
        inA.data["orientation"] = "Right";
        if (type == "OR" || type == "NOR" || type == "XOR") inA.data["length"] = 15.0;
        gate.addPrimitive(inA);
        if (type != "NOT") {
            SymbolPrimitive inB = SymbolPrimitive::createPin(QPointF(-45, 10), 2, "B");
            inB.data["orientation"] = "Right";
            if (type == "OR" || type == "NOR" || type == "XOR") inB.data["length"] = 15.0;
            gate.addPrimitive(inB);
        }
        const qreal outPinX = bubbleOutput ? 55.0 : 45.0;
        SymbolPrimitive outY = SymbolPrimitive::createPin(QPointF(outPinX, 0), 3, "Y");
        outY.data["orientation"] = "Left";
        outY.data["length"] = (type == "NOT") ? 30.0 : 15.0;
        gate.addPrimitive(outY);
        addSym(gate);
    };
    addGate("Gate_AND", "AND");
    addGate("Gate_OR", "OR");
    addGate("Gate_XOR", "XOR");
    addGate("Gate_NAND", "NAND");
    addGate("Gate_NOR", "NOR");
    addGate("Gate_NOT", "NOT");

    // === Voltage Regulator ===
    SymbolDefinition reg("VoltageRegulator");
    reg.setCategory("Power");
    reg.setReferencePrefix("U");
    reg.addPrimitive(SymbolPrimitive::createRect(QRectF(-30, -20, 60, 40), false));
    reg.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 0), 1, "IN"));
    reg.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 35), 2, "GND"));
    reg.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 0), 3, "OUT"));
    addSym(reg);

    QString baseDir = QDir::homePath() + "/.viora_eda/symbols";
    for (auto it = catLibs.begin(); it != catLibs.end(); ++it) {
        QString cat = it.key();
        SymbolLibrary* lib = it.value();
        QString filePath = baseDir + "/" + cat + ".sclib";
        
        // Only generate the default libraries if they don't already exist
        if (!QFile::exists(filePath)) {
            lib->save(filePath);
        }
        delete lib; // Clean up since loadUserLibraries will actually load them into memory
    }
}
