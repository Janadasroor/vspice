#include "../core/sim_model_parser.h"
#include "../core/sim_value_parser.h"

#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string randomToken(std::mt19937_64& rng, size_t maxLen) {
    static const char charset[] =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789"
        " .,+-*/_=()[]{}:;#@$%^&!?\t";

    std::uniform_int_distribution<size_t> lenDist(0, maxLen);
    std::uniform_int_distribution<size_t> chDist(0, sizeof(charset) - 2);
    const size_t len = lenDist(rng);

    std::string out;
    out.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(charset[chDist(rng)]);
    }
    return out;
}

std::string randomLibraryBlob(std::mt19937_64& rng, size_t lines, size_t maxTokenLen) {
    std::string out;
    out.reserve(lines * (maxTokenLen + 4));
    for (size_t i = 0; i < lines; ++i) {
        out += randomToken(rng, maxTokenLen);
        out.push_back('\n');
    }
    return out;
}

void fuzzValueParser() {
    std::mt19937_64 rng(0x5A17C0DEULL);
    std::vector<std::string> corpus = {
        "", " ", ".", "+", "-", "1", "1k", "1meg", "-3.3m", "1e-9", "1e309",
        "999999999999999999999999999999", "1.2.3", "nan", "inf", "--1",
        "1 k", "10ohm", "4.7uF", "-12v", "7hz", "5s", "abc", "1e-9999", "++--",
        "   +42.0mA  ", "0", "-0", ".5", "5.", "2E+10", "2E-10", "2e"
    };

    for (const std::string& seed : corpus) {
        double out = 0.0;
        const bool ok = SimValueParser::parseSpiceNumber(QString::fromStdString(seed), out);
        if (ok) {
            require(!std::isnan(out), "value parser returned NaN for corpus item");
        }
    }

    for (int i = 0; i < 10000; ++i) {
        const std::string token = randomToken(rng, 128);
        double out = 0.0;
        const bool ok = SimValueParser::parseSpiceNumber(QString::fromStdString(token), out);
        if (ok) {
            require(!std::isnan(out), "value parser returned NaN for fuzz token");
        }
    }
}

void fuzzModelLineParser() {
    SimNetlist netlist;
    require(SimModelParser::parseModelLine(netlist, ".model DFAST D (IS=1e-14 N=1.2)"),
            "failed to parse valid diode model");
    require(netlist.findModel("DFAST") != nullptr, "valid model not registered");

    std::mt19937_64 rng(0x10D31F77ULL);
    std::vector<std::string> lines = {
        "", "* comment", ".model", ".model M1", ".model M2 BADTYPE",
        ".MODEL QNPN NPN (BF=100 IS=1e-15)",
        ".model weird D (IS=abc N=1.2)",
        ".model M3 PMOS (KP=2e-5 VTO=-2.5 LAMBDA=0.01)",
        ".model M4 NMOS ((((KP=1e-4))))",
        ".model M5 D (IS=1e309 N=-1)"
    };

    for (const std::string& line : lines) {
        (void)SimModelParser::parseModelLine(netlist, line);
    }

    for (int i = 0; i < 5000; ++i) {
        std::string line;
        if ((i % 3) == 0) {
            line = ".model " + randomToken(rng, 24) + " " + randomToken(rng, 12) + " " + randomToken(rng, 64);
        } else {
            line = randomToken(rng, 140);
        }
        (void)SimModelParser::parseModelLine(netlist, line);
    }
}

void fuzzLibraryParser() {
    SimNetlist validNetlist;
    const std::string validLib =
        ".model QN NPN (BF=120 IS=1e-15)\n"
        ".subckt AMP IN OUT VCC VEE\n"
        "R1 1 2 1k\n"
        "C1 2 0 10n\n"
        ".ends\n";
    require(SimModelParser::parseLibrary(validNetlist, validLib), "failed to parse valid library");
    require(validNetlist.findModel("QN") != nullptr, "valid library model missing");
    require(validNetlist.findSubcircuit("AMP") != nullptr, "valid library subcircuit missing");

    std::mt19937_64 rng(0xBADC0FFEEULL);
    std::vector<std::string> corpora = {
        "",
        ".subckt\n.ends\n",
        ".subckt X A B\nR1 A B 1k\n",
        ".model X D (IS=1e-12)\n.random garbage\n",
        ".subckt TOP 1 2 3\nX1 1 2 CHILD\n.ends\n.subckt CHILD A B\nR1 A B 10\n.ends\n"
    };

    for (const std::string& content : corpora) {
        SimNetlist netlist;
        (void)SimModelParser::parseLibrary(netlist, content);
    }

    for (int i = 0; i < 1000; ++i) {
        const std::string blob = randomLibraryBlob(rng, 1 + (i % 16), 120);
        SimNetlist netlist;
        (void)SimModelParser::parseLibrary(netlist, blob);
    }
}

} // namespace

int main() {
    try {
        fuzzValueParser();
        fuzzModelLineParser();
        fuzzLibraryParser();
        std::cout << "[PASS] parser fuzz checks passed." << std::endl;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "[FAIL] " << ex.what() << std::endl;
        return 1;
    }
}
