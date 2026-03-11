#include "../core/sim_model_parser.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void testModelInheritance() {
    SimNetlist netlist;
    std::vector<SimParseDiagnostic> diags;
    const std::string lib =
        ".model BASE NMOS (KP=100u VTO=1.0)\n"
        ".model FAST BASE (KP=200u)\n";

    const bool ok = SimModelParser::parseLibrary(netlist, lib, SimModelParseOptions(), &diags);
    require(ok, "parseLibrary failed for inheritance case");

    const SimModel* base = netlist.findModel("BASE");
    const SimModel* fast = netlist.findModel("FAST");
    require(base != nullptr, "BASE model missing");
    require(fast != nullptr, "FAST model missing");
    require(fast->type == SimComponentType::MOSFET_NMOS, "FAST type did not inherit NMOS");
    require(fast->params.count("KP") != 0, "FAST KP missing");
    require(fast->params.count("VTO") != 0, "FAST VTO missing");
    require(fast->params.at("KP") > base->params.at("KP"), "FAST KP override did not apply");
}

void testLibSectionFilter() {
    SimNetlist netlist;
    std::vector<SimParseDiagnostic> diags;
    const std::string content =
        ".lib TT\n"
        ".model MTT NMOS (KP=100u)\n"
        ".endl\n"
        ".lib FF\n"
        ".model MFF NMOS (KP=200u)\n"
        ".endl\n";

    SimModelParseOptions options;
    options.activeLibSection = "FF";
    const bool ok = SimModelParser::parseLibrary(netlist, content, options, &diags);
    require(ok, "parseLibrary failed for .lib section filtering");
    require(netlist.findModel("MFF") != nullptr, "filtered section model MFF missing");
    require(netlist.findModel("MTT") == nullptr, "unexpected model from skipped section");
}

void testIncludeResolverAndDiagnostics() {
    SimNetlist netlist;
    std::vector<SimParseDiagnostic> diags;

    SimModelParseOptions options;
    options.sourceName = "root.lib";
    options.includeResolver = [](const std::string& path, std::string& outContent) -> bool {
        if (path == "child.lib") {
            outContent = ".model CHILD D (IS=1e-14 N=1.2)\n";
            return true;
        }
        return false;
    };

    const std::string content =
        ".include child.lib\n"
        ".model BROKEN UNKNOWN_TYPE (KP=1)\n";

    const bool ok = SimModelParser::parseLibrary(netlist, content, options, &diags);
    require(ok, "non-strict parseLibrary should not hard fail");
    require(netlist.findModel("CHILD") != nullptr, "included child model missing");

    bool sawBrokenDiag = false;
    for (const auto& d : diags) {
        if (d.line == 2 && d.severity == SimParseDiagnosticSeverity::Error &&
            d.message.find("unknown model type or base model") != std::string::npos) {
            sawBrokenDiag = true;
            break;
        }
    }
    require(sawBrokenDiag, "expected line-based diagnostic for broken .model");
}

void testSubcktNamedNodeMapping() {
    SimNetlist netlist;
    std::vector<SimParseDiagnostic> diags;

    const std::string content =
        ".subckt INV IN OUT VDD VSS\n"
        "R1 OUT IN 1k\n"
        ".ends\n";

    const bool ok = SimModelParser::parseLibrary(netlist, content, SimModelParseOptions(), &diags);
    require(ok, "subckt parse failed");

    const SimSubcircuit* inv = netlist.findSubcircuit("INV");
    require(inv != nullptr, "subcircuit INV missing");
    require(inv->components.size() == 1, "unexpected subcircuit component count");
    require(inv->components[0].nodes.size() == 2, "subcircuit node mapping size mismatch");
    require(inv->components[0].nodes[0] == 2, "OUT pin did not map to pin index 2");
    require(inv->components[0].nodes[1] == 1, "IN pin did not map to pin index 1");
}

} // namespace

int main() {
    try {
        testModelInheritance();
        testLibSectionFilter();
        testIncludeResolverAndDiagnostics();
        testSubcktNamedNodeMapping();
        std::cout << "[PASS] model parser compatibility checks passed." << std::endl;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "[FAIL] " << ex.what() << std::endl;
        return 1;
    }
}
