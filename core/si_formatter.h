#ifndef SI_FORMATTER_H
#define SI_FORMATTER_H

#include <QString>
#include <cmath>

class SiFormatter {
public:
    static QString format(double val, const QString &unit = "") {
        double absVal = std::abs(val);
        if (absVal < 1e-21) return "0" + unit;
        
        static const struct { double mult; const char* sym; } suffixes[] = {
            {1e18, "E"}, {1e15, "P"}, {1e12, "T"}, {1e9, "G"}, {1e6, "M"}, {1e3, "k"},
            {1.0, ""},
            {1e-3, "m"}, {1e-6, "u"}, {1e-9, "n"}, {1e-12, "p"}, {1e-15, "f"}, {1e-18, "a"}
        };
        
        for (const auto& s : suffixes) {
            if (absVal >= s.mult * 0.999) {
                return QString::number(val / s.mult, 'g', 4) + s.sym + unit;
            }
        }
        return QString::number(val, 'g', 4) + unit;
    }
};

#endif // SI_FORMATTER_H
