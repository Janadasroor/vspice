#include "sim_value_parser.h"

#include <QLocale>
#include <QRegularExpression>

namespace {
bool unitAllowed(const QString& u) {
    return u.isEmpty() || u == "v" || u == "a" || u == "ohm" || u == "hz" || u == "s" ||
           u == "f" || u == "h";
}

bool splitSuffix(const QString& suffixRaw, double& factor, QString& unit) {
    QString suffix = suffixRaw.trimmed().toLower();
    suffix.replace(QChar(0x00B5), "u"); // micro sign
    suffix.replace(QChar(0x03BC), "u"); // greek mu
    suffix.replace(QChar(0x03A9), "ohm"); // greek omega
    suffix.replace(QChar(0x2126), "ohm"); // ohm sign

    factor = 1.0;
    unit = suffix;

    if (suffix.isEmpty()) {
        return true;
    }

    // SPICE standard prefixes. Order matters: 'meg' before 'm'.
    if (suffix.startsWith("meg")) {
        factor = 1e6;
        return true;
    }
    if (suffix.startsWith("mil")) {
        factor = 25.4e-6; // 0.001 inch
        return true;
    }

    static const struct Prefix {
        char key;
        double factor;
    } prefixes[] = {
        {'t', 1e12}, {'g', 1e9}, {'k', 1e3}, {'m', 1e-3},
        {'u', 1e-6}, {'n', 1e-9}, {'p', 1e-12}, {'f', 1e-15},
    };

    for (const auto& p : prefixes) {
        if (suffix[0] == p.key) {
            factor = p.factor;
            return true;
        }
    }

    // No prefix found, standard unit or just numeric trailing chars are ignored in SPICE
    return true;
}
} // namespace

namespace SimValueParser {

bool parseSpiceNumber(const QString& text, double& outValue) {
    QString normalized = text;
    normalized.replace(QChar(0x00B5), "u");   // micro sign
    normalized.replace(QChar(0x03BC), "u");   // greek mu
    normalized.replace(QChar(0x03A9), "ohm"); // greek omega
    normalized.replace(QChar(0x2126), "ohm"); // ohm sign

    // Locale-safe, strict parser:
    // number = sign? ((digits(.digits?)|.digits) (e sign? digits)?)
    // optional suffix/unit token without separators.
    static const QRegularExpression kPattern(
        "^\\s*([+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][+-]?\\d+)?)\\s*([A-Za-z]*)\\s*$"
    );

    const QRegularExpressionMatch m = kPattern.match(normalized);
    if (!m.hasMatch()) {
        return false;
    }

    bool ok = false;
    const double base = QLocale::c().toDouble(m.captured(1), &ok);
    if (!ok) {
        return false;
    }

    double factor = 1.0;
    QString unit;
    if (!splitSuffix(m.captured(2), factor, unit)) {
        return false;
    }

    outValue = base * factor;
    return true;
}

} // namespace SimValueParser
