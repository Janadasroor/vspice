#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <cmath>
#include "../core/sim_netlist.h"
#include "../core/sim_engine.h"

void testPNP() {
    std::cout << "Running PNP BJT test..." << std::endl;
    SimNetlist netlist;
    int nC = netlist.addNode("nodeC");
    int nB = netlist.addNode("nodeB");
    int nVE = netlist.addNode("VE");
    
    // V1 VE 0 10V
    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {nVE, 0};
    v1.params["voltage"] = 10.0;
    netlist.addComponent(v1);

    // Rb nodeB 0 1M (Base to GND)
    SimComponentInstance rb;
    rb.name = "RB";
    rb.type = SimComponentType::Resistor;
    rb.nodes = {nB, 0};
    rb.params["resistance"] = 1e6;
    netlist.addComponent(rb);

    // Rc nodeC 0 1k (Collector to GND)
    SimComponentInstance rc;
    rb.name = "RC"; // Wait, name should be unique
    rc.name = "RC";
    rc.type = SimComponentType::Resistor;
    rc.nodes = {nC, 0};
    rc.params["resistance"] = 1000.0;
    netlist.addComponent(rc);

    // Q1 nodeC nodeB VE PNP
    // Pin order: C, B, E
    SimComponentInstance q1;
    q1.name = "Q1";
    q1.type = SimComponentType::BJT_PNP;
    q1.nodes = {nC, nB, nVE};
    q1.params["Bf"] = 100.0;
    netlist.addComponent(q1);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    double vc = results.nodeVoltages["nodeC"];
    double vb = results.nodeVoltages["nodeB"];
    double ve = results.nodeVoltages["VE"];
    
    std::cout << "PNP OP nodeC: " << vc << "V, nodeB: " << vb << "V, VE: " << ve << "V" << std::endl;
    
    // Expected for PNP:
    // Vbe = Vb - Ve approx -0.7V => Veb approx 0.7V
    // Ib flows OUT of base. 
    // If it's working correctly:
    // Veb = 0.7V => Vb = Ve - 0.7 = 9.3V
    // Ib = (Ve - Vb) / Rb? No, Rb is from B to GND.
    // Ib = Vb / Rb = 9.3V / 1M = 9.3uA.
    // Ic = Beta * Ib = 0.93mA.
    // Vc = Ic * Rc = 0.93V.
    
    if (vb > 9.0 && vb < 9.5 && vc > 0.8 && vc < 1.0) {
        std::cout << "PNP test PASSED" << std::endl;
    } else {
        std::cout << "PNP test FAILED: Unexpected voltages." << std::endl;
        exit(1);
    }
}

int main() {
    testPNP();
    return 0;
}
