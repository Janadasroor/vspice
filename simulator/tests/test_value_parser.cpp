#include "../core/sim_value_parser.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {
void expectOk(const char* text, double expected, double tol = 1e-12) {
    double out = 0.0;
    const bool ok = SimValueParser::parseSpiceNumber(QString::fromLatin1(text), out);
    if (!ok) {
        std::cerr << "FAILED: parseSpiceNumber(\"" << text << "\") returned false, expected " << expected << std::endl;
    }
    assert(ok && "expected parse success");
    if (std::abs(out - expected) > tol * std::max(1.0, std::abs(expected))) {
        std::cerr << "FAILED: parseSpiceNumber(\"" << text << "\") returned " << out << ", expected " << expected << std::endl;
    }
    assert(std::abs(out - expected) <= tol * std::max(1.0, std::abs(expected)));
}

void expectFail(const char* text) {
    double out = 0.0;
    const bool ok = SimValueParser::parseSpiceNumber(QString::fromLatin1(text), out);
    assert(!ok && "expected parse failure");
}
} // namespace

int main() {
    // Basic numeric
    expectOk("10", 10.0);
    expectOk("-2.5", -2.5);
    expectOk("1e-3", 1e-3);
    expectOk("+2.2e3", 2.2e3);

    // Engineering suffixes
    expectOk("1k", 1e3);
    expectOk("4.7u", 4.7e-6);
    expectOk("33n", 33e-9);
    expectOk("10p", 10e-12);
    expectOk("2meg", 2e6);
    expectOk("1m", 1e-3);
    expectOk("1mil", 25.4e-6);

    // Optional units and locale-safe decimal rules
    expectOk("3.3V", 3.3);
    expectOk("2mA", 2e-3);
    expectOk("1kHz", 1e3);
    expectOk("5us", 5e-6);
    expectOk("10uF", 10e-6);
    expectOk("100nF", 100e-9);
    expectOk("10uH", 10e-6);
    expectOk("10uHenry", 10e-6);
    expectOk("1MEGhz", 1e6);
    {
        QString val = "1k";
        val += QChar(0x03A9);
        double out = 0.0;
        const bool ok = SimValueParser::parseSpiceNumber(val, out);
        assert(ok && "expected parse success for omega suffix");
        assert(std::abs(out - 1e3) <= 1e-9);
    }

    // Edge/malformed cases
    expectOk("  1.0e-6  ", 1e-6);
    expectOk("1x", 1.0); // SPICE ignores trailing junk
    expectOk("1,5", 1.0); // SPICE ignores trailing junk (comma)
    expectFail("");
    expectFail("abc");
    expectFail("1..2");
    expectFail("1e");
    expectFail("1..2");
    expectFail("1e");

    std::cout << "simulator.value_parser: all tests passed" << std::endl;
    return 0;
}
