#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QGraphicsScene>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QTemporaryDir>
#include <algorithm>

#include <iostream>

#if __has_include("pcb/factories/pcb_item_registry.h")
#define VIOSPICE_HAS_PCB 1
#include "vioraeda/factories/pcb_item_registry.h"
#include "vioraeda/io/pcb_file_io.h"
#else
#define VIOSPICE_HAS_PCB 0
#endif
#include "flux/schematic/factories/schematic_item_registry.h"
#include "flux/schematic/io/schematic_file_io.h"

namespace {

QJsonValue sanitizeValue(const QJsonValue& value);

QJsonObject sanitizeObject(QJsonObject object) {
    static const QSet<QString> volatileKeys = {
        "id",
        "createdAt",
        "modifiedAt",
        "exportedAt"
    };

    for (auto it = object.begin(); it != object.end();) {
        if (volatileKeys.contains(it.key())) {
            it = object.erase(it);
            continue;
        }

        it.value() = sanitizeValue(it.value());
        ++it;
    }
    return object;
}

QJsonArray sanitizeArray(const QJsonArray& array) {
    QJsonArray out;
    for (const QJsonValue& v : array) {
        out.append(sanitizeValue(v));
    }
    return out;
}

QJsonValue sanitizeValue(const QJsonValue& value) {
    if (value.isObject()) {
        return sanitizeObject(value.toObject());
    }
    if (value.isArray()) {
        return sanitizeArray(value.toArray());
    }
    return value;
}

QString itemSortKey(const QJsonObject& item) {
    const QString type = item.value("type").toString();
    const QString ref = item.value("reference").toString();
    const QString name = item.value("name").toString();
    const QString value = item.value("value").toString();
    const double x = item.value("x").toDouble();
    const double y = item.value("y").toDouble();
    return QString("%1|%2|%3|%4|%5|%6")
        .arg(type, ref, name, value)
        .arg(x, 0, 'f', 6)
        .arg(y, 0, 'f', 6);
}

void normalizeItems(QJsonObject& root) {
    if (!root.contains("items") || !root.value("items").isArray()) {
        return;
    }

    const QJsonArray arr = root.value("items").toArray();
    std::vector<QJsonObject> items;
    items.reserve(static_cast<size_t>(arr.size()));

    for (const QJsonValue& value : arr) {
        if (value.isObject()) {
            items.push_back(value.toObject());
        }
    }

    std::sort(items.begin(), items.end(), [](const QJsonObject& a, const QJsonObject& b) {
        return itemSortKey(a) < itemSortKey(b);
    });

    QJsonArray normalized;
    for (const QJsonObject& obj : items) {
        normalized.append(obj);
    }
    root["items"] = normalized;
}

bool verifyPcbRoundTrip(const QString& fixturesDir, QString& err) {
#if !VIOSPICE_HAS_PCB
    Q_UNUSED(fixturesDir);
    err.clear();
    return true;
#else
    const QString fixture = QDir(fixturesDir).filePath("test_fix.pcb");

    QGraphicsScene sceneA;
    if (!PCBFileIO::loadPCB(&sceneA, fixture)) {
        err = QString("PCB load failed: %1").arg(PCBFileIO::lastError());
        return false;
    }

    if (sceneA.items().isEmpty()) {
        err = "PCB fixture loaded with zero items";
        return false;
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        err = "failed to create temporary directory for PCB round-trip";
        return false;
    }

    const QString outputFile = QDir(tempDir.path()).filePath("roundtrip.pcb");
    if (!PCBFileIO::savePCB(&sceneA, outputFile)) {
        err = QString("PCB save failed: %1").arg(PCBFileIO::lastError());
        return false;
    }

    QGraphicsScene sceneB;
    if (!PCBFileIO::loadPCB(&sceneB, outputFile)) {
        err = QString("PCB round-trip reload failed: %1").arg(PCBFileIO::lastError());
        return false;
    }

    QJsonObject expected = PCBFileIO::serializeSceneToJson(&sceneA);
    QJsonObject actual = PCBFileIO::serializeSceneToJson(&sceneB);

    expected = sanitizeObject(expected);
    actual = sanitizeObject(actual);
    normalizeItems(expected);
    normalizeItems(actual);

    if (expected != actual) {
        err = "PCB golden round-trip mismatch";
        return false;
    }

    return true;
#endif
}

bool verifySchematicRoundTrip(const QString& fixturesDir, QString& err) {
    const QString fixture = QDir(fixturesDir).filePath("untitled.sch");

    QGraphicsScene sceneA;
    QString pageSizeA;
    TitleBlockData titleBlock;
    QString script;
    QMap<QString, QList<QString>> busAliases;
    QSet<QString> ercExclusions;

    if (!SchematicFileIO::loadSchematic(&sceneA, fixture, pageSizeA, titleBlock, &script, &busAliases, &ercExclusions)) {
        err = QString("Schematic load failed: %1").arg(SchematicFileIO::lastError());
        return false;
    }

    if (sceneA.items().isEmpty()) {
        err = "Schematic fixture loaded with zero items";
        return false;
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        err = "failed to create temporary directory for schematic round-trip";
        return false;
    }

    const QString outputFile = QDir(tempDir.path()).filePath("roundtrip.sch");
    if (!SchematicFileIO::saveSchematic(&sceneA, outputFile, pageSizeA, script, titleBlock, busAliases, ercExclusions)) {
        err = QString("Schematic save failed: %1").arg(SchematicFileIO::lastError());
        return false;
    }

    QGraphicsScene sceneB;
    QString pageSizeB;
    TitleBlockData titleBlockB;
    QString scriptB;
    QMap<QString, QList<QString>> busAliasesB;
    QSet<QString> ercExclusionsB;
    if (!SchematicFileIO::loadSchematic(&sceneB, outputFile, pageSizeB, titleBlockB, &scriptB, &busAliasesB, &ercExclusionsB)) {
        err = QString("Schematic round-trip reload failed: %1").arg(SchematicFileIO::lastError());
        return false;
    }

    QJsonObject expected = SchematicFileIO::serializeSceneToJson(&sceneA, pageSizeA);
    QJsonObject actual = SchematicFileIO::serializeSceneToJson(&sceneB, pageSizeB);

    expected = sanitizeObject(expected);
    actual = sanitizeObject(actual);
    normalizeItems(expected);
    normalizeItems(actual);

    if (expected != actual) {
        err = "Schematic golden round-trip mismatch";
        return false;
    }

    return true;
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    const QString fixturesDir = (argc > 1) ? QString::fromLocal8Bit(argv[1])
                                           : QStringLiteral("tests/regression/fixtures");

    #if VIOSPICE_HAS_PCB
    PCBItemRegistry::registerBuiltInItems();
    #endif
    SchematicItemRegistry::registerBuiltInItems();

    QString err;
    if (!verifyPcbRoundTrip(fixturesDir, err)) {
        std::cerr << "[FAIL] PCB regression: " << err.toStdString() << std::endl;
        return 1;
    }

    if (!verifySchematicRoundTrip(fixturesDir, err)) {
        std::cerr << "[FAIL] Schematic regression: " << err.toStdString() << std::endl;
        return 1;
    }

    std::cout << "[PASS] IO golden regression checks passed." << std::endl;
    return 0;
}
