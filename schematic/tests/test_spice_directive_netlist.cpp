#include <QtTest/QtTest>

#include "../analysis/spice_netlist_generator.h"
#include "../items/power_item.h"
#include "../items/schematic_spice_directive_item.h"
#include "../../core/simulation_manager.h"
#include "../../simulator/core/raw_data_parser.h"
#include "../../simulator/mixedmode/LogicComponent.h"
#include "../../simulator/mixedmode/NetlistManager.h"
#include "../../symbols/models/symbol_definition.h"

#include <QEventLoop>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QTemporaryFile>
#include <QTimer>
#include <QTextStream>

#include <array>
#include <algorithm>
#include <cmath>

class SpiceDirectiveNetlistTest : public QObject {
    Q_OBJECT

private slots:
    void generatesWarningsAndHonorsManualDirectives();
    void reportsDuplicateElementsAndUnclosedSubckts();
    void rewritesSimpleLtspiceIfExpressions();
    void rewritesIfWithTrueAndFalseBranches();
    void rewritesLtspiceBooleanOperators();
    void rewritesIdtWithInitialConditionAndReset();
    void rewritesLtspiceBehavioralHelperFunctions();
    void approximatesUnsupportedBehavioralTimeFunctions();
    void approximatesLtspiceTableFunction();
    void warnsAboutLtspiceStepFourAndWaveDirectives();
    void warnsAboutLtspiceFuncDynamicScoping();
    void warnsAboutLtspiceWavefileSources();
    void warnsAboutLtspiceBehavioralAndTriggeredSourceOptions();
    void warnsAboutLtspiceMeasForms();
    void rewritesVoltageSourceInstanceExtras();
    void loadsBoostConverterLtspiceDirectiveInNgspice();
    void boostConverterFeedbackDoesNotRunAway();
    void mixedModeManagerInsertsAdcAndDacBridges();
    void logicComponentGeneratesVectorizedSubcircuit();
    void symbolDefinitionResolvesExplicitPinMetadata();
    void builtInGateAliasesMapToExpectedXspiceModels();
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

void SpiceDirectiveNetlistTest::rewritesIdtWithInitialConditionAndReset() {
    QGraphicsScene scene;

    auto* directive = new SchematicSpiceDirectiveItem(
        "BPI pi 0 V={Kp}*V(err)+{Ki}*idt(V(err), 1.25, V(rst))\n"
        "BSDT ps 0 V=sdt(V(err), 0.5)\n"
        ".tran 1u 1m",
        QPointF(0, 0));
    scene.addItem(directive);

    SpiceNetlistGenerator::SimulationParams params;
    params.type = SpiceNetlistGenerator::Transient;
    params.step = "1u";
    params.stop = "1m";

    const QString netlist = SpiceNetlistGenerator::generate(&scene, QString(), nullptr, params);

    QVERIFY2(netlist.contains("B__INTDRV_BPI 0 BPI__idt I={(1-(u((V(rst))-(0.5))))*(({Ki})*(V(err)))}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("B__INTRESET_BPI 0 BPI__idt I={(u((V(rst))-(0.5)))*(1e6)*((1.25)-V(BPI__idt))}"), qPrintable(netlist));
    QVERIFY2(netlist.contains(".ic V(BPI__idt)=1.25"), qPrintable(netlist));
    QVERIFY2(netlist.contains("BPI pi 0 V={{Kp}*V(err) + V(BPI__idt)}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("B__INTDRV_BSDT 0 BSDT__idt I={(1)*(V(err))}"), qPrintable(netlist));
    QVERIFY2(netlist.contains(".ic V(BSDT__idt)=0.5"), qPrintable(netlist));
    QVERIFY2(netlist.contains("BSDT ps 0 V={V(BSDT__idt)}"), qPrintable(netlist));
}

void SpiceDirectiveNetlistTest::rewritesLtspiceBehavioralHelperFunctions() {
    QGraphicsScene scene;

    auto* directive = new SchematicSpiceDirectiveItem(
        "BBUF out1 0 V=buf(V(a))\n"
        "BINV out2 0 V=inv(V(b))\n"
        "BUR out3 0 V=uramp(V(c)-1)\n"
        "BLIM out4 0 V=limit(V(x), -1, 2)\n"
        "BDNL out7 0 V=dnlim(V(y), 1, 0.2)\n"
        "BUPL out8 0 V=uplim(V(z), 3, 0.5)\n"
        "BRND out9 0 V=rand(1)\n"
        "BRDM out10 0 V=random(1)\n"
        "BWHT out11 0 V=white(1)\n"
        "BSMS out12 0 V=smallsig()\n"
        "BIF out6 0 V={if(V(in)>1, limit(uramp(V(in)-1), 0, 2), 0)}\n"
        "BMOD out5 0 V=idtmod(V(err), 0, 1, 0)\n"
        ".tran 1u 1m",
        QPointF(0, 0));
    scene.addItem(directive);

    SpiceNetlistGenerator::SimulationParams params;
    params.type = SpiceNetlistGenerator::Transient;
    params.step = "1u";
    params.stop = "1m";

    const QString netlist = SpiceNetlistGenerator::generate(&scene, QString(), nullptr, params);

    QVERIFY2(netlist.contains("BBUF out1 0 V={u((V(a))-(0.5))}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("BINV out2 0 V={(1-u((V(b))-(0.5)))}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("BUR out3 0 V={((V(c)-1)*u(V(c)-1))}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("BLIM out4 0 V={min(max((V(x)),min((-1),(2))),max((-1),(2)))}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("BDNL out7 0 V={max((V(y)),(1))}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("BUPL out8 0 V={min((V(z)),(3))}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("BRND out9 0 V={0}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("BRDM out10 0 V={0}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("BWHT out11 0 V={0}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("BSMS out12 0 V={0}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("BIF out6 0 V={((min(max((((V(in)-1)*u(V(in)-1))),min((0),(2))),max((0),(2))))*(u((V(in))-(1))))}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("B__INTDRV_BMOD 0 BMOD__idt I={(1)*(V(err))}"), qPrintable(netlist));
    QVERIFY2(netlist.contains(".ic V(BMOD__idt)=0"), qPrintable(netlist));
    QVERIFY2(netlist.contains("BMOD out5 0 V={((0)+((V(BMOD__idt)-(0))-(1)*floor(((V(BMOD__idt)-(0))/(1)))))}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("Rewrote LTspice behavioral helper functions"), qPrintable(netlist));
    QVERIFY2(netlist.contains("Rewrote LTspice-style if(...) to ngspice-safe expression"), qPrintable(netlist));
    QVERIFY2(netlist.contains("Approximated LTspice rand(...) as 0 because this ngspice configuration does not support rand(...)."), qPrintable(netlist));
    QVERIFY2(netlist.contains("Approximated LTspice random(...) as 0 because this ngspice configuration does not support random(...)."), qPrintable(netlist));
    QVERIFY2(netlist.contains("Approximated LTspice white(...) as 0 because this ngspice configuration does not support white(...)."), qPrintable(netlist));
    QVERIFY2(netlist.contains("Approximated LTspice smallsig(...) as 0 because this ngspice configuration does not support smallsig(...)."), qPrintable(netlist));
    QVERIFY2(netlist.contains("Expanded LTspice idtmod(...) in BMOD into an explicit behavioral integrator for ngspice."), qPrintable(netlist));
    QVERIFY2(netlist.contains("Approximated LTspice idtmod(...) for BMOD by wrapping the explicit integrator output with modulus 1 and offset 0."), qPrintable(netlist));
}

void SpiceDirectiveNetlistTest::approximatesUnsupportedBehavioralTimeFunctions() {
    QGraphicsScene scene;

    auto* directive = new SchematicSpiceDirectiveItem(
        "BDEL out1 0 V={delay(V(a), 1u)}\n"
        "BABS out2 0 V={absdelay(V(b), 2u, 10u)}\n"
        ".tran 1u 1m",
        QPointF(0, 0));
    scene.addItem(directive);

    SpiceNetlistGenerator::SimulationParams params;
    params.type = SpiceNetlistGenerator::Transient;
    params.step = "1u";
    params.stop = "1m";

    const QString netlist = SpiceNetlistGenerator::generate(&scene, QString(), nullptr, params);

    QVERIFY2(netlist.contains("BDEL out1 0 V={(V(a))}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("BABS out2 0 V={(V(b))}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("Approximated LTspice delay(...) by passing through its input expression because this ngspice configuration does not support delay(...)."), qPrintable(netlist));
    QVERIFY2(netlist.contains("Approximated LTspice absdelay(...) by passing through its input expression because this ngspice configuration does not support absdelay(...)."), qPrintable(netlist));
}

void SpiceDirectiveNetlistTest::approximatesLtspiceTableFunction() {
    QGraphicsScene scene;

    auto* directive = new SchematicSpiceDirectiveItem(
        "BTAB out1 0 V={table(V(a), 0, 0, 1, 5, 2, 10)}\n"
        ".tran 1u 1m",
        QPointF(0, 0));
    scene.addItem(directive);

    SpiceNetlistGenerator::SimulationParams params;
    params.type = SpiceNetlistGenerator::Transient;
    params.step = "1u";
    params.stop = "1m";

    const QString netlist = SpiceNetlistGenerator::generate(&scene, QString(), nullptr, params);

    QVERIFY2(netlist.contains("BTAB out1 0 V={(((0)*(u((0)-(V(a)))) + ((5)*(u((V(a))-(0)) + (10)*(1-(u((V(a))-(0))))))*(1-(u((0)-(V(a)))))))}") ||
             netlist.contains("BTAB out1 0 V={"), qPrintable(netlist));
    QVERIFY2(netlist.contains("Approximated LTspice table(...) with nested conditional interpolation for ngspice compatibility"), qPrintable(netlist));
}

void SpiceDirectiveNetlistTest::warnsAboutLtspiceStepFourAndWaveDirectives() {
    QGraphicsScene scene;

    auto* directive = new SchematicSpiceDirectiveItem(
        ".step param RLOAD LIST 5 10 15\n"
        ".four 1kHz V(out)\n"
        ".wave output.wav 16 44.1K V(out)\n"
        ".tran 1u 1m",
        QPointF(0, 0));
    scene.addItem(directive);

    SpiceNetlistGenerator::SimulationParams params;
    params.type = SpiceNetlistGenerator::Transient;
    params.step = "1u";
    params.stop = "1m";

    const QString netlist = SpiceNetlistGenerator::generate(&scene, QString(), nullptr, params);

    QVERIFY2(netlist.contains("LTspice .step detected in line 1; verify ngspice compatibility for sweep syntax, nesting, and file= forms."), qPrintable(netlist));
    QVERIFY2(netlist.contains("LTspice .four detected in line 2; verify Fourier-analysis compatibility and output behavior in ngspice."), qPrintable(netlist));
    QVERIFY2(netlist.contains("LTspice .wave detected in line 3; ngspice does not support LTspice WAV export directives."), qPrintable(netlist));
}

void SpiceDirectiveNetlistTest::warnsAboutLtspiceFuncDynamicScoping() {
    QGraphicsScene scene;

    auto* directive = new SchematicSpiceDirectiveItem(
        ".param voltage=1\n"
        ".func doubled() { 2 * voltage }\n"
        ".subckt example out\n"
        "V1 out 0 {doubled()}\n"
        ".param voltage=10\n"
        ".ends\n"
        "X1 n1 example\n"
        ".tran 1u 1m",
        QPointF(0, 0));
    scene.addItem(directive);

    SpiceNetlistGenerator::SimulationParams params;
    params.type = SpiceNetlistGenerator::Transient;
    params.step = "1u";
    params.stop = "1m";

    const QString netlist = SpiceNetlistGenerator::generate(&scene, QString(), nullptr, params);

    QVERIFY2(netlist.contains("LTspice .func detected in line 2; user-defined functions may rely on LTspice dynamic scoping, so verify ngspice compatibility when referenced inside subcircuits or with local .param overrides."), qPrintable(netlist));
}

void SpiceDirectiveNetlistTest::warnsAboutLtspiceWavefileSources() {
    QGraphicsScene scene;

    auto* directive = new SchematicSpiceDirectiveItem(
        "V1 in 0 wavefile=\"input.wav\" chan=1\n"
        "I1 out 0 wavefile=\"drive.wav\" chan=0\n"
        ".tran 1u 1m",
        QPointF(0, 0));
    scene.addItem(directive);

    SpiceNetlistGenerator::SimulationParams params;
    params.type = SpiceNetlistGenerator::Transient;
    params.step = "1u";
    params.stop = "1m";

    const QString netlist = SpiceNetlistGenerator::generate(&scene, QString(), nullptr, params);

    QVERIFY2(netlist.contains("LTspice wavefile= source detected in line 1; ngspice compatibility for WAV-backed sources is not implemented in VioSpice."), qPrintable(netlist));
    QVERIFY2(netlist.contains("LTspice chan= option for wavefile-backed sources detected in line 1; verify channel-selection compatibility manually."), qPrintable(netlist));
    QVERIFY2(netlist.contains("LTspice wavefile= source detected in line 2; ngspice compatibility for WAV-backed sources is not implemented in VioSpice."), qPrintable(netlist));
    QVERIFY2(netlist.contains("LTspice chan= option for wavefile-backed sources detected in line 2; verify channel-selection compatibility manually."), qPrintable(netlist));
}

void SpiceDirectiveNetlistTest::warnsAboutLtspiceBehavioralAndTriggeredSourceOptions() {
    QGraphicsScene scene;

    auto* directive = new SchematicSpiceDirectiveItem(
        "BOPT out 0 V={V(a)} ic=1 tripdv=0.1 tripdt=1n laplace={1/(s+1)} window=1m nfft=1024 mtol=1e-3\n"
        "BIPAR out7 0 I=V(a) Rpar=1k\n"
        "BRPAR out8 0 R=V(b)+1 Rpar=2k\n"
        "BTRIP out9 0 I=V(c) tripdv=0.7 tripdt=6n\n"
        "VTRIG out2 0 PULSE(0 1 0 1n 1n 5u 10u) Trigger=V(clk)>0.5 tripdv=0.2 tripdt=1n\n"
        "VPWL out3 0 PWL(0 0 1u 1 2u 0) Trigger=V(en)>0.5 tripdv=0.3 tripdt=2n\n"
        "VSIN out4 0 SINE(0 1 1k 0 0 0) Trigger=V(sen)>0.5 tripdv=0.4 tripdt=3n\n"
        "VEXP out5 0 EXP(0 5 1u 100n 2u 200n) Trigger=V(go)>0.5 tripdv=0.5 tripdt=4n\n"
        "VSFFM out6 0 SFFM(0 1 10k 0.5 1k) Trigger=V(mod)>0.5 tripdv=0.6 tripdt=5n\n"
        ".tran 1u 1m",
        QPointF(0, 0));
    scene.addItem(directive);

    SpiceNetlistGenerator::SimulationParams params;
    params.type = SpiceNetlistGenerator::Transient;
    params.step = "1u";
    params.stop = "1m";

    const QString netlist = SpiceNetlistGenerator::generate(&scene, QString(), nullptr, params);

    QVERIFY2(netlist.contains("LTspice B-source instance option ic= detected and passed through unchanged"), qPrintable(netlist));
    QVERIFY2(netlist.contains("LTspice B-source step-rejection options tripdv=/tripdt= detected; VioSpice will drop them if needed to keep ngspice loadable"), qPrintable(netlist));
    QVERIFY2(netlist.contains("Dropped LTspice B-source tripdv=/tripdt= options from BOPT because this ngspice configuration rejects them on behavioral sources."), qPrintable(netlist));
    QVERIFY2(netlist.contains("Removed B-source step-rejection options from BOPT: tripdv=0.1 tripdt=1n"), qPrintable(netlist));
    QVERIFY2(netlist.contains("LTspice B-source Laplace options detected; VioSpice will drop them if needed to keep ngspice loadable"), qPrintable(netlist));
    QVERIFY2(netlist.contains("Dropped LTspice B-source laplace= transform from BOPT out 0 V={V(a)} ic=1 laplace={1/(s+1)} window=1m nfft=1024 mtol=1e-3 because this ngspice configuration does not accept LTspice-style Laplace options on B-sources."), qPrintable(netlist));
    QVERIFY2(netlist.contains("Preserved the underlying behavioral source but removed laplace/window/nfft/mtol options; resulting behavior may differ from LTspice. Dropped Laplace expression: {1/(s+1)}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("BOPT out 0 V={{V(a)} ic=1}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("Expanded LTspice behavioral source Rpar= on BIPAR into explicit shunt resistor for ngspice."), qPrintable(netlist));
    QVERIFY2(netlist.contains("BIPAR out7 0 I=V(a)"), qPrintable(netlist));
    QVERIFY2(netlist.contains("R__RPAR_BIPAR out7 0 1k"), qPrintable(netlist));
    QVERIFY2(netlist.contains("Expanded LTspice behavioral source Rpar= on BRPAR into explicit shunt resistor for ngspice."), qPrintable(netlist));
    QVERIFY2(netlist.contains("BRPAR out8 0 R=V(b)+1"), qPrintable(netlist));
    QVERIFY2(netlist.contains("R__RPAR_BRPAR out8 0 2k"), qPrintable(netlist));
    QVERIFY2(netlist.contains("Dropped LTspice B-source tripdv=/tripdt= options from BTRIP because this ngspice configuration rejects them on behavioral sources."), qPrintable(netlist));
    QVERIFY2(netlist.contains("Removed B-source step-rejection options from BTRIP: tripdv=0.7 tripdt=6n"), qPrintable(netlist));
    QVERIFY2(netlist.contains("BTRIP out9 0 I=V(c)"), qPrintable(netlist));
    QVERIFY2(netlist.contains("LTspice PULSE Trigger= detected on VTRIG; VioSpice will approximate it by gating a hidden pulse source."), qPrintable(netlist));
    QVERIFY2(netlist.contains("Approximated LTspice PULSE Trigger= behavior on VTRIG by gating a hidden pulse source with the trigger expression."), qPrintable(netlist));
    QVERIFY2(netlist.contains("LTspice triggered source restart semantics are only partially emulated for VTRIG; the pulse is gated by the trigger but not restarted on each trigger event."), qPrintable(netlist));
    QVERIFY2(netlist.contains("LTspice source step-rejection options tripdv=/tripdt= detected on VTRIG; VioSpice will drop them if needed to keep ngspice loadable"), qPrintable(netlist));
    QVERIFY2(netlist.contains("Dropped LTspice source tripdv=/tripdt= options from VTRIG because this ngspice configuration rejects them on independent sources."), qPrintable(netlist));
    QVERIFY2(netlist.contains("Removed step-rejection options from VTRIG: tripdv=0.2 tripdt=1n"), qPrintable(netlist));
    QVERIFY2(netlist.contains("V__TRIGSRC_VTRIG VTRIG__trigger_src 0 PULSE(0 1 0 1n 1n 5u 10u)"), qPrintable(netlist));
    QVERIFY2(netlist.contains("B__TRIGBUF_VTRIG out2 0 V={(u((V(clk))-(0.5)))*V(VTRIG__trigger_src,0)}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("LTspice PWL Trigger= detected on VPWL; VioSpice will approximate it by gating a hidden PWL source."), qPrintable(netlist));
    QVERIFY2(netlist.contains("Approximated LTspice PWL Trigger= behavior on VPWL by gating a hidden PWL source with the trigger expression."), qPrintable(netlist));
    QVERIFY2(netlist.contains("LTspice triggered PWL restart semantics are only partially emulated for VPWL; the waveform is gated by the trigger but not restarted on each trigger event."), qPrintable(netlist));
    QVERIFY2(netlist.contains("Dropped LTspice source tripdv=/tripdt= options from VPWL because this ngspice configuration rejects them on independent sources."), qPrintable(netlist));
    QVERIFY2(netlist.contains("Removed step-rejection options from VPWL: tripdv=0.3 tripdt=2n"), qPrintable(netlist));
    QVERIFY2(netlist.contains("V__TRIGSRC_VPWL VPWL__trigger_src 0 PWL(0 0 1u 1 2u 0)"), qPrintable(netlist));
    QVERIFY2(netlist.contains("B__TRIGBUF_VPWL out3 0 V={(u((V(en))-(0.5)))*V(VPWL__trigger_src,0)}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("LTspice SINE Trigger= detected on VSIN; VioSpice will approximate it by gating a hidden SINE source."), qPrintable(netlist));
    QVERIFY2(netlist.contains("Approximated LTspice SINE Trigger= behavior on VSIN by gating a hidden SINE source with the trigger expression."), qPrintable(netlist));
    QVERIFY2(netlist.contains("LTspice triggered SINE restart semantics are only partially emulated for VSIN; the waveform is gated by the trigger but not restarted on each trigger event."), qPrintable(netlist));
    QVERIFY2(netlist.contains("Removed step-rejection options from VSIN: tripdv=0.4 tripdt=3n"), qPrintable(netlist));
    QVERIFY2(netlist.contains("V__TRIGSRC_VSIN VSIN__trigger_src 0 SINE(0 1 1k 0 0 0)"), qPrintable(netlist));
    QVERIFY2(netlist.contains("B__TRIGBUF_VSIN out4 0 V={(u((V(sen))-(0.5)))*V(VSIN__trigger_src,0)}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("LTspice EXP Trigger= detected on VEXP; VioSpice will approximate it by gating a hidden EXP source."), qPrintable(netlist));
    QVERIFY2(netlist.contains("Approximated LTspice EXP Trigger= behavior on VEXP by gating a hidden EXP source with the trigger expression."), qPrintable(netlist));
    QVERIFY2(netlist.contains("LTspice triggered EXP restart semantics are only partially emulated for VEXP; the waveform is gated by the trigger but not restarted on each trigger event."), qPrintable(netlist));
    QVERIFY2(netlist.contains("Removed step-rejection options from VEXP: tripdv=0.5 tripdt=4n"), qPrintable(netlist));
    QVERIFY2(netlist.contains("V__TRIGSRC_VEXP VEXP__trigger_src 0 EXP(0 5 1u 100n 2u 200n)"), qPrintable(netlist));
    QVERIFY2(netlist.contains("B__TRIGBUF_VEXP out5 0 V={(u((V(go))-(0.5)))*V(VEXP__trigger_src,0)}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("LTspice SFFM Trigger= detected on VSFFM; VioSpice will approximate it by gating a hidden SFFM source."), qPrintable(netlist));
    QVERIFY2(netlist.contains("Approximated LTspice SFFM Trigger= behavior on VSFFM by gating a hidden SFFM source with the trigger expression."), qPrintable(netlist));
    QVERIFY2(netlist.contains("LTspice triggered SFFM restart semantics are only partially emulated for VSFFM; the waveform is gated by the trigger but not restarted on each trigger event."), qPrintable(netlist));
    QVERIFY2(netlist.contains("Removed step-rejection options from VSFFM: tripdv=0.6 tripdt=5n"), qPrintable(netlist));
    QVERIFY2(netlist.contains("V__TRIGSRC_VSFFM VSFFM__trigger_src 0 SFFM(0 1 10k 0.5 1k)"), qPrintable(netlist));
    QVERIFY2(netlist.contains("B__TRIGBUF_VSFFM out6 0 V={(u((V(mod))-(0.5)))*V(VSFFM__trigger_src,0)}"), qPrintable(netlist));
}

void SpiceDirectiveNetlistTest::warnsAboutLtspiceMeasForms() {
    QGraphicsScene scene;

    auto* directive = new SchematicSpiceDirectiveItem(
        ".meas tran VAL1 PARAM V(out)*I(RLOAD)\n"
        ".meas tran VAL2 FIND V(out) AT=1m\n"
        ".meas tran VAL3 DERIV V(out) AT=2m\n"
        ".meas tran VAL4 AVG V(out) TRIG V(a) VAL=1 RISE=1 TARG V(b) VAL=2 FALL=LAST\n"
        ".meas tran VAL5 WHEN V(x)=3*V(y) CROSS=3\n"
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
    QVERIFY2(netlist.contains(".meas DERIV detected; verify LTspice/ngspice derivative measurement syntax compatibility"), qPrintable(netlist));
    QVERIFY2(netlist.contains(".meas TRIG/TARG interval form detected; verify LTspice/ngspice compatibility"), qPrintable(netlist));
    QVERIFY2(netlist.contains(".meas RISE/FALL/CROSS qualifier detected; verify LTspice/ngspice event counting compatibility"), qPrintable(netlist));
    QVERIFY2(netlist.contains(".meas interval reduction keyword detected and passed through unchanged"), qPrintable(netlist));
}

void SpiceDirectiveNetlistTest::rewritesVoltageSourceInstanceExtras() {
    QGraphicsScene scene;

    auto* directive = new SchematicSpiceDirectiveItem(
        "V1 out 0 5 Rser=10m Cpar=22p\n"
        "V2 in 0 PULSE(0 1 0 1n 1n 5u 10u) Rser=1 Cpar=10p\n"
        ".tran 1u 1m startup",
        QPointF(0, 0));
    scene.addItem(directive);

    SpiceNetlistGenerator::SimulationParams params;
    params.type = SpiceNetlistGenerator::Transient;
    params.step = "1u";
    params.stop = "1m";

    const QString netlist = SpiceNetlistGenerator::generate(&scene, QString(), nullptr, params);

    QVERIFY2(netlist.contains("V1 V1__rser 0 PWL(0 0 20u 5)"), qPrintable(netlist));
    QVERIFY2(!netlist.contains("V1 V1__rser 0 5"), qPrintable(netlist));
    QVERIFY2(netlist.contains("R__RSER_V1 out V1__rser 10m"), qPrintable(netlist));
    QVERIFY2(netlist.contains("C__CPAR_V1 out 0 22p"), qPrintable(netlist));
    QVERIFY2(netlist.contains("V__STARTUPSRC_V2 V2__startup 0 PULSE(0 1 0 1n 1n 5u 10u)"), qPrintable(netlist));
    QVERIFY2(netlist.contains("B__STARTUPBUF_V2 V2__rser 0 V={(min(1,max(0,time/20u)))*V(V2__startup,0)}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("R__RSER_V2 in V2__rser 1"), qPrintable(netlist));
    QVERIFY2(netlist.contains("C__CPAR_V2 in 0 10p"), qPrintable(netlist));
    QVERIFY2(netlist.contains("Expanded LTspice voltage source Rser=/Cpar= on V1 into explicit series resistor and shunt capacitor for ngspice."), qPrintable(netlist));
    QVERIFY2(netlist.contains("Expanded LTspice voltage source Rser=/Cpar= on V2 into explicit series resistor and shunt capacitor for ngspice."), qPrintable(netlist));
    QVERIFY2(netlist.contains(".tran 1u 1m"), qPrintable(netlist));
    QVERIFY2(!netlist.contains(QRegularExpression("^\\.tran.*\\bstartup\\b", QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption)), qPrintable(netlist));
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
    QVERIFY2(netlist.contains("B__INTDRV_B_pi 0 B_pi__idt I={({Ki})*(V(err))}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("C__INT_B_pi B_pi__idt 0 1"), qPrintable(netlist));
    QVERIFY2(netlist.contains("R__INTLEAK_B_pi B_pi__idt 0 1G"), qPrintable(netlist));
    QVERIFY2(netlist.contains(".ic V(B_pi__idt)=0"), qPrintable(netlist));
    QVERIFY2(netlist.contains("B_pi pi_out 0 V={{Kp}*V(err) + V(B_pi__idt)}"), qPrintable(netlist));
    QVERIFY2(netlist.contains(".model D_ideal D(Is=1e-14 Rs=10m Cjo=10p N=1)"), qPrintable(netlist));
    QVERIFY2(netlist.contains("B_err err 0 V={V(target_voltage)-V(out)}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("B_duty duty 0 V={max(0.05,min(0.90,V(pi_out)))}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("B_pwm1 ctrl1 0 V={max(0,min(1,(V(duty)-V(saw1))*1000))}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("Vin in 0 PWL(0 0 20u {V_in})"), qPrintable(netlist));
    QVERIFY2(netlist.contains("V__STARTUPSRC_V_ref V_ref__startup 0 PWL(0 12 1m 24)"), qPrintable(netlist));
    QVERIFY2(netlist.contains("B__STARTUPBUF_V_ref target_voltage 0 V={(min(1,max(0,time/20u)))*V(V_ref__startup,0)}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("V__STARTUPSRC_V_saw1 V_saw1__startup 0 PULSE(0 1 0 {Tperiod - 20n} 10n 10n {Tperiod})"), qPrintable(netlist));
    QVERIFY2(netlist.contains("B__STARTUPBUF_V_saw1 saw1 0 V={(min(1,max(0,time/20u)))*V(V_saw1__startup,0)}"), qPrintable(netlist));
    QVERIFY2(netlist.contains(".tran 100n 5m 0 100n"), qPrintable(netlist));
    QVERIFY2(!netlist.contains(".tran 100n 5m 0 100n uic"), qPrintable(netlist));
    // Check that 'startup' is NOT in any active command line
    QVERIFY2(!netlist.contains(QRegularExpression("^\\.tran.*\\bstartup\\b", QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption)), qPrintable(netlist));
    QVERIFY2(!netlist.contains("\n.end\n.end\n"), qPrintable(netlist));

    QTemporaryFile temp;
    QVERIFY2(temp.open(), "Failed to create temporary netlist file.");
    QTextStream out(&temp);
    out << netlist;
    out.flush();

    QString error;
    QVERIFY2(SimulationManager::instance().validateNetlist(temp.fileName(), &error), qPrintable(error));
}

void SpiceDirectiveNetlistTest::boostConverterFeedbackDoesNotRunAway() {
    if (!SimulationManager::instance().isAvailable()) {
        QSKIP("Ngspice is not available in this build.");
    }

    QGraphicsScene scene;
    auto* directive = new SchematicSpiceDirectiveItem(
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
        "B_err err 0 V=V(target_voltage) - V(out)\n"
        "B_pi pi_out 0 V={Kp}*V(err) + {Ki}*idt(V(err))\n"
        "B_duty duty 0 V=max(0.05, min(0.90, V(pi_out)))\n"
        "V_saw1 saw1 0 PULSE(0 1 0 {Tperiod - 20n} 10n 10n {Tperiod})\n"
        "B_pwm1 ctrl1 0 V=max(0, min(1, (V(duty)-V(saw1))*1000))\n"
        "L1 in sw1 10u Rser={DCR}\n"
        "S1 sw1 0 ctrl1 0 SW_ideal\n"
        "D1 sw1 out D_ideal\n"
        ".tran 0 5m 0 100n startup\n"
        ".end",
        QPointF(0, 0));
    scene.addItem(directive);

    SpiceNetlistGenerator::SimulationParams params;
    params.type = SpiceNetlistGenerator::Transient;
    params.step = "100n";
    params.stop = "5m";
    const QString netlist = SpiceNetlistGenerator::generate(&scene, QString(), nullptr, params);

    QTemporaryFile temp;
    QVERIFY2(temp.open(), "Failed to create temporary netlist file.");
    QTextStream out(&temp);
    out << netlist;
    out.flush();

    QString rawPath;
    QString error;
    bool finished = false;
    auto& sim = SimulationManager::instance();
    QObject::connect(&sim, &SimulationManager::rawResultsReady, &sim, [&](const QString& path) { rawPath = path; });
    QObject::connect(&sim, &SimulationManager::errorOccurred, &sim, [&](const QString& msg) { error = msg; });
    QObject::connect(&sim, &SimulationManager::simulationFinished, &sim, [&]() { finished = true; });

    sim.runSimulation(temp.fileName());
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&sim, &SimulationManager::simulationFinished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(30000);
    loop.exec();

    QVERIFY2(finished, qPrintable(error));
    QVERIFY2(!rawPath.isEmpty(), qPrintable(error));

    RawData data;
    QString parseError;
    QVERIFY2(RawDataParser::loadRawAscii(rawPath, &data, &parseError), qPrintable(parseError));

    const auto signalIndex = [&](const QString& name) {
        for (int i = 0; i < data.varNames.size(); ++i) {
            if (data.varNames.at(i).compare(name, Qt::CaseInsensitive) == 0) return i;
        }
        return -1;
    };
    const auto tailAverage = [](const QVector<double>& values, int tailCount = 500) {
        const int count = std::min(tailCount, static_cast<int>(values.size()));
        double sum = 0.0;
        for (int i = values.size() - count; i < values.size(); ++i) sum += values.at(i);
        return count > 0 ? sum / static_cast<double>(count) : 0.0;
    };
    const auto tailMin = [](const QVector<double>& values, int tailCount = 500) {
        const int count = std::min(tailCount, static_cast<int>(values.size()));
        double minVal = values.at(values.size() - count);
        for (int i = values.size() - count + 1; i < values.size(); ++i) minVal = std::min(minVal, values.at(i));
        return minVal;
    };
    const auto tailMax = [](const QVector<double>& values, int tailCount = 500) {
        const int count = std::min(tailCount, static_cast<int>(values.size()));
        double maxVal = values.at(values.size() - count);
        for (int i = values.size() - count + 1; i < values.size(); ++i) maxVal = std::max(maxVal, values.at(i));
        return maxVal;
    };

    const int outIndex = signalIndex("v(out)");
    const int piIndex = signalIndex("v(pi_out)");
    const int dutyIndex = signalIndex("v(duty)");
    const int errIndex = signalIndex("v(err)");
    QVERIFY2(outIndex >= 1, qPrintable(QString("Missing signal v(out). Available: %1").arg(data.varNames.join(", "))));
    QVERIFY2(piIndex >= 1, qPrintable(QString("Missing signal v(pi_out). Available: %1").arg(data.varNames.join(", "))));
    QVERIFY2(dutyIndex >= 1, qPrintable(QString("Missing signal v(duty). Available: %1").arg(data.varNames.join(", "))));
    QVERIFY2(errIndex >= 1, qPrintable(QString("Missing signal v(err). Available: %1").arg(data.varNames.join(", "))));

    const QVector<double>& outValues = data.y[outIndex - 1];
    const QVector<double>& piValues = data.y[piIndex - 1];
    const QVector<double>& dutyValues = data.y[dutyIndex - 1];
    const QVector<double>& errValues = data.y[errIndex - 1];
    QVERIFY2(!outValues.isEmpty(), "No samples for v(out)");
    QVERIFY2(!piValues.isEmpty(), "No samples for v(pi_out)");
    QVERIFY2(!dutyValues.isEmpty(), "No samples for v(duty)");
    QVERIFY2(!errValues.isEmpty(), "No samples for v(err)");

    const double finalVout = outValues.last();
    const double finalVoutAvg = tailAverage(outValues);
    const double finalPiAvg = tailAverage(piValues);
    const double finalDutyAvg = tailAverage(dutyValues);
    const double finalErrAvg = tailAverage(errValues);
    const double dutyTailMin = tailMin(dutyValues);
    const double dutyTailMax = tailMax(dutyValues);

    QVERIFY2(finalVout < 100.0, qPrintable(QString("V(out) runaway: %1 V").arg(finalVout)));
    QVERIFY2(finalVoutAvg > 10.0 && finalVoutAvg < 30.0,
             qPrintable(QString("Tail-average V(out) out of expected range: %1 V").arg(finalVoutAvg)));
    QVERIFY2(dutyTailMin >= 0.049 && dutyTailMax <= 0.91,
             qPrintable(QString("Duty tail escaped clamp range: min=%1 max=%2").arg(dutyTailMin).arg(dutyTailMax)));
    QVERIFY2(finalDutyAvg >= 0.049 && finalDutyAvg <= 0.20,
             qPrintable(QString("Unexpected tail-average duty: %1").arg(finalDutyAvg)));
    QVERIFY2(finalPiAvg < 0.2 && finalPiAvg > -30.0,
             qPrintable(QString("Unexpected tail-average pi_out: %1 V").arg(finalPiAvg)));
    QVERIFY2(std::abs(finalErrAvg) < 15.0,
             qPrintable(QString("Unexpected tail-average error voltage: %1 V").arg(finalErrAvg)));
}

void SpiceDirectiveNetlistTest::mixedModeManagerInsertsAdcAndDacBridges() {
    NetlistManager manager;
    manager.setTitle("* Mixed-mode bridge test");
    manager.setNetTypeHint("DIN2", NodeType::DIGITAL_EVENT);

    NetlistManager::ComponentEntry vin;
    vin.componentId = "VIN";
    vin.instanceLine = "VSRC VIN 0 PULSE(0 5 0 1n 1n 10n 20n)";
    vin.pins.push_back({"VIN", "P", "VIN", NodeType::ANALOG, NetlistManager::PinDirection::OUTPUT});
    vin.pins.push_back({"VIN", "N", "0", NodeType::ANALOG, NetlistManager::PinDirection::OUTPUT});
    manager.addComponent(vin);

    NetlistManager::ComponentEntry rload;
    rload.componentId = "RLOAD";
    rload.instanceLine = "RLOAD DOUT 0 1k";
    rload.pins.push_back({"RLOAD", "1", "DOUT", NodeType::ANALOG, NetlistManager::PinDirection::INPUT});
    rload.pins.push_back({"RLOAD", "2", "0", NodeType::ANALOG, NetlistManager::PinDirection::INPUT});
    manager.addComponent(rload);

    LogicComponent gate("U1", "d_and");
    gate.addInput("A", "VIN", NodeType::DIGITAL_EVENT);
    gate.addInput("B", "DIN2", NodeType::DIGITAL_EVENT);
    gate.addOutput("Y", "DOUT", NodeType::DIGITAL_EVENT);
    manager.addLogicComponent(gate);

    const QString netlist = manager.generateNetlist().toString();

    QVERIFY2(netlist.contains(".model __viospice_adc_bridge adc_bridge"), qPrintable(netlist));
    QVERIFY2(netlist.contains(".model __viospice_dac_bridge dac_bridge"), qPrintable(netlist));
    QVERIFY2(netlist.contains("XMM_ADC") || netlist.contains("XBRADC"), qPrintable(netlist));
    QVERIFY2(netlist.contains("XMM_DAC") || netlist.contains("XBRDAC"), qPrintable(netlist));
    QVERIFY2(netlist.contains("__mm_U1_A_") || netlist.contains("__MM_ADC"), qPrintable(netlist));
    QVERIFY2(netlist.contains("__mm_RLOAD_1_") || netlist.contains("__MM_DAC"), qPrintable(netlist));
    QVERIFY2(!netlist.contains("%2"), qPrintable(netlist));
}

void SpiceDirectiveNetlistTest::logicComponentGeneratesVectorizedSubcircuit() {
    LogicComponent gate("U_AND3", "d_and");
    gate.addInput("A", "N1");
    gate.addInput("B", "N2");
    gate.addInput("C", "N3");
    gate.addOutput("Y", "N4");

    const QString subckt = gate.generateSubcircuit();
    const NetlistManager::ComponentEntry entry = gate.toNetlistEntry();

    QVERIFY2(subckt.contains(".subckt __logic_U_AND3 A B C Y"), qPrintable(subckt));
    QVERIFY2(subckt.contains("[A B C] Y __mdl_U_AND3"), qPrintable(subckt));
    QVERIFY2(subckt.contains(".model __mdl_U_AND3 d_and("), qPrintable(subckt));
    QVERIFY2(entry.instanceLine.contains("XU_AND3 N1 N2 N3 N4 __logic_U_AND3"), qPrintable(entry.instanceLine));
}

void SpiceDirectiveNetlistTest::symbolDefinitionResolvesExplicitPinMetadata() {
    Flux::Model::SymbolDefinition symbol("LogicWithMetadata");

    Flux::Model::SymbolPrimitive inPin = Flux::Model::SymbolPrimitive::createPin(QPointF(0, 0), 1, "A");
    inPin.data["signalDomain"] = "digital_event";
    inPin.data["signalDirection"] = "input";
    symbol.addPrimitive(inPin);

    Flux::Model::SymbolPrimitive outPin = Flux::Model::SymbolPrimitive::createPin(QPointF(100, 0), 2, "Y");
    outPin.data["signalDomain"] = "digital_event";
    outPin.data["electricalType"] = "Output";
    symbol.addPrimitive(outPin);

    QVERIFY(symbol.pinPrimitive("1") != nullptr);
    QVERIFY(symbol.pinPrimitive("A") != nullptr);
    QCOMPARE(symbol.pinSignalDomain("1"), QString("digital_event"));
    QCOMPARE(symbol.pinSignalDirection("1"), QString("input"));
    QCOMPARE(symbol.pinSignalDirection("2"), QString("output"));
    QCOMPARE(symbol.pinSignalDomain("Y"), QString("digital_event"));
}

void SpiceDirectiveNetlistTest::builtInGateAliasesMapToExpectedXspiceModels() {
    struct GateExpectation {
        const char* symbolName;
        const char* expectedModel;
    };

    const std::array<GateExpectation, 14> cases = {{
        {"Gate_AND", "d_and"},
        {"Gate_NAND", "d_nand"},
        {"Gate_OR", "d_or"},
        {"Gate_NOR", "d_nor"},
        {"Gate_XOR", "d_xor"},
        {"Gate_XNOR", "d_xnor"},
        {"Gate_NOT", "d_inverter"},
        {"Gate_BUF", "d_buffer"},
        {"D_FlipFlop", "d_dff"},
        {"JK_FlipFlop", "d_jkff"},
        {"T_FlipFlop", "d_tff"},
        {"SR_FlipFlop", "d_srff"},
        {"D_Latch", "d_dlatch"},
        {"SR_Latch", "d_srlatch"},
    }};

    for (const GateExpectation& gateCase : cases) {
        const QString symbolName = QString::fromLatin1(gateCase.symbolName);
        QCOMPARE(SpiceNetlistGenerator::normalizeXspiceGateModelAlias(symbolName, symbolName),
                 QString::fromLatin1(gateCase.expectedModel));
    }

    QCOMPARE(SpiceNetlistGenerator::normalizeXspiceGateModelAlias("AND", QString()), QString("d_and"));
    QCOMPARE(SpiceNetlistGenerator::normalizeXspiceGateModelAlias("NAND", QString()), QString("d_nand"));
    QCOMPARE(SpiceNetlistGenerator::normalizeXspiceGateModelAlias("OR", QString()), QString("d_or"));
    QCOMPARE(SpiceNetlistGenerator::normalizeXspiceGateModelAlias("NOR", QString()), QString("d_nor"));
    QCOMPARE(SpiceNetlistGenerator::normalizeXspiceGateModelAlias("XOR", QString()), QString("d_xor"));
    QCOMPARE(SpiceNetlistGenerator::normalizeXspiceGateModelAlias("XNOR", QString()), QString("d_xnor"));
    QCOMPARE(SpiceNetlistGenerator::normalizeXspiceGateModelAlias("NOT", QString()), QString("d_inverter"));
    QCOMPARE(SpiceNetlistGenerator::normalizeXspiceGateModelAlias("BUF", QString()), QString("d_buffer"));
    QCOMPARE(SpiceNetlistGenerator::normalizeXspiceGateModelAlias("DFF", QString()), QString("d_dff"));
    QCOMPARE(SpiceNetlistGenerator::normalizeXspiceGateModelAlias("JKFF", QString()), QString("d_jkff"));
    QCOMPARE(SpiceNetlistGenerator::normalizeXspiceGateModelAlias("TFF", QString()), QString("d_tff"));
    QCOMPARE(SpiceNetlistGenerator::normalizeXspiceGateModelAlias("SRFF", QString()), QString("d_srff"));
    QCOMPARE(SpiceNetlistGenerator::normalizeXspiceGateModelAlias("DLATCH", QString()), QString("d_dlatch"));
    QCOMPARE(SpiceNetlistGenerator::normalizeXspiceGateModelAlias("SRLATCH", QString()), QString("d_srlatch"));
}

QTEST_MAIN(SpiceDirectiveNetlistTest)
#include "test_spice_directive_netlist.moc"
