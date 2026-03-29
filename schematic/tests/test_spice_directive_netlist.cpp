#include <QtTest/QtTest>

#include "../analysis/spice_netlist_generator.h"
#include "../items/power_item.h"
#include "../items/schematic_spice_directive_item.h"
#include "../../core/simulation_manager.h"

#include <QGraphicsScene>
#include <QTemporaryFile>
#include <QTextStream>

class SpiceDirectiveNetlistTest : public QObject {
    Q_OBJECT

private slots:
    void generatesWarningsAndHonorsManualDirectives();
    void reportsDuplicateElementsAndUnclosedSubckts();
    void rewritesSimpleLtspiceIfExpressions();
    void rewritesIfWithTrueAndFalseBranches();
    void rewritesLtspiceBooleanOperators();
    void warnsAboutLtspiceMeasForms();
    void loadsBoostConverterLtspiceDirectiveInNgspice();
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

void SpiceDirectiveNetlistTest::loadsBoostConverterLtspiceDirectiveInNgspice() {
    if (!SimulationManager::instance().isAvailable()) {
        QSKIP("Ngspice is not available in this build.");
    }

    QGraphicsScene scene;

    auto* directive = new SchematicSpiceDirectiveItem(
        "* 10-Phase DC-DC Boost Converter (Transient & Solver Optimized)\n"
        ".options cshunt=1p\n"
        ".param Fsw=100k\n"
        ".param V_in=12\n"
        ".param Kp=0.02\n"
        ".param Ki=500\n"
        ".param Tperiod={1/Fsw}\n"
        ".param Tstep={Tperiod/10}\n"
        ".param DCR=15m\n"
        ".param ESR=20m\n"
        ".param Rds_on=5m\n"
        ".param Vd_fwd=0.4\n"
        "Vin in 0 {V_in}\n"
        "Cout out 0 220u Rser={ESR}\n"
        "I_load out 0 PWL(0 1 1.9m 1 2.0m 10 3.9m 10 4.0m 8)\n"
        ".model SW_ideal SW(Ron={Rds_on} Roff=1Meg Vt=0.5)\n"
        ".model D_ideal D(Ron=10m Vfwd={Vd_fwd} Cjo=10p)\n"
        "V_ref target_voltage 0 PWL(0 12 1m 24)\n"
        "B_err err 0 V=V(target_voltage)-V(out)\n"
        "B_pi pi_out 0 V={Kp}*V(err)+{Ki}*idt(V(err))\n"
        "B_duty duty 0 V=max(0.05,min(0.90,V(pi_out)))\n"
        "V_saw1 saw1 0 PULSE(0 1 0 {Tperiod - 20n} 10n 10n {Tperiod})\n"
        "B_pwm1 ctrl1 0 V=max(0,min(1,(V(duty)-V(saw1))*1000))\n"
        "L1 in sw1 10u Rser={DCR}\n"
        "S1 sw1 0 ctrl1 0 SW_ideal\n"
        "D1 sw1 out D_ideal\n"
        "V_saw2 saw2 0 PULSE(0 1 {1*Tstep} {Tperiod - 20n} 10n 10n {Tperiod})\n"
        "B_pwm2 ctrl2 0 V=max(0,min(1,(V(duty)-V(saw2))*1000))\n"
        "L2 in sw2 10u Rser={DCR}\n"
        "S2 sw2 0 ctrl2 0 SW_ideal\n"
        "D2 sw2 out D_ideal\n"
        ".tran 0 5m 0 100n startup\n"
        ".end",
        QPointF(0, 0));
    scene.addItem(directive);

    SpiceNetlistGenerator::SimulationParams params;
    params.type = SpiceNetlistGenerator::Transient;
    params.step = "100n";
    params.stop = "5m";

    const QString netlist = SpiceNetlistGenerator::generate(&scene, QString(), nullptr, params);

    QVERIFY2(netlist.contains("R__RSER_Cout out Cout__rser {ESR}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("Cout Cout__rser 0 220u"), qPrintable(netlist));
    QVERIFY2(netlist.contains("R__RSER_L1 in L1__rser {DCR}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("L1 L1__rser sw1 10u"), qPrintable(netlist));
    QVERIFY2(netlist.contains("B__INTDRV_B_pi 0 B_pi__idt I=({Ki})*(V(err))"), qPrintable(netlist));
    QVERIFY2(netlist.contains("C__INT_B_pi B_pi__idt 0 1"), qPrintable(netlist));
    QVERIFY2(netlist.contains("R__INTLEAK_B_pi B_pi__idt 0 1G"), qPrintable(netlist));
    QVERIFY2(netlist.contains("B_pi pi_out 0 V={Kp}*V(err) + V(B_pi__idt)"), qPrintable(netlist));
    QVERIFY2(netlist.contains(".model D_ideal D(Is=1e-14 Rs=10m Cjo=10p N=1)"), qPrintable(netlist));
    QVERIFY2(netlist.contains("B_err err 0 V={V(target_voltage)-V(out)}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("B_duty duty 0 V={max(0.05,min(0.90,V(pi_out)))}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("B_pwm1 ctrl1 0 V={max(0,min(1,(V(duty)-V(saw1))*1000))}"), qPrintable(netlist));
    QVERIFY2(netlist.contains(".tran 100n 5m 0 100n startup"), qPrintable(netlist));
    QVERIFY2(!netlist.contains("\n.end\n.end\n"), qPrintable(netlist));

    QTemporaryFile temp;
    QVERIFY2(temp.open(), "Failed to create temporary netlist file.");
    QTextStream out(&temp);
    out << netlist;
    out.flush();

    QString error;
    QVERIFY2(SimulationManager::instance().validateNetlist(temp.fileName(), &error), qPrintable(error));
}

QTEST_MAIN(SpiceDirectiveNetlistTest)
#include "test_spice_directive_netlist.moc"
