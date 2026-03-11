#ifndef SIM_VALUE_PARSER_H
#define SIM_VALUE_PARSER_H

#include <QString>

namespace SimValueParser {

// Parse SPICE-style numeric values with optional engineering suffixes and units.
// Examples: 1k, 4.7u, 10meg, 1e-3, 2.2mV, 3.3V, -5e-6A
// Returns true on success and writes parsed value to outValue.
bool parseSpiceNumber(const QString& text, double& outValue);

} // namespace SimValueParser

#endif // SIM_VALUE_PARSER_H
