#include <QApplication>
#include <QGraphicsScene>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <stdexcept>

#include "flux/schematic/factories/schematic_item_registry.h"
#include "schematic/items/net_label_item.h"
#include "schematic/items/power_item.h"
#include "schematic/items/resistor_item.h"
#include "schematic/items/switch_item.h"
#include "schematic/items/transformer_item.h"
#include "schematic/items/voltage_source_item.h"
#include "schematic/items/wire_item.h"
#include "simulator/bridge/sim_schematic_bridge.h"
#include "simulator/core/sim_engine.h"

namespace {

constexpr double kEpsilon = 1e-6;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void buildDividerScene(
    QGraphicsScene& scene,
    const QString& sourceValue,
    VoltageSourceItem::SourceType sourceType,
    const std::function<void(VoltageSourceItem&)>& sourceConfigurer = nullptr
) {
    auto* source = new VoltageSourceItem(QPointF(0.0, 0.0), sourceValue, sourceType);
    source->setReference("V1");
    if (sourceConfigurer) {
        sourceConfigurer(*source);
    }

    auto* rTop = new ResistorItem(QPointF(60.0, -45.0), "1k", ResistorItem::US);
    rTop->setReference("R1");

    auto* rBottom = new ResistorItem(QPointF(180.0, -45.0), "1k", ResistorItem::US);
    rBottom->setReference("R2");

    auto* gnd = new PowerItem(QPointF(0.0, 60.0), PowerItem::GND);
    gnd->setValue("GND");

    auto* outLabel = new NetLabelItem(QPointF(120.0, -45.0), "OUT");

    auto* returnA = new WireItem(QPointF(240.0, -45.0), QPointF(240.0, 45.0));
    auto* returnB = new WireItem(QPointF(240.0, 45.0), QPointF(0.0, 45.0));

    scene.addItem(source);
    scene.addItem(rTop);
    scene.addItem(rBottom);
    scene.addItem(gnd);
    scene.addItem(outLabel);
    scene.addItem(returnA);
    scene.addItem(returnB);
}

void testDividerOperatingPoint() {
    QGraphicsScene scene;
    buildDividerScene(scene, "10V", VoltageSourceItem::DC);
    SimNetlist netlist = SimSchematicBridge::buildNetlist(&scene, nullptr);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;
    netlist.setAnalysis(config);

    SimEngine engine;
    const SimResults results = engine.run(netlist);

    const auto it = results.nodeVoltages.find("OUT");
    require(it != results.nodeVoltages.end(), "missing OUT node in OP results");
    require(std::abs(it->second - 5.0) < 0.05,
            "unexpected OUT OP voltage: " + std::to_string(it->second));
}

void testDividerTransientWaveform() {
    QGraphicsScene scene;
    buildDividerScene(scene, "0V", VoltageSourceItem::AC, [](VoltageSourceItem& source) {
        source.setAcOffset(0.0);
        source.setAcAmplitude(10.0);
        source.setAcFrequency(1000.0);
        source.setAcPhase(0.0);
    });
    SimNetlist netlist = SimSchematicBridge::buildNetlist(&scene, nullptr);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::Transient;
    config.tStart = 0.0;
    config.tStop = 4e-3;
    config.tStep = 1e-5;
    netlist.setAnalysis(config);

    SimEngine engine;
    const SimResults results = engine.run(netlist);

    const auto wfIt = std::find_if(
        results.waveforms.begin(),
        results.waveforms.end(),
        [](const SimWaveform& wf) { return wf.name == "V(OUT)"; });
    require(wfIt != results.waveforms.end(), "missing V(OUT) transient waveform");
    require(!wfIt->xData.empty() && !wfIt->yData.empty(), "empty transient waveform");
    require(std::abs(wfIt->xData.back() - config.tStop) < 1e-4, "transient did not reach requested stop time");

    double yMin = wfIt->yData.front();
    double yMax = wfIt->yData.front();
    for (double v : wfIt->yData) {
        yMin = std::min(yMin, v);
        yMax = std::max(yMax, v);
    }

    require(yMin < -4.8 + kEpsilon, "transient minimum too high");
    require(yMax > 4.8 - kEpsilon, "transient maximum too low");
}

void testSwitchAndTransformerMapping() {
    QGraphicsScene scene;

    // 2-pin schematic switch closed state represented as low resistance.
    auto* vSwitch = new VoltageSourceItem(QPointF(0.0, 0.0), "5V", VoltageSourceItem::DC);
    vSwitch->setReference("VSW");
    auto* sw = new SwitchItem(QPointF(15.0, -45.0));
    sw->setReference("SW1");
    sw->setValue("0.001");
    auto* gnd = new PowerItem(QPointF(0.0, 60.0), PowerItem::GND);
    gnd->setValue("GND");
    auto* swReturnA = new WireItem(QPointF(30.0, -45.0), QPointF(30.0, 45.0));
    auto* swReturnB = new WireItem(QPointF(30.0, 45.0), QPointF(0.0, 45.0));

    // 4-pin transformer symbol mapped to quasi-static transmission-line element.
    auto* xfmr = new TransformerItem(QPointF(100.0, 0.0), "1:1");
    xfmr->setReference("T1");
    auto* vPri = new VoltageSourceItem(QPointF(90.0, 5.0), "2V", VoltageSourceItem::DC);
    vPri->setReference("VP");
    auto* vSec = new VoltageSourceItem(QPointF(110.0, 5.0), "1V", VoltageSourceItem::DC);
    vSec->setReference("VS");
    auto* gndPri = new PowerItem(QPointF(90.0, 65.0), PowerItem::GND);
    auto* gndSec = new PowerItem(QPointF(110.0, 65.0), PowerItem::GND);
    auto* xfmrRetPri = new WireItem(QPointF(90.0, 40.0), QPointF(90.0, 50.0));
    auto* xfmrRetSec = new WireItem(QPointF(110.0, 40.0), QPointF(110.0, 50.0));

    scene.addItem(vSwitch);
    scene.addItem(sw);
    scene.addItem(gnd);
    scene.addItem(swReturnA);
    scene.addItem(swReturnB);
    scene.addItem(xfmr);
    scene.addItem(vPri);
    scene.addItem(vSec);
    scene.addItem(gndPri);
    scene.addItem(gndSec);
    scene.addItem(xfmrRetPri);
    scene.addItem(xfmrRetSec);

    SimNetlist netlist = SimSchematicBridge::buildNetlist(&scene, nullptr);

    const auto& comps = netlist.components();
    const auto swIt = std::find_if(comps.begin(), comps.end(), [](const SimComponentInstance& c) {
        return c.name == "SW1";
    });
    require(swIt != comps.end(), "missing SW1 in mapped netlist");
    require(swIt->type == SimComponentType::Switch, "SW1 did not map to switch model");
    require(swIt->params.count("roff") > 0, "SW1 missing resistance parameter");
    require(std::abs(swIt->params.at("roff") - 0.001) < 1e-9, "SW1 closed-state resistance not preserved");

    const auto xfmrIt = std::find_if(comps.begin(), comps.end(), [](const SimComponentInstance& c) {
        return c.name == "T1";
    });
    require(xfmrIt != comps.end(), "missing T1 in mapped netlist");
    require(xfmrIt->type == SimComponentType::TransmissionLine, "T1 did not map to transmission-line surrogate");
    require(xfmrIt->nodes.size() == 4, "T1 does not have all four mapped pins");
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    SchematicItemRegistry::registerBuiltInItems();

    try {
        testDividerOperatingPoint();
        testDividerTransientWaveform();
        testSwitchAndTransformerMapping();
        std::cout << "[PASS] schematic-simulator integration checks passed." << std::endl;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "[FAIL] " << ex.what() << std::endl;
        return 1;
    }
}
