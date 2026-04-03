#include "footprint_library.h"
#include "../core/library_index.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QDebug>
#include <QStandardPaths>

using Flux::Model::FootprintDefinition;

namespace {
QString buildFootprintTags(const FootprintDefinition& def) {
    QStringList tags;
    tags << def.name() << def.category() << def.description() << def.classification();
    tags << def.keywords();
    for (QString& tag : tags) tag = tag.trimmed();
    tags.removeAll(QString());
    tags.removeDuplicates();
    return tags.join(" ");
}

void indexLibraryFootprints(FootprintLibrary* lib) {
    if (!lib) return;
    for (const QString& name : lib->getFootprintNames()) {
        const FootprintDefinition def = lib->getFootprint(name);
        LibraryIndex::instance().addFootprint(name, lib->name(), def.category(), buildFootprintTags(def));
    }
}
} // namespace

// ================= FootprintLibrary =================

FootprintLibrary::FootprintLibrary(const QString& name, const QString& path, bool builtIn)
    : m_name(name), m_path(path), m_builtIn(builtIn) {
    load();
}

void FootprintLibrary::load() {
    if (m_path.endsWith(".fplib")) {
        // Load from a single library file
        QFile file(m_path);
        if (file.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (!doc.isNull() && doc.isObject()) {
                QJsonObject root = doc.object();
                QJsonArray fpArray = root["footprints"].toArray();
                for (const QJsonValue& val : fpArray) {
                    FootprintDefinition def = FootprintDefinition::fromJson(val.toObject());
                    if (def.isValid()) {
                        m_footprints[def.name()] = def;
                    }
                }
            }
            file.close();
        }
        return;
    }

    QDir dir(m_path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QStringList filters;
    filters << "*.json";
    dir.setNameFilters(filters);
    
    QFileInfoList files = dir.entryInfoList();
    for (const QFileInfo& fileInfo : files) {
        QFile file(fileInfo.absoluteFilePath());
        if (file.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (!doc.isNull() && doc.isObject()) {
                FootprintDefinition def = FootprintDefinition::fromJson(doc.object());
                if (def.isValid()) {
                    m_footprints[def.name()] = def;
                }
            }
            file.close();
        }
    }
}

bool FootprintLibrary::hasFootprint(const QString& name) const {
    return m_footprints.contains(name);
}

FootprintDefinition FootprintLibrary::getFootprint(const QString& name) const {
    return m_footprints.value(name);
}

QStringList FootprintLibrary::getFootprintNames() const {
    return m_footprints.keys();
}

void FootprintLibrary::addFootprint(const FootprintDefinition& footprint) {
    if (footprint.isValid() && !footprint.name().isEmpty()) {
        m_footprints[footprint.name()] = footprint;
    }
}

bool FootprintLibrary::saveFootprint(const FootprintDefinition& footprint) {
    if (!footprint.isValid() || footprint.name().isEmpty()) return false;

    // Update memory
    m_footprints[footprint.name()] = footprint;

    // Save to disk
    QDir dir(m_path);
    if (!dir.exists()) dir.mkpath(".");

    QString filename = dir.filePath(footprint.name() + ".json");
    QFile file(filename);
    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(footprint.toJson());
        file.write(doc.toJson());
        file.close();
        qDebug() << "Saved footprint" << footprint.name() << "to library" << m_name;
        return true;
    } 
    return false;
}

// ================= FootprintLibraryManager =================

FootprintLibraryManager& FootprintLibraryManager::instance() {
    static FootprintLibraryManager instance;
    return instance;
}

FootprintLibraryManager::FootprintLibraryManager(QObject* parent) : QObject(parent) {
    initialize();
}

FootprintLibraryManager::~FootprintLibraryManager() {
    qDeleteAll(m_libraries);
}

void FootprintLibraryManager::initialize() {
    LibraryIndex::instance().initialize();
    
    // 1. Load Built-in from Resources
    FootprintLibrary* builtin = new FootprintLibrary("Built-in Standard", ":/library/builtin.fplib", true);
    m_libraries.append(builtin);
    indexLibraryFootprints(builtin);
    
    // 2. Ensure root directory exists for user libs
    QString baseDir = QDir::homePath() + "/.viora_eda/footprints";
    QDir().mkpath(baseDir);

    // Seed a richer default library set in user space on first run.
    createDefaultBuiltInLibrary();

    // 3. Scan for user directory libraries (category folders)
    QDir userDir(baseDir);
    QFileInfoList subdirs = userDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& dirInfo : subdirs) {
        addLibrary(dirInfo.absoluteFilePath());
    }

    // 4. Scan for standalone .fplib libraries dropped into user footprint root.
    const QFileInfoList fplibFiles = userDir.entryInfoList(QStringList() << "*.fplib", QDir::Files);
    for (const QFileInfo& fileInfo : fplibFiles) {
        addLibrary(fileInfo.absoluteFilePath());
    }
}

void FootprintLibraryManager::loadBuiltInLibrary() {
    // Logic moved to initialize()
}

void FootprintLibraryManager::createDefaultBuiltInLibrary() {
    QString baseDir = QDir::homePath() + "/.viora_eda/footprints";
    QMap<QString, FootprintLibrary*> catLibs;

    auto addFootprintToCat = [&](const FootprintDefinition& fpt) {
        QString cat = fpt.category();
        if (cat.isEmpty()) cat = "Uncategorized";
        if (!catLibs.contains(cat)) {
            QString libDir = baseDir + "/" + cat;
            QDir().mkpath(libDir);
            catLibs[cat] = new FootprintLibrary(cat, libDir);
        }
        if (!QFile::exists(catLibs[cat]->path() + "/" + fpt.name() + ".json")) {
            catLibs[cat]->saveFootprint(fpt);
        }
    };

    auto addSMDResCap = [&](const QString& name, qreal w, qreal h, qreal padW, qreal padH) {
        FootprintDefinition def(name);
        def.setCategory("Passives");
        qreal x = (w / 2.0) - (padW / 2.0);
        def.addPrimitive(FootprintPrimitive::createPad(QPointF(-x, 0), "1", "Rect", QSizeF(padW, padH)));
        def.addPrimitive(FootprintPrimitive::createPad(QPointF(x, 0), "2", "Rect", QSizeF(padW, padH)));
        def.addPrimitive(FootprintPrimitive::createRect(QRectF(-w/2 - 0.2, -h/2 - 0.2, w + 0.4, h + 0.4)));
        addFootprintToCat(def);
    };

    // === SMD Passives ===
    addSMDResCap("R_0402", 1.0, 0.5, 0.5, 0.6);
    addSMDResCap("R_0603", 1.6, 0.8, 0.8, 0.9);
    addSMDResCap("R_0805", 2.0, 1.25, 1.0, 1.3);
    addSMDResCap("R_1206", 3.2, 1.6, 1.2, 1.8);
    
    addSMDResCap("C_0402", 1.0, 0.5, 0.5, 0.6);
    addSMDResCap("C_0603", 1.6, 0.8, 0.8, 0.9);
    addSMDResCap("C_0805", 2.0, 1.25, 1.0, 1.3);
    addSMDResCap("C_1206", 3.2, 1.6, 1.2, 1.8);

    // === Through-Hole Passives ===
    FootprintDefinition resAxial("R_Axial_DIN0207_Horizontal");
    resAxial.setCategory("Passives");
    resAxial.addPrimitive(FootprintPrimitive::createPad(QPointF(-3.81, 0), "1", "Round", QSizeF(1.6, 1.6)));
    resAxial.addPrimitive(FootprintPrimitive::createPad(QPointF(3.81, 0), "2", "Round", QSizeF(1.6, 1.6)));
    // Axial resistors are TH
    resAxial.primitives()[0].data["drill_size"] = 0.8;
    resAxial.primitives()[1].data["drill_size"] = 0.8;
    resAxial.addPrimitive(FootprintPrimitive::createRect(QRectF(-3.15, -1.25, 6.3, 2.5)));
    addFootprintToCat(resAxial);

    // === Diodes ===
    FootprintDefinition sod123("D_SOD-123");
    sod123.setCategory("Diodes");
    sod123.addPrimitive(FootprintPrimitive::createPad(QPointF(-1.65, 0), "1", "Rect", QSizeF(0.9, 1.2)));
    sod123.addPrimitive(FootprintPrimitive::createPad(QPointF(1.65, 0), "2", "Rect", QSizeF(0.9, 1.2)));
    sod123.addPrimitive(FootprintPrimitive::createRect(QRectF(-1.4, -0.8, 2.8, 1.6)));
    sod123.addPrimitive(FootprintPrimitive::createLine(QPointF(-0.5, -0.8), QPointF(-0.5, 0.8))); // Cathode mark
    addFootprintToCat(sod123);

    // === Transistors ===
    FootprintDefinition sot23("SOT-23");
    sot23.setCategory("Transistors");
    sot23.addPrimitive(FootprintPrimitive::createPad(QPointF(-0.95, 1.0), "1", "Rect", QSizeF(0.8, 0.9)));
    sot23.addPrimitive(FootprintPrimitive::createPad(QPointF(0.95, 1.0), "2", "Rect", QSizeF(0.8, 0.9)));
    sot23.addPrimitive(FootprintPrimitive::createPad(QPointF(0, -1.0), "3", "Rect", QSizeF(0.8, 0.9)));
    sot23.addPrimitive(FootprintPrimitive::createRect(QRectF(-1.45, -0.65, 2.9, 1.3)));
    addFootprintToCat(sot23);

    FootprintDefinition to92("TO-92_Inline");
    to92.setCategory("Transistors");
    to92.addPrimitive(FootprintPrimitive::createPad(QPointF(-1.27, 0), "1", "Round", QSizeF(1.2, 1.2)));
    to92.addPrimitive(FootprintPrimitive::createPad(QPointF(0, 0), "2", "Round", QSizeF(1.2, 1.2)));
    to92.addPrimitive(FootprintPrimitive::createPad(QPointF(1.27, 0), "3", "Round", QSizeF(1.2, 1.2)));
    to92.addPrimitive(FootprintPrimitive::createArc(QPointF(0, 0), 2.5, 0, 180));
    to92.addPrimitive(FootprintPrimitive::createLine(QPointF(-2.5, 0), QPointF(2.5, 0)));
    addFootprintToCat(to92);

    // === ICs ===
    auto addDIP = [&](int pins, qreal length) {
        QString name = QString("DIP-%1_W7.62mm").arg(pins);
        FootprintDefinition def(name);
        def.setCategory("ICs");
        int pinsPerSide = pins / 2;
        qreal startY = -((pinsPerSide - 1) * 2.54) / 2.0;
        for (int i = 0; i < pinsPerSide; ++i) {
            def.addPrimitive(FootprintPrimitive::createPad(QPointF(-3.81, startY + i*2.54), QString::number(i+1), "Oblong", QSizeF(1.6, 1.1)));
            def.addPrimitive(FootprintPrimitive::createPad(QPointF(3.81, startY + (pinsPerSide-1-i)*2.54), QString::number(pinsPerSide+i+1), "Oblong", QSizeF(1.6, 1.1)));
            // DIP are TH
            def.primitives().last().data["drill_size"] = 0.8;
            def.primitives()[def.primitives().size()-2].data["drill_size"] = 0.8;
        }
        def.addPrimitive(FootprintPrimitive::createRect(QRectF(-3.0, -length/2, 6.0, length)));
        def.addPrimitive(FootprintPrimitive::createCircle(QPointF(-2.0, -length/2 + 1.0), 0.3)); // Pin 1 mark
        addFootprintToCat(def);
    };
    addDIP(8, 10.16);
    addDIP(14, 17.78);
    addDIP(16, 20.32);

    auto addSOIC = [&](int pins, qreal length) {
        QString name = QString("SOIC-%1_3.9x%2mm_P1.27mm").arg(pins).arg(length);
        FootprintDefinition def(name);
        def.setCategory("ICs");
        int pinsPerSide = pins / 2;
        qreal startY = -((pinsPerSide - 1) * 1.27) / 2.0;
        for (int i = 0; i < pinsPerSide; ++i) {
            def.addPrimitive(FootprintPrimitive::createPad(QPointF(-2.6, startY + i*1.27), QString::number(i+1), "Rect", QSizeF(1.5, 0.6)));
            def.addPrimitive(FootprintPrimitive::createPad(QPointF(2.6, startY + (pinsPerSide-1-i)*1.27), QString::number(pinsPerSide+i+1), "Rect", QSizeF(1.5, 0.6)));
        }
        def.addPrimitive(FootprintPrimitive::createRect(QRectF(-1.95, -length/2, 3.9, length)));
        def.addPrimitive(FootprintPrimitive::createCircle(QPointF(-1.2, -length/2 + 0.8), 0.2)); // Pin 1 mark
        addFootprintToCat(def);
    };
    addSOIC(8, 4.9);
    addSOIC(14, 8.65);
    addSOIC(16, 9.9);

    // === Modern Footprints ===
    FootprintDefinition sot223("SOT-223");
    sot223.setCategory("Transistors");
    sot223.setDescription("Small Outline Transistor, 4 pins, 2.3mm pitch");
    sot223.addPrimitive(FootprintPrimitive::createPad(QPointF(-2.3, 3.1), "1", "Rect", QSizeF(1.0, 1.4)));
    sot223.addPrimitive(FootprintPrimitive::createPad(QPointF(0, 3.1), "2", "Rect", QSizeF(1.0, 1.4)));
    sot223.addPrimitive(FootprintPrimitive::createPad(QPointF(2.3, 3.1), "3", "Rect", QSizeF(1.0, 1.4)));
    sot223.addPrimitive(FootprintPrimitive::createPad(QPointF(0, -3.1), "4", "Rect", QSizeF(3.3, 1.4)));
    sot223.addPrimitive(FootprintPrimitive::createRect(QRectF(-3.25, -1.75, 6.5, 3.5)));
    addFootprintToCat(sot223);

    FootprintDefinition to220("TO-220-3_Vertical");
    to220.setCategory("Transistors");
    to220.setDescription("Transistor Outline, 3 pins, Vertical, 2.54mm pitch");
    to220.addPrimitive(FootprintPrimitive::createPad(QPointF(-2.54, 0), "1", "Round", QSizeF(1.8, 1.8)));
    to220.addPrimitive(FootprintPrimitive::createPad(QPointF(0, 0), "2", "Round", QSizeF(1.8, 1.8)));
    to220.addPrimitive(FootprintPrimitive::createPad(QPointF(2.54, 0), "3", "Round", QSizeF(1.8, 1.8)));
    // TO-220 is TH
    to220.primitives()[0].data["drill_size"] = 1.0;
    to220.primitives()[1].data["drill_size"] = 1.0;
    to220.primitives()[2].data["drill_size"] = 1.0;
    to220.addPrimitive(FootprintPrimitive::createRect(QRectF(-5.0, -1.2, 10.0, 4.5)));
    addFootprintToCat(to220);

    auto addTSSOP = [&](int pins, qreal bodyW, qreal bodyH) {
        FootprintDefinition def(QString("TSSOP-%1").arg(pins));
        def.setCategory("ICs");
        def.setDescription(QString("Thin Shrink Small Outline Package, %1 pins, 0.65mm pitch").arg(pins));
        qreal pitch = 0.65;
        int pinsPerSide = pins / 2;
        qreal startY = -((pinsPerSide - 1) * pitch) / 2.0;
        for (int i = 0; i < pinsPerSide; i++) {
            def.addPrimitive(FootprintPrimitive::createPad(QPointF(-2.8, startY + i*pitch), QString::number(i+1), "Rect", QSizeF(1.2, 0.4)));
            def.addPrimitive(FootprintPrimitive::createPad(QPointF(2.8, startY + (pinsPerSide-1-i)*pitch), QString::number(pinsPerSide+i+1), "Rect", QSizeF(1.2, 0.4)));
        }
        def.addPrimitive(FootprintPrimitive::createRect(QRectF(-bodyW/2, -bodyH/2, bodyW, bodyH)));
        addFootprintToCat(def);
    };
    addTSSOP(20, 4.4, 6.5);

    FootprintDefinition qfp48("QFP-48_7x7mm_P0.5mm");
    qfp48.setCategory("ICs");
    qreal qfpPitch = 0.5;
    for (int i = 0; i < 12; i++) {
        qreal pos = -((12-1)*qfpPitch)/2.0 + i*qfpPitch;
        qfp48.addPrimitive(FootprintPrimitive::createPad(QPointF(-4.2, pos), QString::number(i+1), "Rect", QSizeF(1.2, 0.3)));
        qfp48.addPrimitive(FootprintPrimitive::createPad(QPointF(pos, 4.2), QString::number(i+13), "Rect", QSizeF(0.3, 1.2)));
        qfp48.addPrimitive(FootprintPrimitive::createPad(QPointF(4.2, -pos), QString::number(i+25), "Rect", QSizeF(1.2, 0.3)));
        qfp48.addPrimitive(FootprintPrimitive::createPad(QPointF(-pos, -4.2), QString::number(i+37), "Rect", QSizeF(0.3, 1.2)));
    }
    qfp48.addPrimitive(FootprintPrimitive::createRect(QRectF(-3.5, -3.5, 7.0, 7.0)));
    addFootprintToCat(qfp48);

    // === Connectors ===
    FootprintDefinition usbc("USB-C_6Pin_PowerOnly");
    usbc.setCategory("Connectors");
    usbc.addPrimitive(FootprintPrimitive::createPad(QPointF(-3.2, 0), "1", "Rect", QSizeF(0.6, 1.2)));
    usbc.addPrimitive(FootprintPrimitive::createPad(QPointF(-1.2, 0), "2", "Rect", QSizeF(0.6, 1.2)));
    usbc.addPrimitive(FootprintPrimitive::createPad(QPointF(1.2, 0), "3", "Rect", QSizeF(0.6, 1.2)));
    usbc.addPrimitive(FootprintPrimitive::createPad(QPointF(3.2, 0), "4", "Rect", QSizeF(0.6, 1.2)));
    usbc.addPrimitive(FootprintPrimitive::createRect(QRectF(-4.5, -2, 9, 4)));
    addFootprintToCat(usbc);

    FootprintDefinition rj45("RJ45_Jack");
    rj45.setCategory("Connectors");
    for (int i=0; i<8; i++) {
        rj45.addPrimitive(FootprintPrimitive::createPad(QPointF(-3.5 + i*1.02, 0), QString::number(i+1), "Round", QSizeF(0.9, 0.9)));
    }
    rj45.addPrimitive(FootprintPrimitive::createRect(QRectF(-7.5, -5, 15, 10)));
    addFootprintToCat(rj45);

    auto addHeader = [&](int pins) {
        QString name = QString("PinHeader_1x%1_P2.54mm").arg(pins);
        FootprintDefinition def(name);
        def.setCategory("Connectors");
        qreal startY = -((pins - 1) * 2.54) / 2.0;
        for (int i = 0; i < pins; ++i) {
            QString shape = (i == 0) ? "Rect" : "Round";
            def.addPrimitive(FootprintPrimitive::createPad(QPointF(0, startY + i*2.54), QString::number(i+1), shape, QSizeF(1.7, 1.7)));
        }
        def.addPrimitive(FootprintPrimitive::createRect(QRectF(-1.27, -pins*2.54/2, 2.54, pins*2.54)));
        addFootprintToCat(def);
        
        // Add "mock" name alias
        FootprintDefinition alias = def;
        alias.setName(QString("PinHeader_1x%1").arg(pins < 10 ? QString("0%1").arg(pins) : QString::number(pins)));
        addFootprintToCat(alias);
    };
    addHeader(2);
    addHeader(3);
    addHeader(4);
    addHeader(6);
    addHeader(8);
    addHeader(10);

    // === Generic / Alias Footprints (Matches common naming) ===
    auto addAlias = [&](const QString& original, const QString& aliasName) {
        for (auto* lib : catLibs.values()) {
            if (lib->hasFootprint(original)) {
                FootprintDefinition alias = lib->getFootprint(original);
                alias.setName(aliasName);
                addFootprintToCat(alias);
                break;
            }
        }
    };
    addAlias("R_0402", "R0402"); addAlias("R_0603", "R0603"); addAlias("R_0805", "R0805"); addAlias("R_1206", "R1206");
    addAlias("C_0402", "C0402"); addAlias("C_0603", "C0603"); addAlias("C_0805", "C0805"); addAlias("C_1206", "C1206");
    addAlias("SOT-23", "SOT23");
    addAlias("SOT-23", "sot23");
    addAlias("SOT-23", "sot-23");
    addAlias("SOT-223", "SOT223");
    addAlias("SOT-223", "sot223");
    addAlias("SOT-223", "sot-223");
    addAlias("TO-220-3_Vertical", "TO220");
    addAlias("TO-92_Inline", "TO92");
    addAlias("TSSOP-20", "TSSOP20");
    addAlias("QFP-48_7x7mm_P0.5mm", "QFP48");
    addAlias("DIP-8_W7.62mm", "DIP-8");
    addAlias("DIP-14_W7.62mm", "DIP-14");
    addAlias("SOIC-8_3.9x4.9mm_P1.27mm", "SOIC-8");
    addAlias("SOIC-14_3.9x8.65mm_P1.27mm", "SOIC-14");
    addAlias("SOIC-16_3.9x9.9mm_P1.27mm", "SOIC-16");
    addAlias("USB-C_6Pin_PowerOnly", "USB-C_Female");
    addAlias("USB-C_6Pin_PowerOnly", "USB_C_Female");
    
    // Base generic defaults (already added in previous turn but for completeness)
    {
        FootprintDefinition genRes("Resistor");
        genRes.setCategory("Passives");
        genRes.addPrimitive(FootprintPrimitive::createPad(QPointF(-1.8, 0), "1", "Rect", QSizeF(1.2, 1.6)));
        genRes.addPrimitive(FootprintPrimitive::createPad(QPointF(1.8, 0), "2", "Rect", QSizeF(1.2, 1.6)));
        genRes.addPrimitive(FootprintPrimitive::createRect(QRectF(-2.0, -1.0, 4.0, 2.0)));
        addFootprintToCat(genRes);
    }
    
    for (FootprintLibrary* lib : catLibs.values()) {
        delete lib;
    }
}

void FootprintLibraryManager::addLibrary(const QString& path) {
    QFileInfo info(path);
    QString name;
    if (info.isFile()) name = info.completeBaseName();
    else name = QDir(path).dirName();
    if (name.isEmpty()) name = "User Library";
    
    // Check if already exists
    for (auto* lib : m_libraries) {
        if (lib->path() == path) return;
    }
    
    FootprintLibrary* lib = new FootprintLibrary(name, path);
    m_libraries.append(lib);
    indexLibraryFootprints(lib);
}

FootprintLibrary* FootprintLibraryManager::createLibrary(const QString& name) {
    QString basePath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (basePath.isEmpty()) basePath = QDir::currentPath() + "/footprints_lib";
    else basePath += "/footprints";
    
    QString libPath = basePath + "/" + name;
    QDir dir(libPath);
    if (!dir.exists()) dir.mkpath(".");
    
    addLibrary(libPath);
    return findLibrary(name);
}

FootprintLibrary* FootprintLibraryManager::findLibrary(const QString& name) {
    for (auto* lib : m_libraries) {
        if (lib->name() == name) return lib;
    }
    return nullptr;
}

FootprintDefinition FootprintLibraryManager::findFootprint(const QString& name) const {
    for (auto* lib : m_libraries) {
        if (lib->hasFootprint(name)) return lib->getFootprint(name);
    }
    return FootprintDefinition();
}

bool FootprintLibraryManager::hasFootprint(const QString& name) const {
    for (auto* lib : m_libraries) {
        if (lib->hasFootprint(name)) return true;
    }
    return false;
}

void FootprintLibraryManager::loadUserLibraries(const QString& userLibPath) {
    QFileInfo info(userLibPath);
    if (!info.exists()) {
        QDir().mkpath(userLibPath);
        emit progressUpdated("Footprint library created", 1, 1);
        return;
    }

    if (info.isFile() && userLibPath.endsWith(".fplib")) {
        // Load a single .fplib file
        FootprintLibrary* lib = new FootprintLibrary(info.baseName(), userLibPath);
        QWriteLocker locker(&m_lock);
        m_libraries.append(lib);
        indexLibraryFootprints(lib);
        emit progressUpdated(QString("Loaded footprint library: %1").arg(lib->name()), 1, 1);
        return;
    }

    if (info.isDir()) {
        // Scan directory for footprint libraries
        QDir dir(userLibPath);
        QFileInfoList subdirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        int total = subdirs.size();
        int count = 0;

        for (const QFileInfo& dirInfo : subdirs) {
            FootprintLibrary* lib = new FootprintLibrary(dirInfo.fileName(), dirInfo.absoluteFilePath());
            QWriteLocker locker(&m_lock);
            m_libraries.append(lib);
            indexLibraryFootprints(lib);
            count++;
            emit progressUpdated(QString("Loading footprint: %1").arg(lib->name()), count, total);
        }

        // Also scan for .fplib files in the root
        const QFileInfoList fplibFiles = dir.entryInfoList(QStringList() << "*.fplib", QDir::Files);
        total += fplibFiles.size();
        for (const QFileInfo& fileInfo : fplibFiles) {
            FootprintLibrary* lib = new FootprintLibrary(fileInfo.baseName(), fileInfo.absoluteFilePath());
            QWriteLocker locker(&m_lock);
            m_libraries.append(lib);
            indexLibraryFootprints(lib);
            count++;
            emit progressUpdated(QString("Loaded footprint library: %1").arg(lib->name()), count, total);
        }
    }

    emit loadingFinished();
}

void FootprintLibraryManager::reloadUserLibraries() {
    QWriteLocker locker(&m_lock);
    // Remove non-built-in libraries
    for (auto it = m_libraries.begin(); it != m_libraries.end();) {
        if (!(*it)->isBuiltIn()) {
            delete *it;
            it = m_libraries.erase(it);
        } else {
            ++it;
        }
    }

    // Reload from default location
    QString baseDir = QDir::homePath() + "/.viora_eda/footprints";
    QDir userDir(baseDir);
    if (userDir.exists()) {
        QFileInfoList subdirs = userDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& dirInfo : subdirs) {
            addLibrary(dirInfo.absoluteFilePath());
        }

        const QFileInfoList fplibFiles = userDir.entryInfoList(QStringList() << "*.fplib", QDir::Files);
        for (const QFileInfo& fileInfo : fplibFiles) {
            addLibrary(fileInfo.absoluteFilePath());
        }
    }
}
