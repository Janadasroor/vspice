#include <QtTest/QtTest>

#include "../analysis/spice_netlist_generator.h"
#include "../items/power_item.h"
#include "../items/schematic_spice_directive_item.h"

#include <QGraphicsScene>

class SpiceDirectiveNetlistTest : public QObject {
    Q_OBJECT

private slots:
    void generatesWarningsAndHonorsManualDirectives();
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

QTEST_MAIN(SpiceDirectiveNetlistTest)
#include "test_spice_directive_netlist.moc"
