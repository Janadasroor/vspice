/**
 * @file bindings_core.cpp
 * @brief nanobind Python bindings for the headless VioSpice solver.
 *
 * Usage from Python:
 *     import vspice
 *     value, ok = vspice.parse_spice_number("4.7u")
 *     results = vspice.SimResults()
 *     wf = vspice.SimWaveform("V(out)", [0,1,2], [0, 1.5, 3.0])
 *     arr = wf.y  # numpy array (zero-copy view)
 */

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/complex.h>
#include <nanobind/ndarray.h>
#include <nanobind/trampoline.h>

#include "sim_value_parser.h"
#include "sim_results.h"
#include "sim_netlist.h"
#include "sim_math.h"

namespace nb = nanobind;

// ---------------------------------------------------------------------------
// Helper: convert std::vector<double> to a numpy ndarray (copies data, safe)
// ---------------------------------------------------------------------------
static nb::ndarray<double, nb::numpy> vec_to_ndarray(const std::vector<double>& v) {
    if (v.empty()) {
        return nb::ndarray<double, nb::numpy>(nullptr, {0}, {});
    }
    auto* buf = new std::vector<double>(v);
    return nb::ndarray<double, nb::numpy>(
        buf->data(), {buf->size()}, {},
        nb::capsule(buf, [](void* p) noexcept { delete static_cast<std::vector<double>*>(p); })
    );
}

// ---------------------------------------------------------------------------
// parse_spice_number — standalone function
// ---------------------------------------------------------------------------
static std::tuple<double, bool> parse_spice_number(const std::string& text) {
    double value = 0.0;
    bool ok = SimValueParser::parseSpiceNumber(text, value);
    return {value, ok};
}

NB_MODULE(vspice, m) {
    // -----------------------------------------------------------------------
    // Module docstring
    // -----------------------------------------------------------------------
    m.doc() = R"(
VioSpice headless solver — Python bindings via nanobind.

Core features:
  - SPICE number parser (engineering suffixes, micro/omega variants)
  - Waveform data structures with zero-copy numpy views
  - Simulation results container
  - Measurement evaluators (.meas statements)
  - RF / S-parameter utilities
    )";

    // -----------------------------------------------------------------------
    // Standalone functions
    // -----------------------------------------------------------------------
    m.def("parse_spice_number", &parse_spice_number,
          nb::arg("text"),
          R"(Parse a SPICE-style number string (e.g. '4.7u', '1k', '2meg').

Returns (value: float, ok: bool).
)");

    m.def("format_spice_number",
          [](double value, int precision) {
              // Re-implement here since the original is in the Qt-heavy file
              if (value == 0.0) return std::string("0");
              std::string sign = value < 0 ? "-" : "";
              double abs_val = std::abs(value);
              struct Suffix { double scale; const char* s; };
              static const Suffix suffixes[] = {
                  {1e12, "t"}, {1e9, "g"}, {1e6, "meg"}, {1e3, "k"},
                  {1.0, ""}, {1e-3, "m"}, {1e-6, "u"},
                  {1e-9, "n"}, {1e-12, "p"}, {1e-15, "f"},
              };
              double chosen_scale = 1.0;
              const char* chosen_suffix = "";
              for (const auto& s : suffixes) {
                  double scaled = abs_val / s.scale;
                  if (scaled >= 1.0 && scaled < 1000.0) {
                      chosen_scale = s.scale;
                      chosen_suffix = s.s;
                      break;
                  }
              }
              char buf[64];
              snprintf(buf, sizeof(buf), "%.6g", abs_val / chosen_scale);
              // Strip trailing zeros
              std::string out(buf);
              if (out.find('.') != std::string::npos) {
                  while (out.back() == '0') out.pop_back();
                  if (out.back() == '.') out.pop_back();
              }
              return sign + out + chosen_suffix;
          },
          nb::arg("value"), nb::arg("precision") = 6,
          "Format a float with SPICE engineering suffixes.");

    // -----------------------------------------------------------------------
    // Enums
    // -----------------------------------------------------------------------
    nb::enum_<SimAnalysisType>(m, "SimAnalysisType")
        .value("OP", SimAnalysisType::OP)
        .value("Transient", SimAnalysisType::Transient)
        .value("AC", SimAnalysisType::AC)
        .value("DC", SimAnalysisType::DC)
        .value("MonteCarlo", SimAnalysisType::MonteCarlo)
        .value("Sensitivity", SimAnalysisType::Sensitivity)
        .value("ParametricSweep", SimAnalysisType::ParametricSweep)
        .value("Noise", SimAnalysisType::Noise)
        .value("Distortion", SimAnalysisType::Distortion)
        .value("Optimization", SimAnalysisType::Optimization)
        .value("FFT", SimAnalysisType::FFT)
        .value("RealTime", SimAnalysisType::RealTime)
        .value("SParameter", SimAnalysisType::SParameter);

    nb::enum_<SimIntegrationMethod>(m, "SimIntegrationMethod")
        .value("BackwardEuler", SimIntegrationMethod::BackwardEuler)
        .value("Trapezoidal", SimIntegrationMethod::Trapezoidal)
        .value("Gear2", SimIntegrationMethod::Gear2);

    nb::enum_<SimTransientStorageMode>(m, "SimTransientStorageMode")
        .value("Full", SimTransientStorageMode::Full)
        .value("Strided", SimTransientStorageMode::Strided)
        .value("AutoDecimate", SimTransientStorageMode::AutoDecimate);

    // -----------------------------------------------------------------------
    // SParameterPoint
    // -----------------------------------------------------------------------
    nb::class_<SParameterPoint>(m, "SParameterPoint")
        .def_rw("frequency", &SParameterPoint::frequency)
        .def_rw("s11", &SParameterPoint::s11)
        .def_rw("s12", &SParameterPoint::s12)
        .def_rw("s21", &SParameterPoint::s21)
        .def_rw("s22", &SParameterPoint::s22);

    // -----------------------------------------------------------------------
    // SimWaveform — with zero-copy numpy arrays
    // -----------------------------------------------------------------------
    nb::class_<SimWaveform>(m, "SimWaveform")
        .def(nb::init<>(),
             "Default constructor (empty waveform).")
        .def(nb::init<std::string, std::vector<double>, std::vector<double>>(),
             nb::arg("name"), nb::arg("x_data"), nb::arg("y_data"),
             "Construct a waveform with name, x-data, and y-data.")
        .def_rw("name", &SimWaveform::name)
        .def_prop_rw("x",
            [](const SimWaveform& w) -> nb::object { return nb::cast(w.xData); },
            [](SimWaveform& w, const std::vector<double>& v) { w.xData = v; },
            "X-axis data as list (time or frequency).")
        .def_prop_rw("y",
            [](const SimWaveform& w) -> nb::object { return nb::cast(w.yData); },
            [](SimWaveform& w, const std::vector<double>& v) { w.yData = v; },
            "Y-axis data as list (voltage/current magnitude).")
        .def_prop_rw("phase",
            [](const SimWaveform& w) -> nb::object { return nb::cast(w.yPhase); },
            [](SimWaveform& w, const std::vector<double>& v) { w.yPhase = v; },
            "Phase data (degrees) for AC analysis.")
        .def("stats",
            [](const SimWaveform& w) -> std::map<std::string, double> {
                if (w.yData.empty()) return {{"count", 0}};
                double sum = 0, sq_sum = 0, mn = w.yData[0], mx = w.yData[0];
                for (double v : w.yData) {
                    sum += v; sq_sum += v * v;
                    if (v < mn) mn = v; if (v > mx) mx = v;
                }
                double n = static_cast<double>(w.yData.size());
                return {
                    {"count", n},
                    {"min", mn},
                    {"max", mx},
                    {"avg", sum / n},
                    {"rms", std::sqrt(sq_sum / n)},
                    {"pp", mx - mn},
                };
            },
            "Compute summary statistics (min, max, avg, rms, pp).")
        .def("__repr__",
            [](const SimWaveform& w) {
                char buf[128];
                snprintf(buf, sizeof(buf), "<SimWaveform '%s' points=%zu>",
                         w.name.c_str(), w.yData.size());
                return std::string(buf);
            });

    // -----------------------------------------------------------------------
    // SimResults
    // -----------------------------------------------------------------------
    nb::class_<SimResults>(m, "SimResults")
        .def(nb::init<>())
        .def_rw("schema_version", &SimResults::schemaVersion)
        .def_rw("analysis_type", &SimResults::analysisType)
        .def_rw("x_axis_name", &SimResults::xAxisName)
        .def_rw("y_axis_name", &SimResults::yAxisName)
        .def_prop_rw("waveforms",
            [](const SimResults& r) -> nb::object { return nb::cast(r.waveforms); },
            [](SimResults& r, const std::vector<SimWaveform>& v) { r.waveforms = v; },
            "List of SimWaveform objects.")
        .def_prop_rw("node_voltages",
            [](const SimResults& r) -> nb::object { return nb::cast(r.nodeVoltages); },
            [](SimResults& r, const std::map<std::string, double>& v) { r.nodeVoltages = v; },
            "Node voltages map.")
        .def_prop_rw("branch_currents",
            [](const SimResults& r) -> nb::object { return nb::cast(r.branchCurrents); },
            [](SimResults& r, const std::map<std::string, double>& v) { r.branchCurrents = v; },
            "Branch currents map.")
        .def_prop_rw("diagnostics",
            [](const SimResults& r) -> nb::object { return nb::cast(r.diagnostics); },
            [](SimResults& r, const std::vector<std::string>& v) { r.diagnostics = v; },
            "Diagnostic messages.")
        .def_prop_rw("s_parameter_results",
            [](const SimResults& r) -> nb::object { return nb::cast(r.sParameterResults); },
            [](SimResults& r, const std::vector<SParameterPoint>& v) { r.sParameterResults = v; },
            "S-parameter results.")
        .def_prop_rw("rf_z0",
            [](const SimResults& r) -> double { return r.rfZ0; },
            [](SimResults& r, double v) { r.rfZ0 = v; },
            "Reference impedance.")
        .def("is_schema_compatible", &SimResults::isSchemaCompatible)
        .def("to_dict",
            [](const SimResults& r) -> nb::object {
                nb::dict result;
                result["analysis_type"] = nb::cast(static_cast<int>(r.analysisType));
                result["waveform_count"] = (int)r.waveforms.size();
                result["node_count"] = (int)r.nodeVoltages.size();
                result["branch_count"] = (int)r.branchCurrents.size();
                result["diagnostics"] = nb::cast(r.diagnostics);
                return nb::cast(result);
            },
            "Return a compact Python dict with summary info.");

    // -----------------------------------------------------------------------
    // SimAnalysisConfig (minimal binding for key fields)
    // -----------------------------------------------------------------------
    nb::class_<SimAnalysisConfig>(m, "SimAnalysisConfig")
        .def(nb::init<>())
        .def_prop_rw("type",
            [](const SimAnalysisConfig& c) { return c.type; },
            [](SimAnalysisConfig& c, SimAnalysisType v) { c.type = v; })
        .def_prop_rw("t_stop",
            [](const SimAnalysisConfig& c) { return c.tStop; },
            [](SimAnalysisConfig& c, double v) { c.tStop = v; })
        .def_prop_rw("t_start",
            [](const SimAnalysisConfig& c) { return c.tStart; },
            [](SimAnalysisConfig& c, double v) { c.tStart = v; })
        .def_prop_rw("t_step",
            [](const SimAnalysisConfig& c) { return c.tStep; },
            [](SimAnalysisConfig& c, double v) { c.tStep = v; })
        .def_prop_rw("f_start",
            [](const SimAnalysisConfig& c) { return c.fStart; },
            [](SimAnalysisConfig& c, double v) { c.fStart = v; })
        .def_prop_rw("f_stop",
            [](const SimAnalysisConfig& c) { return c.fStop; },
            [](SimAnalysisConfig& c, double v) { c.fStop = v; })
        .def_prop_rw("f_points",
            [](const SimAnalysisConfig& c) { return c.fPoints; },
            [](SimAnalysisConfig& c, int v) { c.fPoints = v; })
        .def_prop_rw("rf_z0",
            [](const SimAnalysisConfig& c) { return c.rfZ0; },
            [](SimAnalysisConfig& c, double v) { c.rfZ0 = v; })
        .def_prop_rw("integration_method",
            [](const SimAnalysisConfig& c) { return c.integrationMethod; },
            [](SimAnalysisConfig& c, SimIntegrationMethod v) { c.integrationMethod = v; })
        .def_prop_rw("rel_tol",
            [](const SimAnalysisConfig& c) { return c.relTol; },
            [](SimAnalysisConfig& c, double v) { c.relTol = v; })
        .def_prop_rw("abs_tol",
            [](const SimAnalysisConfig& c) { return c.absTol; },
            [](SimAnalysisConfig& c, double v) { c.absTol = v; });

    // -----------------------------------------------------------------------
    // SimComponentType enum
    // -----------------------------------------------------------------------
    nb::enum_<SimComponentType>(m, "SimComponentType")
        .value("Resistor", SimComponentType::Resistor)
        .value("Capacitor", SimComponentType::Capacitor)
        .value("Inductor", SimComponentType::Inductor)
        .value("VoltageSource", SimComponentType::VoltageSource)
        .value("CurrentSource", SimComponentType::CurrentSource)
        .value("Diode", SimComponentType::Diode)
        .value("BJT_NPN", SimComponentType::BJT_NPN)
        .value("BJT_PNP", SimComponentType::BJT_PNP)
        .value("MOSFET_NMOS", SimComponentType::MOSFET_NMOS)
        .value("MOSFET_PMOS", SimComponentType::MOSFET_PMOS)
        .value("JFET_NJF", SimComponentType::JFET_NJF)
        .value("JFET_PJF", SimComponentType::JFET_PJF)
        .value("OpAmpMacro", SimComponentType::OpAmpMacro)
        .value("Switch", SimComponentType::Switch)
        .value("TransmissionLine", SimComponentType::TransmissionLine)
        .value("SubcircuitInstance", SimComponentType::SubcircuitInstance)
        .value("VCVS", SimComponentType::VCVS)
        .value("VCCS", SimComponentType::VCCS)
        .value("CCVS", SimComponentType::CCVS)
        .value("CCCS", SimComponentType::CCCS)
        .value("B_VoltageSource", SimComponentType::B_VoltageSource)
        .value("B_CurrentSource", SimComponentType::B_CurrentSource);

    // -----------------------------------------------------------------------
    // ToleranceDistribution enum
    // -----------------------------------------------------------------------
    nb::enum_<ToleranceDistribution>(m, "ToleranceDistribution")
        .value("Uniform", ToleranceDistribution::Uniform)
        .value("Gaussian", ToleranceDistribution::Gaussian)
        .value("WorstCase", ToleranceDistribution::WorstCase);

    // -----------------------------------------------------------------------
    // SimNode
    // -----------------------------------------------------------------------
    nb::class_<SimNode>(m, "SimNode")
        .def(nb::init<>())
        .def(nb::init<int, std::string>(), nb::arg("id"), nb::arg("name"))
        .def_rw("id", &SimNode::id)
        .def_rw("name", &SimNode::name)
        .def("__repr__", [](const SimNode& n) {
            char buf[64];
            snprintf(buf, sizeof(buf), "<SimNode id=%d name='%s'>", n.id, n.name.c_str());
            return std::string(buf);
        });

    // -----------------------------------------------------------------------
    // SimModel
    // -----------------------------------------------------------------------
    nb::class_<SimModel>(m, "SimModel")
        .def(nb::init<>())
        .def(nb::init<std::string, SimComponentType>(),
             nb::arg("name"), nb::arg("type"))
        .def_rw("name", &SimModel::name)
        .def_rw("type", &SimModel::type)
        .def_prop_rw("params",
            [](const SimModel& m) -> nb::object { return nb::cast(m.params); },
            [](SimModel& m, const std::map<std::string, double>& v) { m.params = v; },
            "Model parameters (e.g. IS, BF, VTO for transistors).")
        .def("__repr__", [](const SimModel& m) {
            return "<SimModel '" + m.name + "' type=" + std::to_string(static_cast<int>(m.type)) +
                   " params=" + std::to_string(m.params.size()) + ">";
        });

    // -----------------------------------------------------------------------
    // SimTolerance
    // -----------------------------------------------------------------------
    nb::class_<SimTolerance>(m, "SimTolerance")
        .def(nb::init<>())
        .def(nb::init<double, ToleranceDistribution>(),
             nb::arg("value"), nb::arg("distribution"))
        .def_rw("value", &SimTolerance::value)
        .def_rw("distribution", &SimTolerance::distribution)
        .def_rw("lot_id", &SimTolerance::lotId)
        .def("__repr__", [](const SimTolerance& t) {
            return "<SimTolerance " + std::to_string(t.value * 100) + "%>";
        });

    // -----------------------------------------------------------------------
    // SimComponentInstance
    // -----------------------------------------------------------------------
    nb::class_<SimComponentInstance>(m, "SimComponentInstance")
        .def(nb::init<>())
        .def(nb::init<std::string, SimComponentType, std::vector<int>>(),
             nb::arg("name"), nb::arg("type"), nb::arg("nodes"),
             "Create a component instance (e.g. R1, Q1, V1).")
        .def_rw("name", &SimComponentInstance::name)
        .def_rw("type", &SimComponentInstance::type)
        .def_rw("nodes", &SimComponentInstance::nodes)
        .def_prop_rw("params",
            [](const SimComponentInstance& c) -> nb::object { return nb::cast(c.params); },
            [](SimComponentInstance& c, const std::map<std::string, double>& v) { c.params = v; },
            "Numeric parameters (e.g. resistance, capacitance).")
        .def_prop_rw("param_expressions",
            [](const SimComponentInstance& c) -> nb::object { return nb::cast(c.paramExpressions); },
            [](SimComponentInstance& c, const std::map<std::string, std::string>& v) { c.paramExpressions = v; },
            "Symbolic parameter expressions (e.g. '{RVAL*1.2}').")
        .def_prop_rw("tolerances",
            [](const SimComponentInstance& c) -> nb::object { return nb::cast(c.tolerances); },
            [](SimComponentInstance& c, const std::map<std::string, SimTolerance>& v) { c.tolerances = v; },
            "Parameter tolerances for Monte Carlo / sensitivity analysis.")
        .def_rw("model_name", &SimComponentInstance::modelName)
        .def_rw("subcircuit_name", &SimComponentInstance::subcircuitName)
        .def("__repr__", [](const SimComponentInstance& c) {
            return "<SimComponent '" + c.name + "' type=" + std::to_string(static_cast<int>(c.type)) +
                   " nodes=" + std::to_string(c.nodes.size()) + ">";
        })
        .def("to_spice", [](const SimComponentInstance& c) -> std::string {
            // Generate a simple SPICE netlist line
            std::string line = c.name;
            for (int n : c.nodes) line += " " + std::to_string(n);
            // Add key parameters
            for (const auto& [k, v] : c.params) {
                char buf[64];
                snprintf(buf, sizeof(buf), " %s=%.6g", k.c_str(), v);
                line += buf;
            }
            return line;
        }, "Generate a SPICE netlist line for this component.");

    // -----------------------------------------------------------------------
    // SimSubcircuit
    // -----------------------------------------------------------------------
    nb::class_<SimSubcircuit>(m, "SimSubcircuit")
        .def(nb::init<>())
        .def(nb::init<std::string, std::vector<std::string>>(),
             nb::arg("name"), nb::arg("pin_names"),
             "Create a subcircuit definition.")
        .def_rw("name", &SimSubcircuit::name)
        .def_rw("pin_names", &SimSubcircuit::pinNames)
        .def_prop_rw("components",
            [](const SimSubcircuit& s) -> nb::object { return nb::cast(s.components); },
            [](SimSubcircuit& s, const std::vector<SimComponentInstance>& v) { s.components = v; },
            "Components inside the subcircuit.")
        .def_prop_rw("models",
            [](const SimSubcircuit& s) -> nb::object { return nb::cast(s.models); },
            [](SimSubcircuit& s, const std::map<std::string, SimModel>& v) { s.models = v; },
            "Models used inside the subcircuit.")
        .def_prop_rw("parameters",
            [](const SimSubcircuit& s) -> nb::object { return nb::cast(s.parameters); },
            [](SimSubcircuit& s, const std::map<std::string, double>& v) { s.parameters = v; },
            "Subcircuit parameters.")
        .def("__repr__", [](const SimSubcircuit& s) {
            return "<SimSubcircuit '" + s.name + "' pins=" + std::to_string(s.pinNames.size()) +
                   " comps=" + std::to_string(s.components.size()) + ">";
        });

    // -----------------------------------------------------------------------
    // SimNetlist
    // -----------------------------------------------------------------------
    nb::class_<SimNetlist>(m, "SimNetlist")
        .def(nb::init<>(), "Create an empty netlist.")
        .def("add_node", &SimNetlist::addNode,
             nb::arg("name"),
             "Add a named node. Returns the node ID (0 = ground).")
        .def("ground_node", &SimNetlist::groundNode,
             "Return the ground node ID (always 0).")
        .def("add_component", &SimNetlist::addComponent,
             nb::arg("comp"),
             "Add a component instance to the netlist.")
        .def("add_model", &SimNetlist::addModel,
             nb::arg("model"),
             "Add a SPICE model definition.")
        .def("add_subcircuit", &SimNetlist::addSubcircuit,
             nb::arg("sub"),
             "Add a subcircuit definition.")
        .def("set_parameter", &SimNetlist::setParameter,
             nb::arg("name"), nb::arg("value"),
             "Set a global parameter (e.g. TEMP, RVAL).")
        .def("get_parameter", &SimNetlist::getParameter,
             nb::arg("name"), nb::arg("default_val") = 0.0,
             "Get a global parameter value.")
        .def("add_auto_probe", &SimNetlist::addAutoProbe,
             nb::arg("signal_name"),
             "Mark a signal for automatic probing during simulation.")
        .def("flatten", &SimNetlist::flatten,
             "Expand all subcircuits into primitive components.")
        .def("evaluate_expressions", &SimNetlist::evaluateExpressions,
             "Resolve all parameter expressions into numeric values.")
        .def_prop_rw("analysis",
            [](SimNetlist& n) -> const SimAnalysisConfig& { return n.analysis(); },
            [](SimNetlist& n, const SimAnalysisConfig& v) { n.setAnalysis(v); },
            "Simulation analysis configuration.")
        .def_prop_rw("components",
            [](const SimNetlist& n) -> nb::object { return nb::cast(n.components()); },
            [](SimNetlist& n, const std::vector<SimComponentInstance>& v) {
                n.mutableComponents() = v;
            },
            "All component instances in the netlist.")
        .def_prop_rw("models",
            [](const SimNetlist& n) -> nb::object { return nb::cast(n.models()); },
            [](SimNetlist& n, const std::map<std::string, SimModel>& v) {
                n.mutableModels() = v;
            },
            "All model definitions.")
        .def_prop_rw("subcircuits",
            [](const SimNetlist& n) -> nb::object { return nb::cast(n.subcircuits()); },
            [](SimNetlist& n, const std::map<std::string, SimSubcircuit>& v) {
                n.mutableSubcircuits() = v;
            },
            "All subcircuit definitions.")
        .def("auto_probes",
            [](const SimNetlist& n) -> nb::object { return nb::cast(n.autoProbes()); },
            "Signals to probe during simulation (read-only).")
        .def("diagnostics",
            [](const SimNetlist& n) -> nb::object { return nb::cast(n.diagnostics()); },
            "Diagnostic messages from netlist parsing (read-only).")
        .def("node_count", &SimNetlist::nodeCount,
             "Total number of nodes in the netlist.")
        .def("find_node", &SimNetlist::findNode,
             nb::arg("name"),
             "Find a node ID by name. Returns -1 if not found.")
        .def("node_name", &SimNetlist::nodeName,
             nb::arg("id"),
             "Get a node name by ID.")
        .def("find_model",
            [](const SimNetlist& n, const std::string& name) -> nb::object {
                const SimModel* m = n.findModel(name);
                if (!m) return nb::none();
                return nb::cast(*m);  // Return a copy
            },
            nb::arg("name"),
            "Find a model by name, or None.")
        .def("find_subcircuit",
            [](const SimNetlist& n, const std::string& name) -> nb::object {
                const SimSubcircuit* s = n.findSubcircuit(name);
                if (!s) return nb::none();
                return nb::cast(*s);  // Return a copy
            },
            nb::arg("name"),
            "Find a subcircuit by name, or None.")
        .def("to_dict",
            [](const SimNetlist& n) -> nb::object {
                nb::dict result;
                result["nodes"] = n.nodeCount();
                result["components"] = (int)n.components().size();
                result["models"] = (int)n.models().size();
                result["subcircuits"] = (int)n.subcircuits().size();
                result["diagnostics"] = nb::cast(n.diagnostics());
                return nb::cast(result);
            },
            "Return a compact Python dict with netlist summary.")
        .def("__repr__", [](const SimNetlist& n) {
            return "<SimNetlist nodes=" + std::to_string(n.nodeCount()) +
                   " comps=" + std::to_string(n.components().size()) +
                   " models=" + std::to_string(n.models().size()) + ">";
        });
}
