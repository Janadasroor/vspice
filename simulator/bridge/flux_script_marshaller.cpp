#include "flux_script_marshaller.h"

py::dict FluxScriptMarshaller::voltagesToDict(const std::map<std::string, double>& voltages) {
    py::dict d;
    for (const auto& [name, val] : voltages) {
        d[py::cast(name)] = val;
    }
    return d;
}

std::map<std::string, double> FluxScriptMarshaller::pythonToOutputs(const py::object& result) {
    std::map<std::string, double> outputs;

    if (py::isinstance<py::float_>(result) || py::isinstance<py::int_>(result)) {
        // Single value output (default to "out")
        outputs["out"] = result.cast<double>();
    } else if (py::isinstance<py::dict>(result)) {
        // Multi-value output
        py::dict d = result.cast<py::dict>();
        for (auto item : d) {
            outputs[item.first.cast<std::string>()] = item.second.cast<double>();
        }
    }

    return outputs;
}
