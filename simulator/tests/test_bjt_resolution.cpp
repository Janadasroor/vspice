#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <cmath>
#include "../core/sim_netlist.h"
#include "../core/sim_engine.h"
#include "../core/sim_model_parser.h"

void testBJTResolution() {
    std::cout << "Running BJT resolution test..." << std::endl;
    SimNetlist netlist;
    
    // Define models
    SimModel npnModel;
    npnModel.name = "MYNPN";
    npnModel.type = SimComponentType::BJT_NPN;
    npnModel.params["Bf"] = 150.0;
    netlist.addModel(npnModel);

    SimModel pnpModel;
    pnpModel.name = "MYPNP";
    pnpModel.type = SimComponentType::BJT_PNP;
    pnpModel.params["Bf"] = 80.0;
    netlist.addModel(pnpModel);

    // Add components using these models
    int n1 = netlist.addNode("1");
    int n2 = netlist.addNode("2");
    int n3 = netlist.addNode("3");
    int n4 = netlist.addNode("4");
    int n5 = netlist.addNode("5");
    int n6 = netlist.addNode("6");

    // Q1 1 2 3 MYNPN
    SimComponentInstance q1;
    q1.name = "Q1";
    q1.type = SimComponentType::BJT_NPN; // Default set by parser
    q1.nodes = {n1, n2, n3};
    q1.modelName = "MYNPN";
    netlist.addComponent(q1);

    // Q2 4 5 6 MYPNP
    SimComponentInstance q2;
    q2.name = "Q2";
    q2.type = SimComponentType::BJT_NPN; // WRONG default set by parser
    q2.nodes = {n4, n5, n6};
    q2.modelName = "MYPNP";
    netlist.addComponent(q2);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;
    netlist.setAnalysis(config);

    SimEngine engine;
    // The run() method should internally flatten and resolve models.
    // We can't easily check the internal flatNetlist, so we check if simulation works.
    
    // Let's set up a real circuit to verify PNP behavior.
    // V1 6 0 10V
    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n6, 0};
    v1.params["voltage"] = 10.0;
    netlist.addComponent(v1);

    // RB 5 0 1M
    SimComponentInstance rb;
    rb.name = "RB";
    rb.type = SimComponentType::Resistor;
    rb.nodes = {n5, 0};
    rb.params["resistance"] = 1e6;
    netlist.addComponent(rb);

    // RC 4 0 1k
    SimComponentInstance rc;
    rc.name = "RC";
    rc.type = SimComponentType::Resistor;
    rc.nodes = {n4, 0};
    rc.params["resistance"] = 1000.0;
    netlist.addComponent(rc);

    SimResults results = engine.run(netlist);

    double vc = results.nodeVoltages["4"];
    double vb = results.nodeVoltages["5"];
    double ve = results.nodeVoltages["6"];
    
    std::cout << "Resolved PNP OP nodeC: " << vc << "V, nodeB: " << vb << "V, VE: " << ve << "V" << std::endl;
    
    // If Q2 was correctly resolved as PNP:
    // Vb approx 9.3V, Vc approx 0.8-1.0V (with Bf=80)
    if (vb > 9.0 && vc > 0.5) {
        std::cout << "BJT resolution test PASSED" << std::endl;
    } else {
        std::cout << "BJT resolution test FAILED" << std::endl;
        exit(1);
    }
}

int main() {
    testBJTResolution();
    return 0;
}
