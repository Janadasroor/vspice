#include "sim_value_parser.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

#include <QString>

namespace {

std::string trim(std::string_view sv) {
    auto start = sv.begin();
    while (start != sv.end() && std::isspace(static_cast<unsigned char>(*start))) ++start;
    if (start == sv.end()) return {};
    auto end = sv.end();
    --end;
    while (end != start && std::isspace(static_cast<unsigned char>(*end))) --end;
    return std::string(start, end + 1 - start);
}

std::string toLower(std::string_view sv) {
    std::string out(sv.size(), '\0');
    std::transform(sv.begin(), sv.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool unitAllowed(std::string_view u) {
    return u.empty() || u == "v" || u == "a" || u == "ohm" || u == "hz" || u == "s" ||
           u == "f" || u == "h";
}

bool splitSuffix(std::string_view suffixRaw, double& factor, std::string& unit) {
    std::string suffix = toLower(trim(suffixRaw));

    // Normalize micro and ohm Unicode variants to ASCII equivalents.
    // µ (U+00B5) -> u
    // μ (U+03BC) -> u
    // Ω (U+03A9) -> ohm
    // Ω (U+2126) -> ohm
    std::string normalized;
    normalized.reserve(suffix.size());
    for (size_t i = 0; i < suffix.size(); ) {
        unsigned char c = static_cast<unsigned char>(suffix[i]);
        if (c == 0xC2 && i + 1 < suffix.size() && static_cast<unsigned char>(suffix[i + 1]) == 0xB5) {
            normalized += 'u';
            i += 2;
        } else if (c == 0xCE && i + 1 < suffix.size() && static_cast<unsigned char>(suffix[i + 1]) == 0xBC) {
            normalized += 'u';
            i += 2;
        } else if (c == 0xCE && i + 1 < suffix.size() && static_cast<unsigned char>(suffix[i + 1]) == 0xA9) {
            normalized += "ohm";
            i += 2;
        } else if (c == 0xE2 && i + 2 < suffix.size() &&
                   static_cast<unsigned char>(suffix[i + 1]) == 0x84 &&
                   static_cast<unsigned char>(suffix[i + 2]) == 0xA6) {
            normalized += "ohm";
            i += 3;
        } else {
            normalized += static_cast<char>(c);
            ++i;
        }
    }
    suffix = normalized;
    factor = 1.0;
    unit = suffix;

    if (suffix.empty()) {
        return true;
    }

    // SPICE standard prefixes. Order matters: 'meg' before 'm'.
    if (suffix.rfind("meg", 0) == 0) {
        factor = 1e6;
        return true;
    }
    if (suffix.rfind("mil", 0) == 0) {
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
        if (!suffix.empty() && suffix[0] == p.key) {
            factor = p.factor;
            return true;
        }
    }

    // No prefix found, standard unit or just numeric trailing chars are ignored in SPICE
    return true;
}
} // namespace

namespace SimValueParser {

bool parseSpiceNumber(std::string_view text, double& outValue) {
    // Normalize Unicode micro/omega variants.
    std::string normalized;
    normalized.reserve(text.size());
    for (size_t i = 0; i < text.size(); ) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == 0xC2 && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0xB5) {
            normalized += 'u';
            i += 2;
        } else if (c == 0xCE && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0xBC) {
            normalized += 'u';
            i += 2;
        } else if (c == 0xCE && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0xA9) {
            normalized += "ohm";
            i += 2;
        } else if (c == 0xE2 && i + 2 < text.size() &&
                   static_cast<unsigned char>(text[i + 1]) == 0x84 &&
                   static_cast<unsigned char>(text[i + 2]) == 0xA6) {
            normalized += "ohm";
            i += 3;
        } else {
            normalized += static_cast<char>(c);
            ++i;
        }
    }

    // Simple regex: optional sign, digits with optional decimal, optional exponent, optional unit suffix.
    size_t pos = 0;
    // Skip leading whitespace
    while (pos < normalized.size() && std::isspace(static_cast<unsigned char>(normalized[pos]))) ++pos;

    if (pos == normalized.size()) return false;

    size_t start = pos;
    // Optional sign
    if (normalized[pos] == '+' || normalized[pos] == '-') ++pos;

    // Digits with optional decimal point
    bool hasDigits = false;
    while (pos < normalized.size() && std::isdigit(static_cast<unsigned char>(normalized[pos]))) {
        hasDigits = true;
        ++pos;
    }
    if (pos < normalized.size() && normalized[pos] == '.') {
        ++pos;
        while (pos < normalized.size() && std::isdigit(static_cast<unsigned char>(normalized[pos]))) {
            hasDigits = true;
            ++pos;
        }
    }
    if (!hasDigits) return false;

    // Optional exponent
    if (pos < normalized.size() && (normalized[pos] == 'e' || normalized[pos] == 'E')) {
        ++pos;
        if (pos < normalized.size() && (normalized[pos] == '+' || normalized[pos] == '-')) ++pos;
        if (pos == normalized.size() || !std::isdigit(static_cast<unsigned char>(normalized[pos]))) return false;
        while (pos < normalized.size() && std::isdigit(static_cast<unsigned char>(normalized[pos]))) ++pos;
    }

    // Extract numeric part
    std::string numStr = normalized.substr(start, pos - start);
    double base;
    try {
        base = std::stod(numStr);
    } catch (...) {
        return false;
    }

    // Remaining part is unit suffix
    std::string suffix;
    while (pos < normalized.size()) {
        if (std::isalpha(static_cast<unsigned char>(normalized[pos]))) {
            suffix += static_cast<char>(std::tolower(static_cast<unsigned char>(normalized[pos])));
        } else if (!std::isspace(static_cast<unsigned char>(normalized[pos]))) {
            return false; // Invalid character
        }
        ++pos;
    }
    while (pos < normalized.size() && std::isspace(static_cast<unsigned char>(normalized[pos]))) ++pos;

    double factor = 1.0;
    std::string unit;
    if (!splitSuffix(suffix, factor, unit)) {
        return false;
    }

    if (!unitAllowed(unit)) return false;

    outValue = base * factor;
    return true;
}

// Qt convenience overload
bool parseSpiceNumber(const QString& text, double& outValue) {
    return parseSpiceNumber(text.toStdString(), outValue);
}

} // namespace SimValueParser
