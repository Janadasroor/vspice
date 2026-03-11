#include "../core/sim_engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include <sys/resource.h>
#include <unistd.h>

namespace {

struct Sample {
    double elapsedMs = 0.0;
    long rssKb = -1;
};

struct Metric {
    std::string analysis;
    std::string circuit;
    int iterations = 0;
    double avgMs = 0.0;
    double minMs = 0.0;
    double maxMs = 0.0;
    double avgRssKb = 0.0;
    long maxRssKb = -1;
};

long currentRssKb() {
    std::ifstream statm("/proc/self/statm");
    long totalPages = 0;
    long residentPages = 0;
    if (!(statm >> totalPages >> residentPages)) {
        return -1;
    }
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) return -1;
    return (residentPages * pageSize) / 1024;
}

long processPeakRssKb() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return -1;
    }
    return usage.ru_maxrss;
}

SimNetlist buildOpDivider() {
    SimNetlist netlist;
    const int n1 = netlist.addNode("N1");
    const int n2 = netlist.addNode("N2");

    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["voltage"] = 10.0;
    netlist.addComponent(v1);

    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, n2};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    SimComponentInstance r2;
    r2.name = "R2";
    r2.type = SimComponentType::Resistor;
    r2.nodes = {n2, 0};
    r2.params["resistance"] = 1000.0;
    netlist.addComponent(r2);

    SimAnalysisConfig cfg;
    cfg.type = SimAnalysisType::OP;
    netlist.setAnalysis(cfg);
    return netlist;
}

SimNetlist buildTranRc() {
    SimNetlist netlist;
    const int n1 = netlist.addNode("N1");
    const int n2 = netlist.addNode("N2");

    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["voltage"] = 5.0;
    netlist.addComponent(v1);

    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, n2};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    SimComponentInstance c1;
    c1.name = "C1";
    c1.type = SimComponentType::Capacitor;
    c1.nodes = {n2, 0};
    c1.params["capacitance"] = 100e-9;
    netlist.addComponent(c1);

    SimAnalysisConfig cfg;
    cfg.type = SimAnalysisType::Transient;
    cfg.tStart = 0.0;
    cfg.tStop = 2e-3;
    cfg.tStep = 2e-6;
    cfg.useAdaptiveStep = false;
    netlist.setAnalysis(cfg);
    return netlist;
}

SimNetlist buildAcRcLowpass() {
    SimNetlist netlist;
    const int n1 = netlist.addNode("N1");
    const int n2 = netlist.addNode("N2");

    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["ac_mag"] = 1.0;
    netlist.addComponent(v1);

    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, n2};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    SimComponentInstance c1;
    c1.name = "C1";
    c1.type = SimComponentType::Capacitor;
    c1.nodes = {n2, 0};
    c1.params["capacitance"] = 100e-9;
    netlist.addComponent(c1);

    SimAnalysisConfig cfg;
    cfg.type = SimAnalysisType::AC;
    cfg.fStart = 10.0;
    cfg.fStop = 1e6;
    cfg.fPoints = 64;
    netlist.setAnalysis(cfg);
    return netlist;
}

bool resultsLookValid(const SimResults& results, SimAnalysisType type) {
    if (!results.isSchemaCompatible()) return false;
    if (type == SimAnalysisType::OP) {
        const auto it = results.nodeVoltages.find("N2");
        return it != results.nodeVoltages.end() && std::isfinite(it->second);
    }
    if (type == SimAnalysisType::Transient || type == SimAnalysisType::AC) {
        return !results.waveforms.empty() && !results.waveforms.front().xData.empty();
    }
    return true;
}

Metric runProfile(const std::string& analysis,
                  const std::string& circuit,
                  const SimNetlist& netlist,
                  SimAnalysisType type,
                  int iterations) {
    SimEngine engine;

    // Warm-up for more stable timing.
    const SimResults warm = engine.run(netlist);
    if (!resultsLookValid(warm, type)) {
        throw std::runtime_error("invalid warm-up result for " + analysis + ":" + circuit);
    }

    std::vector<Sample> samples;
    samples.reserve(static_cast<size_t>(iterations));
    for (int i = 0; i < iterations; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        const SimResults results = engine.run(netlist);
        const auto t1 = std::chrono::steady_clock::now();
        if (!resultsLookValid(results, type)) {
            throw std::runtime_error("invalid benchmark result for " + analysis + ":" + circuit);
        }
        const std::chrono::duration<double, std::milli> elapsed = t1 - t0;
        samples.push_back({elapsed.count(), currentRssKb()});
    }

    Metric m;
    m.analysis = analysis;
    m.circuit = circuit;
    m.iterations = iterations;

    m.minMs = std::numeric_limits<double>::infinity();
    m.maxMs = 0.0;
    double sumMs = 0.0;
    double sumRss = 0.0;
    long rssMax = -1;
    int rssCount = 0;
    for (const Sample& s : samples) {
        m.minMs = std::min(m.minMs, s.elapsedMs);
        m.maxMs = std::max(m.maxMs, s.elapsedMs);
        sumMs += s.elapsedMs;
        if (s.rssKb >= 0) {
            sumRss += static_cast<double>(s.rssKb);
            rssMax = std::max(rssMax, s.rssKb);
            rssCount++;
        }
    }
    m.avgMs = sumMs / static_cast<double>(iterations);
    m.avgRssKb = (rssCount > 0) ? (sumRss / static_cast<double>(rssCount)) : -1.0;
    m.maxRssKb = rssMax;
    return m;
}

std::string escapeJson(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (char c : in) {
        if (c == '\\' || c == '"') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

std::string toJson(const std::vector<Metric>& metrics, int iterations) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(6);
    ss << "{\n";
    ss << "  \"profile_version\": 1,\n";
    ss << "  \"iterations\": " << iterations << ",\n";
    ss << "  \"process_peak_rss_kb\": " << processPeakRssKb() << ",\n";
    ss << "  \"metrics\": [\n";
    for (size_t i = 0; i < metrics.size(); ++i) {
        const Metric& m = metrics[i];
        ss << "    {\n";
        ss << "      \"analysis\": \"" << escapeJson(m.analysis) << "\",\n";
        ss << "      \"circuit\": \"" << escapeJson(m.circuit) << "\",\n";
        ss << "      \"iterations\": " << m.iterations << ",\n";
        ss << "      \"avg_ms\": " << m.avgMs << ",\n";
        ss << "      \"min_ms\": " << m.minMs << ",\n";
        ss << "      \"max_ms\": " << m.maxMs << ",\n";
        ss << "      \"avg_rss_kb\": " << m.avgRssKb << ",\n";
        ss << "      \"max_rss_kb\": " << m.maxRssKb << "\n";
        ss << "    }" << (i + 1 < metrics.size() ? "," : "") << "\n";
    }
    ss << "  ]\n";
    ss << "}\n";
    return ss.str();
}

} // namespace

int main(int argc, char** argv) {
    std::string outPath;
    int iterations = 20;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc) {
            outPath = argv[++i];
        } else if (arg == "--iterations" && i + 1 < argc) {
            iterations = std::max(1, std::atoi(argv[++i]));
        } else {
            outPath = arg;
        }
    }

    try {
        std::vector<Metric> metrics;
        metrics.push_back(runProfile("op", "voltage_divider", buildOpDivider(), SimAnalysisType::OP, iterations));
        metrics.push_back(runProfile("tran", "rc_charge", buildTranRc(), SimAnalysisType::Transient, iterations));
        metrics.push_back(runProfile("ac", "rc_lowpass", buildAcRcLowpass(), SimAnalysisType::AC, iterations));

        const std::string json = toJson(metrics, iterations);
        std::cout << json;

        if (!outPath.empty()) {
            std::ofstream out(outPath, std::ios::out | std::ios::trunc);
            if (!out.is_open()) {
                std::cerr << "failed to open output file: " << outPath << std::endl;
                return 1;
            }
            out << json;
            out.close();
        }

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "simulator profiling harness failed: " << ex.what() << std::endl;
        return 1;
    }
}
