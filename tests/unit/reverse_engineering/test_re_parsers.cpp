#include <QtTest>
#include <QCoreApplication>
#include "../../../reverse_engineering/normalized_parser.h"
#include "../../../reverse_engineering/drill_parser.h"
#include "../../../reverse_engineering/session.h"

using namespace VioraEDA::ReverseEngineering;

class TestREParsers : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        // Ensure test-files directory exists relative to current run
    }

    void testGerberParsing() {
        Session::FileEntry entry;
        entry.path = "test-files/mm-F_Cu.gbr";
        entry.detectedFormat = "Gerber";
        entry.layerRole = "TopCopper";
        
        NormalizedLayer* layer = NormalizedParser::parseFile(entry);
        QVERIFY(layer != nullptr);
        QCOMPARE(layer->role(), QString("TopCopper"));
        QVERIFY(layer->raw()->primitives().size() > 0);
        
        // F_Cu (Top Copper) in mm-fixture has 7 primitives
        QCOMPARE(layer->raw()->primitives().size(), 7);
        delete layer;
    }

    void testDrillParsing() {
        QString path = "test-files/mm-PTH.drl";
        QList<DrillHole> holes = DrillParser::parse(path);
        
        QVERIFY(!holes.isEmpty());
        // Simple sanity check on coordinate ranges for the mm-fixture
        for (const auto& hole : holes) {
            QVERIFY(hole.position.x() >= -200 && hole.position.x() <= 200);
            QVERIFY(hole.position.y() >= -200 && hole.position.y() <= 200);
        }
    }
};

QTEST_MAIN(TestREParsers)
#include "test_re_parsers.moc"
