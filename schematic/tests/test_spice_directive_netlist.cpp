#include <QtTest/QtTest>

#include "../analysis/spice_netlist_generator.h"
#include "../items/power_item.h"
#include "../items/schematic_spice_directive_item.h"
#include "../../core/simulation_manager.h"
#include "../../simulator/core/raw_data_parser.h"

#include <QEventLoop>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QTemporaryFile>
#include <QTimer>
#include <QTextStream>

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
    void warnsAboutLtspiceMeasForms();
    void loadsBoostConverterLtspiceDirectiveInNgspice();
    void boostConverterFeedbackDoesNotRunAway();
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
    QVERIFY2(netlist.contains("BMOD out5 0 V={idtmod(V(err), 0, 1, 0)}"), qPrintable(netlist));
    QVERIFY2(netlist.contains("Rewrote LTspice behavioral helper functions"), qPrintable(netlist));
    QVERIFY2(netlist.contains("idtmod(...) detected and passed through unchanged"), qPrintable(netlist));
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
    timer.start(20000);
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

QTEST_MAIN(SpiceDirectiveNetlistTest)
#include "test_spice_directive_netlist.moc"
