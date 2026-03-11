#include <QtTest/QtTest>
#include "../../../pcb/models/trace_model.h"
#include "../../../pcb/models/pad_model.h"
#include "../../../pcb/models/component_model.h"
#include "../../../pcb/models/copper_pour_model.h"
#include "../../../pcb/models/board_model.h"
#include "../../../pcb/analysis/connectivity_engine.h"
#include "../../../pcb/analysis/drc_engine.h"

// Schematic Models
#include "../../../schematic/models/wire_model.h"
#include "../../../schematic/models/schematic_component_model.h"
#include "../../../schematic/models/schematic_page_model.h"
#include "../../../schematic/models/erc_engine.h"
#include "../../../schematic/analysis/netlist_engine.h"
#include "../../../core/sync_engine.h"

using namespace Flux::Model;
using namespace Flux::Analysis;

class HeadlessEDATest : public QObject {
    Q_OBJECT

private slots:
    // --- PCB TESTS ---
    void testTraceToPadConnectivity() {
        TraceModel t;
        t.setStart(QPointF(0, 0));
        t.setEnd(QPointF(10, 10));
        t.setWidth(0.2);

        PadModel p;
        p.setPos(QPointF(10, 10));
        p.setSize(QSizeF(1.0, 1.0));

        bool touching = ConnectivityEngine::areTouching(&t, &p);
        QVERIFY(touching == true);
    }

    void testGroupNets() {
        QList<TraceModel*> traces;
        TraceModel* t1 = new TraceModel();
        t1->setNetName("VCC");
        traces.append(t1);

        QList<ViaModel*> vias;
        ViaModel* v1 = new ViaModel();
        v1->setNetName("GND");
        vias.append(v1);

        QList<PadModel*> pads;
        PadModel* p1 = new PadModel();
        p1->setNetName("VCC");
        pads.append(p1);

        auto netGroups = ConnectivityEngine::groupItemsByNet(traces, vias, pads);

        QCOMPARE(netGroups["VCC"].size(), 2);
        QCOMPARE(netGroups["GND"].size(), 1);
        
        delete t1; delete v1; delete p1;
    }

    void testComponentModel() {
        ComponentModel comp;
        comp.setName("U1");
        comp.setComponentType("SOIC-8");
        
        PadModel* p1 = new PadModel();
        p1->setPos(QPointF(-2, -2));
        comp.addPad(p1);

        QCOMPARE(comp.pads().size(), 1);

        QJsonObject json = comp.toJson();
        ComponentModel comp2;
        comp2.fromJson(json);
        QCOMPARE(comp2.name(), QString("U1"));
        QCOMPARE(comp2.pads().size(), 1);
    }

    void testCopperPourModel() {
        CopperPourModel pour;
        pour.setNetName("GND");
        QPolygonF poly;
        poly << QPointF(0, 0) << QPointF(10, 0) << QPointF(10, 10) << QPointF(0, 10);
        pour.setPolygon(poly);

        QCOMPARE(pour.polygon().size(), 4);
        QJsonObject json = pour.toJson();
        CopperPourModel pour2;
        pour2.fromJson(json);
        QCOMPARE(pour2.netName(), QString("GND"));
    }

    void testBoardModelAggregation() {
        BoardModel board;
        board.setName("Test Board");
        
        TraceModel* t = new TraceModel();
        t->setNetName("VCC");
        board.addTrace(t);
        
        QCOMPARE(board.traces().size(), 1);
        QJsonObject json = board.toJson();
        BoardModel board2;
        board2.fromJson(json);
        QCOMPARE(board2.name(), QString("Test Board"));
    }

    void testDRCEngineTraceWidth() {
        BoardModel board;
        DRCRules rules;
        rules.setMinTraceWidth(0.25);

        TraceModel* t2 = new TraceModel();
        t2->setWidth(0.15); // Violation!
        board.addTrace(t2);

        auto result = DRCEngine::runFullCheck(&board, rules);
        QCOMPARE(result.errorCount, 1);
        QCOMPARE(result.violations.first().type(), DRCViolation::MinTraceWidth);
    }

    void testDRCEngineClearance() {
        BoardModel board;
        DRCRules rules;
        rules.setMinClearance(0.2);

        TraceModel* t = new TraceModel();
        t->setNetName("NetA");
        t->setStart(QPointF(0, 0));
        t->setEnd(QPointF(10, 0));
        t->setWidth(0.2);
        board.addTrace(t);

        ViaModel* v = new ViaModel();
        v->setNetName("NetB");
        v->setPos(QPointF(0.1, 0.1));
        v->setDiameter(0.6);
        board.addVia(v);

        auto result = DRCEngine::runFullCheck(&board, rules);
        QCOMPARE(result.errorCount, 1);
        QCOMPARE(result.violations.first().type(), DRCViolation::ClearanceViolation);
    }

    // --- SCHEMATIC TESTS ---
    void testSchematicWireModel() {
        WireModel wire;
        wire.addPoint(QPointF(0, 0));
        wire.addPoint(QPointF(10, 0));
        wire.setNetName("VCC");

        QCOMPARE(wire.length(), 10.0);
        QCOMPARE(wire.netName(), QString("VCC"));

        QJsonObject json = wire.toJson();
        WireModel wire2;
        wire2.fromJson(json);
        QCOMPARE(wire2.length(), 10.0);
        QCOMPARE(wire2.points().size(), 2);
    }

    void testERCEngineUnconnectedPin() {
        SchematicPageModel page;
        
        // Add a component with one pin at (0, 10)
        SchematicComponentModel* comp = new SchematicComponentModel();
        comp->setReference("R1");
        comp->setPos(QPointF(0, 0));
        
        PinModel* p = new PinModel();
        p->pos = QPointF(0, 10);
        comp->addPin(p);
        page.addComponent(comp);

        // Run ERC (R1 is not connected)
        auto result = ERCEngine::runPageCheck(&page);
        QCOMPARE(result.warningCount, 2); // 1. Unconnected pin 2. Missing value

        // Now add a wire that connects to it
        WireModel* wire = new WireModel();
        wire->addPoint(QPointF(0, 10)); // Exact hit on pin
        wire->addPoint(QPointF(10, 10));
        page.addWire(wire);

        auto result2 = ERCEngine::runPageCheck(&page);
        QCOMPARE(result2.warningCount, 1); // Only missing value remains
    }

    void testNetlistGeneration() {
        SchematicPageModel page;

        // R1: Pin 2 at (100, 100)
        SchematicComponentModel* r1 = new SchematicComponentModel();
        r1->setReference("R1");
        r1->setPos(QPointF(40, 100)); // Body at 40
        PinModel* p1_2 = new PinModel();
        p1_2->pos = QPointF(60, 0); // Pin 2 is 60 units right of body center -> (100, 100)
        r1->addPin(p1_2);
        page.addComponent(r1);

        // R2: Pin 1 at (100, 100)
        SchematicComponentModel* r2 = new SchematicComponentModel();
        r2->setReference("R2");
        r2->setPos(QPointF(160, 100));
        PinModel* p2_1 = new PinModel();
        p2_1->pos = QPointF(-60, 0); // Pin 1 is 60 units left of body center -> (100, 100)
        r2->addPin(p2_1);
        page.addComponent(r2);

        // A wire that covers (100, 100)
        WireModel* wire = new WireModel();
        wire->addPoint(QPointF(90, 100));
        wire->addPoint(QPointF(110, 100)); // Crosses the pins
        page.addWire(wire);

        auto netlist = NetlistEngine::generateNetlist(&page);

        // We expect a net containing both R1 and R2
        bool foundConnection = false;
        for (const auto& net : netlist) {
            bool hasR1 = false;
            bool hasR2 = false;
            for (const auto& conn : net.connections) {
                if (conn.componentRef == "R1") hasR1 = true;
                if (conn.componentRef == "R2") hasR2 = true;
            }
            if (hasR1 && hasR2) foundConnection = true;
        }

        QVERIFY(foundConnection == true);
    }

    void testSchematicToPCBSync() {
        // 1. Setup Schematic with one component R1
        SchematicPageModel schematic;
        SchematicComponentModel* sComp = new SchematicComponentModel();
        sComp->setReference("R1");
        sComp->setFootprint("R0603");
        
        PinModel* p1 = new PinModel();
        p1->name = "1"; p1->pos = QPointF(-60, 0);
        sComp->addPin(p1);
        schematic.addComponent(sComp);

        // 2. Setup empty PCB Board
        BoardModel board;

        // 3. Generate ECO (Forward Sync)
        ECOPackage eco = SyncEngine::generateForwardECO(&schematic, &board);
        QCOMPARE(eco.components.size(), 1);
        QCOMPARE(eco.components.first().reference, QString("R1"));

        // 4. Apply ECO to Board
        SyncEngine::applyECOToBoard(eco, &board);

        // 5. Verify Board now has R1 with the correct footprint
        QCOMPARE(board.components().size(), 1);
        QCOMPARE(board.components().first()->name(), QString("R1"));
        QCOMPARE(board.components().first()->componentType(), QString("R0603"));
    }
};

QTEST_APPLESS_MAIN(HeadlessEDATest)
#include "headless_eda_test.moc"
