#include <QtTest/QtTest>

#include "../analysis/spice_netlist_generator.h"
#include "../items/power_item.h"
#include "../items/schematic_spice_directive_item.h"

#include <QGraphicsScene>

class SpiceDirectiveNetlistTest : public QObject {
    Q_OBJECT

private slots:
    void generatesWarningsAndHonorsManualDirectives();
    void reportsDuplicateElementsAndUnclosedSubckts();
    void rewritesSimpleLtspiceIfExpressions();
    void rewritesIfWithTrueAndFalseBranches();
    void rewritesLtspiceBooleanOperators();
    void warnsAboutLtspiceMeasForms();
};

void SpiceDirectiveNetlistTest::generatesWarningsAndHonorsManualDirectives() {
    QGraphicsScene scene;

    auto* directive = new SchematicSpiceDirectiveItem(
        ".model OPMOD NPN(Bf=100\n"
        "+ Vaf=100)\n"
        ".model OPMOD NPN(Is=1e-14)\n"
        "Vcc vcc 0 DC 15\n"
        ".tran 10u 10m\n"
        "+ 0 100n",
        QPointF(0, 0));
    scene.addItem(directive);

    auto* vcc = new PowerItem(QPointF(100, 0), PowerItem::VCC);
    vcc->setValue("vcc");
    scene.addItem(vcc);

    SpiceNetlistGenerator::SimulationParams params;
    params.type = SpiceNetlistGenerator::Transient;
    params.step = "1u";
    params.stop = "1m";

    const QString netlist = SpiceNetlistGenerator::generate(&scene, QString(), nullptr, params);

    QVERIFY2(netlist.contains("* Directive Warnings\n"), qPrintable(netlist));
    QVERIFY2(netlist.contains("* Warning: Duplicate .model OPMOD in directive block"), qPrintable(netlist));
    QVERIFY2(netlist.contains("* Warning: Manual directive source already drives schematic power rail vcc; skipped auto-generated rail source."), qPrintable(netlist));
    QVERIFY2(!netlist.contains("V_vcc vcc 0 DC"), qPrintable(netlist));
    QCOMPARE(netlist.count(".tran "), 1);
}

void SpiceDirectiveNetlistTest::reportsDuplicateElementsAndUnclosedSubckts() {
    QGraphicsScene scene;

    auto* directive = new SchematicSpiceDirectiveItem(
        ".subckt opamp 1 2 3 4 5\n"
        "R1 1 2 10k\n"
        "R1 3 4 20k\n"
        ".ac dec 10 1 1Meg",
        QPointF(0, 0));
    scene.addItem(directive);

    SpiceNetlistGenerator::SimulationParams params;
    params.type = SpiceNetlistGenerator::Transient;
    params.step = "1u";
    params.stop = "1m";

    const QString netlist = SpiceNetlistGenerator::generate(&scene, QString(), nullptr, params);

    QVERIFY2(netlist.contains("* Directive Warnings\n"), qPrintable(netlist));
    QVERIFY2(netlist.contains("* Warning: Duplicate element reference R1 in directive block"), qPrintable(netlist));
    QVERIFY2(netlist.contains("* Warning: Missing .ends for subcircuit opamp."), qPrintable(netlist));
    QCOMPARE(netlist.count(".ac dec 10 1 1Meg"), 1);
    QVERIFY2(!netlist.contains(".tran 1u 1m"), qPrintable(netlist));
}

void SpiceDirectiveNetlistTest::rewritesSimpleLtspiceIfExpressions() {
    QGraphicsScene scene;

    auto* directive = new SchematicSpiceDirectiveItem(
        "BGAH GAH 0 V={if(V(REFA)>V(TRI), VG, 0)}\n"
        ".model DBODY D(Ron=0.01 Roff=1e9 Vfwd=0.8)\n"
        ".meas tran Iload_rms RMS I(RLOAD) FROM=1m TO=2m\n"
        ".tran 1u 2m",
        QPointF(0, 0));
    scene.addItem(directive);

    SpiceNetlistGenerator::SimulationParams params;
    params.type = SpiceNetlistGenerator::Transient;
    params.step = "1u";
    params.stop = "2m";

    const QString netlist = SpiceNetlistGenerator::generate(&scene, QString(), nullptr, params);

    QVERIFY2(netlist.contains("* LTspice rewrite: BGAH GAH 0 V={if(V(REFA)>V(TRI), VG, 0)}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("BGAH GAH 0 V={((VG)*(u((V(REFA))-(V(TRI)))))}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("LTspice-style diode model parameters detected"), qPrintable(netlist));
    QVERIFY2(netlist.contains("I(R...) style expressions"), qPrintable(netlist));
    QVERIFY2(!netlist.contains(".tran 1u 2m\n.tran 1u 2m"), qPrintable(netlist));
}

void SpiceDirectiveNetlistTest::rewritesIfWithTrueAndFalseBranches() {
    QGraphicsScene scene;

    auto* directive = new SchematicSpiceDirectiveItem(
        "BDRV G 0 V={if(V(IN)>0.5, 12, -3)}\n"
        ".tran 1u 1m",
        QPointF(0, 0));
    scene.addItem(directive);

    SpiceNetlistGenerator::SimulationParams params;
    params.type = SpiceNetlistGenerator::Transient;
    params.step = "1u";
    params.stop = "1m";

    const QString netlist = SpiceNetlistGenerator::generate(&scene, QString(), nullptr, params);

    QVERIFY2(netlist.contains("* LTspice rewrite: BDRV G 0 V={if(V(IN)>0.5, 12, -3)}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("BDRV G 0 V={((12)*(u((V(IN))-(0.5))) + (-3)*(1-(u((V(IN))-(0.5)))))"), qPrintable(netlist));
}

void SpiceDirectiveNetlistTest::rewritesLtspiceBooleanOperators() {
    QGraphicsScene scene;

    auto* directive = new SchematicSpiceDirectiveItem(
        "BLOGIC OUT 0 V={(V(A)>1)&&(V(B)>1)}\n"
        "BALT OUT2 0 V={(V(A)>1)||(V(B)>1)}\n"
        ".func LUT(x) {table(x, 0,0, 1,1)}\n"
        ".tran 1u 1m",
        QPointF(0, 0));
    scene.addItem(directive);

    SpiceNetlistGenerator::SimulationParams params;
    params.type = SpiceNetlistGenerator::Transient;
    params.step = "1u";
    params.stop = "1m";

    const QString netlist = SpiceNetlistGenerator::generate(&scene, QString(), nullptr, params);

    QVERIFY2(netlist.contains("BLOGIC OUT 0 V={(V(A)>1) and (V(B)>1)}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("BALT OUT2 0 V={(V(A)>1) or (V(B)>1)}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("Rewrote LTspice-style boolean operators"), qPrintable(netlist));
    QVERIFY2(netlist.contains("table(...)"), qPrintable(netlist));
}

void SpiceDirectiveNetlistTest::warnsAboutLtspiceMeasForms() {
    QGraphicsScene scene;

    auto* directive = new SchematicSpiceDirectiveItem(
        ".meas tran VAL1 PARAM V(out)*I(RLOAD)\n"
        ".meas tran VAL2 FIND V(out) AT=1m\n"
        ".tran 1u 2m",
        QPointF(0, 0));
    scene.addItem(directive);

    SpiceNetlistGenerator::SimulationParams params;
    params.type = SpiceNetlistGenerator::Transient;
    params.step = "1u";
    params.stop = "2m";

    const QString netlist = SpiceNetlistGenerator::generate(&scene, QString(), nullptr, params);

    QVERIFY2(netlist.contains(".meas PARAM detected and passed through unchanged"), qPrintable(netlist));
    QVERIFY2(netlist.contains(".meas FIND ... AT= detected"), qPrintable(netlist));
}

QTEST_MAIN(SpiceDirectiveNetlistTest)
#include "test_spice_directive_netlist.moc"
