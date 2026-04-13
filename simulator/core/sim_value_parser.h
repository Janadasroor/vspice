#ifndef SIM_VALUE_PARSER_H
#define SIM_VALUE_PARSER_H

#include <string>

namespace SimValueParser {

// Parse SPICE-style numeric values with optional engineering suffixes and units.
// Examples: 1k, 4.7u, 10meg, 1e-3, 2.2mV, 3.3V, -5e-6A
// Returns true on success and writes parsed value to outValue.
bool parseSpiceNumber(const std::string& text, double& outValue);

} // namespace SimValueParser

// Qt convenience overload — only available when <QString> has been included.
// Must be defined AFTER including this header + <QString> in any translation unit
// that needs it. The inline definition avoids ODR violations.
//
// Usage in Qt-dependent files:
//   #include "sim_value_parser.h"
//   #include <QString>
//   // Then call SimValueParser::parseSpiceNumber(QString, double&) as normal.

#ifdef QSTRING_H
namespace SimValueParser {
inline bool parseSpiceNumber(const QString& text, double& outValue) {
    return parseSpiceNumber(text.toStdString(), outValue);
}
} // namespace SimValueParser
#endif

#endif // SIM_VALUE_PARSER_H
