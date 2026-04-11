#include "footprint_library.h"
#include "../core/library_index.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QDebug>
#include <QStandardPaths>
#include <QPainterPath>
#include <QRegularExpression>

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
    QString baseDir = QDir::homePath() + "/ViospiceLib/footprints";
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
    QString baseDir = QDir::homePath() + "/ViospiceLib/footprints";
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

    // === Through-Hole Passives ===
    auto addTHResistor = [&](const QString& name, qreal pitch, qreal bodyL, qreal bodyD) {
        FootprintDefinition def(name);
        def.setCategory("Passives");
        def.addPrimitive(FootprintPrimitive::createPad(QPointF(-pitch/2, 0), "1", "Round", QSizeF(1.6, 1.6)));
        def.addPrimitive(FootprintPrimitive::createPad(QPointF(pitch/2, 0), "2", "Round", QSizeF(1.6, 1.6)));
        def.primitives()[0].data["drill_size"] = 0.8;
        def.primitives()[1].data["drill_size"] = 0.8;
        def.addPrimitive(FootprintPrimitive::createRect(QRectF(-bodyL/2, -bodyD/2, bodyL, bodyD)));
        addFootprintToCat(def);
    };
    addTHResistor("Resistor_THT_P7.62mm_Horizontal", 7.62, 6.3, 2.5);
    addTHResistor("Resistor_THT_P10.16mm_Horizontal", 10.16, 6.3, 2.5);

    auto addTHCapRadial = [&](const QString& name, qreal pitch, qreal bodyD) {
        FootprintDefinition def(name);
        def.setCategory("Passives");
        def.addPrimitive(FootprintPrimitive::createPad(QPointF(-pitch/2, 0), "1", "Round", QSizeF(1.6, 1.6)));
        def.addPrimitive(FootprintPrimitive::createPad(QPointF(pitch/2, 0), "2", "Round", QSizeF(1.6, 1.6)));
        def.primitives()[0].data["drill_size"] = 0.8;
        def.primitives()[1].data["drill_size"] = 0.8;
        def.addPrimitive(FootprintPrimitive::createCircle(QPointF(0, 0), bodyD/2));
        addFootprintToCat(def);
    };
    addTHCapRadial("C_Radial_D5.0mm_P2.00mm", 2.0, 5.0);
    addTHCapRadial("C_Radial_D6.3mm_P2.50mm", 2.5, 6.3);
    addTHCapRadial("C_Radial_D8.0mm_P3.50mm", 3.5, 8.0);

    auto addTHDiode = [&](const QString& name, qreal pitch, qreal bodyL, qreal bodyD) {
        FootprintDefinition def(name);
        def.setCategory("Passives");
        def.addPrimitive(FootprintPrimitive::createPad(QPointF(-pitch/2, 0), "1", "Round", QSizeF(1.8, 1.8)));
        def.addPrimitive(FootprintPrimitive::createPad(QPointF(pitch/2, 0), "2", "Round", QSizeF(1.8, 1.8)));
        def.primitives()[0].data["drill_size"] = 0.9;
        def.primitives()[1].data["drill_size"] = 0.9;
        def.addPrimitive(FootprintPrimitive::createRect(QRectF(-bodyL/2, -bodyD/2, bodyL, bodyD)));
        def.addPrimitive(FootprintPrimitive::createLine(QPointF(-bodyL/2 + 0.5, -bodyD/2), QPointF(-bodyL/2 + 0.5, bodyD/2)));
        addFootprintToCat(def);
    };
    addTHDiode("D_THT_P7.62mm_Horizontal", 7.62, 3.2, 2.0); 
    addTHDiode("D_THT_P10.16mm_Horizontal", 10.16, 5.0, 3.3);

    // === Crystals ===
    auto addTHCrystal = [&](const QString& name, qreal pitch, qreal bodyW, qreal bodyH) {
        FootprintDefinition def(name);
        def.setCategory("Crystals");
        def.addPrimitive(FootprintPrimitive::createPad(QPointF(-pitch/2, 0), "1", "Round", QSizeF(1.2, 1.2)));
        def.addPrimitive(FootprintPrimitive::createPad(QPointF(pitch/2, 0), "2", "Round", QSizeF(1.2, 1.2)));
        def.primitives()[0].data["drill_size"] = 0.6;
        def.primitives()[1].data["drill_size"] = 0.6;
        def.addPrimitive(FootprintPrimitive::createRect(QRectF(-bodyW/2, -bodyH/2, bodyW, bodyH)));
        addFootprintToCat(def);
    };
    addTHCrystal("Crystal_HC49-U_Vertical", 4.88, 10.5, 4.5);

    // === Connectors / Headers ===
    auto addHeader = [&](int pins) {
        FootprintDefinition def(QString("Header_1x%1_P2.54mm").arg(pins));
        def.setCategory("Connectors");
        qreal startX = -((pins - 1) * 2.54) / 2.0;
        for (int i = 0; i < pins; ++i) {
            def.addPrimitive(FootprintPrimitive::createPad(QPointF(startX + i*2.54, 0), QString::number(i+1), 
                             (i == 0 ? "Rect" : "Round"), QSizeF(1.7, 1.7)));
            def.primitives().last().data["drill_size"] = 1.0;
        }
        def.addPrimitive(FootprintPrimitive::createRect(QRectF(startX - 1.27, -1.27, pins * 2.54, 2.54)));
        addFootprintToCat(def);
    };
    for (int p : {2, 3, 4, 6, 8, 10}) addHeader(p);

    // === Potentiometers ===
    auto addPot = [&]() {
        FootprintDefinition def("Potentiometer_Bourns_3296W_Vertical");
        def.setCategory("Passives");
        def.addPrimitive(FootprintPrimitive::createPad(QPointF(-2.54, 0), "1", "Round", QSizeF(1.5, 1.5)));
        def.addPrimitive(FootprintPrimitive::createPad(QPointF(0, 0), "2", "Round", QSizeF(1.5, 1.5)));
        def.addPrimitive(FootprintPrimitive::createPad(QPointF(2.54, 0), "3", "Round", QSizeF(1.5, 1.5)));
        for(int i=0; i<3; i++) def.primitives()[i].data["drill_size"] = 0.7;
        def.addPrimitive(FootprintPrimitive::createRect(QRectF(-4.8, -2.4, 9.6, 4.8)));
        addFootprintToCat(def);
    };
    addPot();

    // === SMD Passives ===
    addSMDResCap("R_0402", 1.0, 0.5, 0.5, 0.6);
    addSMDResCap("R_0603", 1.6, 0.8, 0.8, 0.9);
    addSMDResCap("R_0805", 2.0, 1.25, 1.0, 1.3);
    addSMDResCap("R_1206", 3.2, 1.6, 1.2, 1.8);

    addSMDResCap("C_0402", 1.0, 0.5, 0.5, 0.6);
    addSMDResCap("C_0603", 1.6, 0.8, 0.8, 0.9);
    addSMDResCap("C_0805", 2.0, 1.25, 1.0, 1.3);
    addSMDResCap("C_1206", 3.2, 1.6, 1.2, 1.8);

    // === Transistors ===
    FootprintDefinition sot23("SOT-23");
    sot23.setCategory("Transistors");
    sot23.addPrimitive(FootprintPrimitive::createPad(QPointF(-0.95, 1.0), "1", "Rect", QSizeF(0.8, 0.9)));
    sot23.addPrimitive(FootprintPrimitive::createPad(QPointF(0.95, 1.0), "2", "Rect", QSizeF(0.8, 0.9)));
    sot23.addPrimitive(FootprintPrimitive::createPad(QPointF(0, -1.0), "3", "Rect", QSizeF(0.8, 0.9)));
    sot23.addPrimitive(FootprintPrimitive::createRect(QRectF(-1.5, -0.7, 3.0, 1.4)));
    addFootprintToCat(sot23);

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

    auto addTO92 = [&]() {
        FootprintDefinition def("TO-92_Inline");
        def.setCategory("Transistors");
        def.addPrimitive(FootprintPrimitive::createPad(QPointF(-1.27, 0), "1", "Round", QSizeF(1.2, 1.2)));
        def.addPrimitive(FootprintPrimitive::createPad(QPointF(0, 0), "2", "Round", QSizeF(1.2, 1.2)));
        def.addPrimitive(FootprintPrimitive::createPad(QPointF(1.27, 0), "3", "Round", QSizeF(1.2, 1.2)));
        for(int i=0; i<3; ++i) def.primitives()[i].data["drill_size"] = 0.6;
        def.addPrimitive(FootprintPrimitive::createRect(QRectF(-2.5, -1.5, 5.0, 3.0)));
        addFootprintToCat(def);
    };
    addTO92();

    // Clean up
    qDeleteAll(catLibs);
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
    QString basePath = QDir::homePath() + "/ViospiceLib/footprints";
    QDir().mkpath(basePath);

    QString safeName = name.trimmed();
    if (safeName.isEmpty()) safeName = "User Library";
    safeName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");

    QString libPath = basePath + "/" + safeName;
    QDir dir(libPath);
    if (!dir.exists()) dir.mkpath(".");
    
    addLibrary(libPath);
    return findLibrary(safeName);
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
    QString baseDir = QDir::homePath() + "/ViospiceLib/footprints";
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
