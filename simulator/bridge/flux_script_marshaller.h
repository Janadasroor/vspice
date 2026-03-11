#ifndef FLUX_SCRIPT_MARSHALLER_H
#define FLUX_SCRIPT_MARSHALLER_H

#include <string>
#include <map>
#include <vector>

#ifdef slots
#undef slots
#endif
#include <pybind11/pybind11.h>
#define slots Q_SLOTS

namespace py = pybind11;

/**
 * @brief Utilities for converting between C++ simulation data and Python objects.
 * Designed for high performance during tight simulation loops.
 */
class FluxScriptMarshaller {
public:
    /**
     * @brief Converts a C++ map of pin voltages to a Python dictionary.
     */
    static py::dict voltagesToDict(const std::map<std::string, double>& voltages);

    /**
     * @brief Converts a Python object (float or dict) to a C++ map of output values.
     */
    static std::map<std::string, double> pythonToOutputs(const py::object& result);
};

#endif // FLUX_SCRIPT_MARSHALLER_H
