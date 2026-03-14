#include "sim_component.h"
#include "sim_matrix.h"
#include "sim_netlist.h"
#include "flux_script_device.h"
#include <cmath>
#include <iostream>
#include <algorithm>

// --- Resistor Model ---
void ResistorModel::stamp(SimMNAMatrix& matrix, const SimNetlist&, const SimComponentInstance& inst, int&, double) {
    if (inst.nodes.size() < 2) return;
    double r = inst.params.at("resistance");
    if (r < 1e-9) r = 1e-9;
    double g = 1.0 / r;
    int n1 = inst.nodes[0];
    int n2 = inst.nodes[1];
    matrix.addG(n1, n1, g);
    matrix.addG(n2, n2, g);
    matrix.addG(n1, n2, -g);
    matrix.addG(n2, n1, -g);
}
void ResistorModel::stampAC(SimComplexMNAMatrix& matrix, const SimNetlist&, const SimComponentInstance& inst, double, int&) {
    if (inst.nodes.size() < 2) return;
    double r = inst.params.at("resistance");
    if (r < 1e-9) r = 1e-9;
    double g = 1.0 / r;
    int n1 = inst.nodes[0], n2 = inst.nodes[1];
    matrix.addG(n1, n1, g); matrix.addG(n2, n2, g);
    matrix.addG(n1, n2, -g); matrix.addG(n2, n1, -g);
}

// --- Capacitor Model ---
void CapacitorModel::stamp(SimMNAMatrix&, const SimNetlist&, const SimComponentInstance&, int&, double) {}
void CapacitorModel::stampTransient(SimMNAMatrix& matrix, const SimNetlist&, const SimComponentInstance& inst,
                                   double, double h, const std::vector<double>& prevSolution, 
                                   const std::vector<double>& prev2Solution, SimIntegrationMethod method, int&) {
    if (inst.nodes.size() < 2) return;
    double c = inst.params.at("capacitance");
    if (c < 1e-18) c = 1e-18;
    int n1 = inst.nodes[0], n2 = inst.nodes[1];
    double v_prev = (n1 > 0 ? prevSolution[n1-1] : 0) - (n2 > 0 ? prevSolution[n2-1] : 0);
    double v_prev2 = (n1 > 0 && !prev2Solution.empty() ? prev2Solution[n1-1] : 0) - (n2 > 0 && !prev2Solution.empty() ? prev2Solution[n2-1] : 0);
    
    double g, i_eq;
    if (method == SimIntegrationMethod::Trapezoidal) {
        g = 2.0 * c / h;
        i_eq = g * v_prev; // Simplified companion model
    } else if (method == SimIntegrationMethod::Gear2 && !prev2Solution.empty()) {
        g = 1.5 * c / h;
        i_eq = (2.0 * c / h) * v_prev - (0.5 * c / h) * v_prev2;
    } else { // Backward Euler
        g = c / h;
        i_eq = g * v_prev;
    }
    
    matrix.addG(n1, n1, g); matrix.addG(n2, n2, g);
    matrix.addG(n1, n2, -g); matrix.addG(n2, n1, -g);
    matrix.addI(n1, i_eq); matrix.addI(n2, -i_eq);
}
void CapacitorModel::stampAC(SimComplexMNAMatrix& matrix, const SimNetlist&, const SimComponentInstance& inst, double omega, int&) {
    if (inst.nodes.size() < 2) return;
    std::complex<double> y(0, omega * inst.params.at("capacitance"));
    int n1 = inst.nodes[0], n2 = inst.nodes[1];
    matrix.addG(n1, n1, y); matrix.addG(n2, n2, y);
    matrix.addG(n1, n2, -y); matrix.addG(n2, n1, -y);
}
double CapacitorModel::calculateLTE(const SimComponentInstance& inst, double h, const std::vector<double>& sol, const std::vector<double>& prevSol, const std::vector<double>& prev2Sol, int, int&, double relTol, double absTol) {
    if (inst.nodes.size() < 2 || prev2Sol.empty()) return 0.0;
    const int n1 = inst.nodes[0], n2 = inst.nodes[1];
    
    auto getV = [&](const std::vector<double>& s) {
        double v1 = (n1 > 0) ? s[n1 - 1] : 0.0;
        double v2 = (n2 > 0) ? s[n2 - 1] : 0.0;
        return v1 - v2;
    };

    double vn = getV(sol);
    double vn_1 = getV(prevSol);
    double vn_2 = getV(prev2Sol);

    // Truncation error in Volts
    double lteRel = (1.0 / 12.0) * std::abs(vn - 2.0 * vn_1 + vn_2);
    
    // Scale tolerance based on local signal magnitude
    double scale = std::max({1.0, std::abs(vn), std::abs(vn_1)});
    double tol = absTol + relTol * scale;
    
    return lteRel / tol;
}

// --- Inductor Model ---
void InductorModel::stamp(SimMNAMatrix& matrix, const SimNetlist&, const SimComponentInstance& inst, int& vSourceCounter, double) {
    if (inst.nodes.size() < 2) return;
    int n1 = inst.nodes[0], n2 = inst.nodes[1];
    int vIdx = vSourceCounter++;
    matrix.addB(n1, vIdx, 1.0); matrix.addB(n2, vIdx, -1.0);
    matrix.addC(vIdx, n1, 1.0); matrix.addC(vIdx, n2, -1.0);
}
void InductorModel::stampTransient(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst,
                                 double, double h, const std::vector<double>& prevSolution, 
                                 const std::vector<double>& prev2Solution, SimIntegrationMethod method, int& vSourceCounter) {
    if (inst.nodes.size() < 2) return;
    double l = inst.params.at("inductance");
    if (l < 1e-18) l = 1e-18;
    int n1 = inst.nodes[0], n2 = inst.nodes[1];
    int vIdx = vSourceCounter++;
    int solIdx = netlist.nodeCount() - 1 + inst.vIdx;
    double i_prev = (solIdx >= 0 && solIdx < (int)prevSolution.size()) ? prevSolution[solIdx] : 0;
    double i_prev2 = (solIdx >= 0 && solIdx < (int)prev2Solution.size()) ? prev2Solution[solIdx] : 0;

    matrix.addB(n1, vIdx, 1.0); matrix.addB(n2, vIdx, -1.0);
    matrix.addC(vIdx, n1, 1.0); matrix.addC(vIdx, n2, -1.0);
    
    double r_eq, e_eq;
    if (method == SimIntegrationMethod::Trapezoidal) {
        r_eq = 2.0 * l / h;
        e_eq = -r_eq * i_prev; 
    } else if (method == SimIntegrationMethod::Gear2 && !prev2Solution.empty()) {
        r_eq = 1.5 * l / h;
        e_eq = -(2.0 * l / h) * i_prev + (0.5 * l / h) * i_prev2;
    } else { // Backward Euler
        r_eq = l / h;
        e_eq = -r_eq * i_prev;
    }
    
    matrix.addD(vIdx, vIdx, -r_eq);
    matrix.addE(vIdx, e_eq);
}
void InductorModel::stampAC(SimComplexMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, double omega, int& vSourceCounter) {
    if (inst.nodes.size() < 2) return;
    int n1 = inst.nodes[0], n2 = inst.nodes[1];
    int vIdx = vSourceCounter++;
    std::complex<double> z(0, omega * inst.params.at("inductance"));
    matrix.addB(n1, vIdx, 1.0); matrix.addB(n2, vIdx, -1.0);
    matrix.addC(vIdx, n1, 1.0); matrix.addC(vIdx, n2, -1.0);
    matrix.addD(vIdx, vIdx, -z);
}
double InductorModel::calculateLTE(const SimComponentInstance& inst, double, const std::vector<double>& sol, const std::vector<double>& prevSol, const std::vector<double>& prev2Sol, int nodes, int& vSourceCounter, double relTol, double absTol) {
    if (prev2Sol.empty()) { vSourceCounter++; return 0.0; }
    const int vIdx = nodes - 1 + vSourceCounter++;
    
    if (vIdx < 0 || vIdx >= static_cast<int>(sol.size())) return 0.0;

    double in = sol[vIdx];
    double in_1 = prevSol[vIdx];
    double in_2 = prev2Sol[vIdx];

    double lteRel = (1.0 / 12.0) * std::abs(in - 2.0 * in_1 + in_2);
    double scale = std::max({1.0, std::abs(in), std::abs(in_1)});
    double tol = absTol + relTol * scale;

    return lteRel / tol;
}

// --- Voltage Source Model ---
void VoltageSourceModel::stamp(SimMNAMatrix& matrix, const SimNetlist&, const SimComponentInstance& inst, int& vSourceCounter, double sourceFactor) {
    if (inst.nodes.size() < 2) return;
    int n1 = inst.nodes[0], n2 = inst.nodes[1];
    int vIdx = vSourceCounter++;
    double val = inst.params.count("voltage") ? inst.params.at("voltage") : 0.0;
    
    // In DC OP, if we have a step source, use v_init
    if (inst.params.count("v_init")) {
        val = inst.params.at("v_init");
    }
    if (inst.params.count("wave_type")) {
        const int w = static_cast<int>(inst.params.at("wave_type"));
        if (w == 1 && inst.params.count("v_offset")) { // SIN
            val = inst.params.at("v_offset");
        } else if (w == 2 && inst.params.count("pulse_v1")) { // PULSE
            val = inst.params.at("pulse_v1");
        } else if (w == 3 && inst.params.count("exp_v1")) { // EXP
            val = inst.params.at("exp_v1");
        } else if (w == 4 && inst.params.count("sffm_offset")) { // SFFM
            val = inst.params.at("sffm_offset");
        } else if (w == 5 && inst.params.count("pwl_v0")) { // PWL
            val = inst.params.at("pwl_v0");
        } else if (w == 6) { // AM
            val = 0.0;
        } else if (w == 7 && inst.params.count("fm_offset")) { // FM
            val = inst.params.at("fm_offset");
        }
    }
    
    matrix.addB(n1, vIdx, 1.0); matrix.addB(n2, vIdx, -1.0);
    matrix.addC(vIdx, n1, 1.0); matrix.addC(vIdx, n2, -1.0);
    matrix.addE(vIdx, val * sourceFactor);
}
void VoltageSourceModel::stampTransient(SimMNAMatrix& matrix, const SimNetlist&, const SimComponentInstance& inst, double t, double, const std::vector<double>&, const std::vector<double>&, SimIntegrationMethod, int& vSourceCounter) {
    if (inst.nodes.size() < 2) return;
    int n1 = inst.nodes[0], n2 = inst.nodes[1];
    int vIdx = vSourceCounter++;
    double val = inst.params.count("voltage") ? inst.params.at("voltage") : 0.0;
    
    if (inst.params.count("v_init") && inst.params.count("v_step")) {
        val = (t <= 1e-12) ? inst.params.at("v_init") : inst.params.at("v_step");
    }

    if (inst.params.count("wave_type")) {
        const int w = static_cast<int>(inst.params.at("wave_type"));
        if (w == 1) { // SIN
            const double off = inst.params.count("v_offset") ? inst.params.at("v_offset") : 0.0;
            const double amp = inst.params.count("v_ampl") ? inst.params.at("v_ampl") : 1.0;
            const double freq = inst.params.count("v_freq") ? inst.params.at("v_freq") : 1000.0;
            const double delay = inst.params.count("v_delay") ? inst.params.at("v_delay") : 0.0;
            const double theta = inst.params.count("v_theta") ? inst.params.at("v_theta") : 0.0;
            const double phaseDeg = inst.params.count("v_phase") ? inst.params.at("v_phase") : 0.0;
            const double ncycles = inst.params.count("v_ncycles") ? inst.params.at("v_ncycles") : 0.0;
            
            const double phase = phaseDeg * M_PI / 180.0;
            const double te = (t > delay) ? (t - delay) : 0.0;
            
            // Damping (Theta)
            double damping = 1.0;
            if (theta != 0 && t > delay) damping = std::exp(-te * theta);
            
            val = off + amp * damping * std::sin(2.0 * M_PI * freq * te + phase);
            
            // NCYCLES support: source returns to offset after n cycles
            if (ncycles > 0 && te > (ncycles / freq)) {
                val = off;
            }
        } else if (w == 2) { // PULSE
            const double v1 = inst.params.count("pulse_v1") ? inst.params.at("pulse_v1") : 0.0;
            const double v2 = inst.params.count("pulse_v2") ? inst.params.at("pulse_v2") : 5.0;
            const double td = inst.params.count("pulse_td") ? inst.params.at("pulse_td") : 0.0;
            const double tr = std::max(inst.params.count("pulse_tr") ? inst.params.at("pulse_tr") : 1e-9, 1e-12);
            const double tf = std::max(inst.params.count("pulse_tf") ? inst.params.at("pulse_tf") : 1e-9, 1e-12);
            const double pw = std::max(inst.params.count("pulse_pw") ? inst.params.at("pulse_pw") : 1e-3, 0.0);
            const double per = std::max(inst.params.count("pulse_per") ? inst.params.at("pulse_per") : (pw + tr + tf + 1e-6), 1e-12);


            if (t < td) {
                val = v1;
            } else {
                const double local = std::fmod(t - td, per);
                if (local < tr) {
                    val = v1 + (v2 - v1) * (local / tr);
                } else if (local < tr + pw) {
                    val = v2;
                } else if (local < tr + pw + tf) {
                    const double x = (local - tr - pw) / tf;
                    val = v2 + (v1 - v2) * x;
                } else {
                    val = v1;
                }
            }
        } else if (w == 3) { // EXP
            const double v1 = inst.params.count("exp_v1") ? inst.params.at("exp_v1") : 0.0;
            const double v2 = inst.params.count("exp_v2") ? inst.params.at("exp_v2") : 5.0;
            const double td1 = inst.params.count("exp_td1") ? inst.params.at("exp_td1") : 0.0;
            const double tau1 = std::max(inst.params.count("exp_tau1") ? inst.params.at("exp_tau1") : 1e-4, 1e-12);
            const double td2 = inst.params.count("exp_td2") ? inst.params.at("exp_td2") : 1e-3;
            const double tau2 = std::max(inst.params.count("exp_tau2") ? inst.params.at("exp_tau2") : 1e-4, 1e-12);

            if (t < td1) {
                val = v1;
            } else if (t < td2) {
                val = v1 + (v2 - v1) * (1.0 - std::exp(-(t - td1) / tau1));
            } else {
                const double rise = (1.0 - std::exp(-(t - td1) / tau1));
                const double fall = (1.0 - std::exp(-(t - td2) / tau2));
                val = v1 + (v2 - v1) * rise + (v1 - v2) * fall;
            }
        } else if (w == 4) { // SFFM
            const double off = inst.params.count("sffm_offset") ? inst.params.at("sffm_offset") : 0.0;
            const double amp = inst.params.count("sffm_ampl") ? inst.params.at("sffm_ampl") : 1.0;
            const double fc = inst.params.count("sffm_carrier_freq") ? inst.params.at("sffm_carrier_freq") : 1000.0;
            const double mdi = inst.params.count("sffm_mod_index") ? inst.params.at("sffm_mod_index") : 1.0;
            const double fs = inst.params.count("sffm_signal_freq") ? inst.params.at("sffm_signal_freq") : 100.0;
            val = off + amp * std::sin(2.0 * M_PI * fc * t + mdi * std::sin(2.0 * M_PI * fs * t));
        } else if (w == 5) { // PWL
            const int nPairs = static_cast<int>(inst.params.count("pwl_n") ? inst.params.at("pwl_n") : 0.0);
            if (nPairs <= 0) {
                val = inst.params.count("voltage") ? inst.params.at("voltage") : 0.0;
            } else {
                auto getPair = [&](int idx, double& ti, double& vi) {
                    const std::string tKey = "pwl_t" + std::to_string(idx);
                    const std::string vKey = "pwl_v" + std::to_string(idx);
                    ti = inst.params.count(tKey) ? inst.params.at(tKey) : 0.0;
                    vi = inst.params.count(vKey) ? inst.params.at(vKey) : 0.0;
                };

                double t0 = 0.0;
                double v0 = 0.0;
                getPair(0, t0, v0);
                if (t <= t0 || nPairs == 1) {
                    val = v0;
                } else {
                    val = v0;
                    bool segmentFound = false;
                    for (int i = 1; i < nPairs; ++i) {
                        double t1 = 0.0;
                        double v1 = 0.0;
                        getPair(i, t1, v1);
                        if (t <= t1) {
                            const double dt = std::max(1e-18, t1 - t0);
                            const double a = (t - t0) / dt;
                            val = v0 + (v1 - v0) * a;
                            segmentFound = true;
                            break;
                        }
                        t0 = t1;
                        v0 = v1;
                    }
                    if (!segmentFound) {
                        val = v0;
                    }
                }
            }
        } else if (w == 6) { // AM
            const double sa = inst.params.count("am_scale") ? inst.params.at("am_scale") : 1.0;
            const double oc = inst.params.count("am_offset_coeff") ? inst.params.at("am_offset_coeff") : 1.0;
            const double fm = inst.params.count("am_mod_freq") ? inst.params.at("am_mod_freq") : 1000.0;
            const double fc = inst.params.count("am_carrier_freq") ? inst.params.at("am_carrier_freq") : 10000.0;
            const double td = inst.params.count("am_delay") ? inst.params.at("am_delay") : 0.0;

            if (t < td) {
                val = 0.0;
            } else {
                const double te = t - td;
                val = sa * (oc + std::sin(2.0 * M_PI * fm * te)) * std::sin(2.0 * M_PI * fc * te);
            }
        } else if (w == 7) { // FM
            const double off = inst.params.count("fm_offset") ? inst.params.at("fm_offset") : 0.0;
            const double amp = inst.params.count("fm_ampl") ? inst.params.at("fm_ampl") : 1.0;
            const double fc = inst.params.count("fm_carrier_freq") ? inst.params.at("fm_carrier_freq") : 10000.0;
            const double fd = inst.params.count("fm_freq_dev") ? inst.params.at("fm_freq_dev") : 2000.0;
            const double fm = std::max(inst.params.count("fm_mod_freq") ? inst.params.at("fm_mod_freq") : 1000.0, 1e-12);
            const double phase = 2.0 * M_PI * fc * t + (fd / fm) * std::sin(2.0 * M_PI * fm * t);
            val = off + amp * std::sin(phase);
        }
    } else if (inst.modelName.find("SINE") != std::string::npos) {
        // Legacy fallback.
        val = 5.0 * std::sin(2.0 * M_PI * 1000.0 * t);
    }
    
    matrix.addB(n1, vIdx, 1.0); matrix.addB(n2, vIdx, -1.0);
    matrix.addC(vIdx, n1, 1.0); matrix.addC(vIdx, n2, -1.0);
    matrix.addE(vIdx, val);
}
void VoltageSourceModel::stampAC(SimComplexMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, double, int& vSourceCounter) {
    if (inst.nodes.size() < 2) return;
    int n1 = inst.nodes[0], n2 = inst.nodes[1];
    int vIdx = vSourceCounter++;
    double mag = inst.params.count("ac_mag") ? inst.params.at("ac_mag") :
                 (inst.params.count("voltage") ? inst.params.at("voltage") : 1.0);
    double phaseDeg = inst.params.count("ac_phase") ? inst.params.at("ac_phase") : 0.0;
    double phase = phaseDeg * M_PI / 180.0;
    std::complex<double> val(mag * std::cos(phase), mag * std::sin(phase));
    matrix.addB(n1, vIdx, 1.0); matrix.addB(n2, vIdx, -1.0);
    matrix.addC(vIdx, n1, 1.0); matrix.addC(vIdx, n2, -1.0);
    matrix.addE(vIdx, val);
}

// --- Current Source Model ---
void CurrentSourceModel::stamp(SimMNAMatrix& matrix, const SimNetlist&, const SimComponentInstance& inst, int&, double sourceFactor) {
    if (inst.nodes.size() < 2) return;
    int nPos = inst.nodes[0], nNeg = inst.nodes[1];

    double val = inst.params.count("current") ? inst.params.at("current") : 0.0;
    if (inst.params.count("i_init")) {
        val = inst.params.at("i_init");
    }
    if (inst.params.count("wave_type")) {
        const int w = static_cast<int>(inst.params.at("wave_type"));
        if (w == 1 && inst.params.count("i_offset")) {
            val = inst.params.at("i_offset");
        } else if (w == 2 && inst.params.count("pulse_i1")) {
            val = inst.params.at("pulse_i1");
        } else if (w == 3 && inst.params.count("exp_i1")) {
            val = inst.params.at("exp_i1");
        }
    }

    const double i = val * sourceFactor;
    matrix.addI(nPos, -i);
    matrix.addI(nNeg, i);
}

void CurrentSourceModel::stampTransient(SimMNAMatrix& matrix, const SimNetlist&, const SimComponentInstance& inst,
                                        double t, double, const std::vector<double>&,
                                        const std::vector<double>&, SimIntegrationMethod, int&) {
    if (inst.nodes.size() < 2) return;
    int nPos = inst.nodes[0], nNeg = inst.nodes[1];

    double val = inst.params.count("current") ? inst.params.at("current") : 0.0;
    if (inst.params.count("i_init") && inst.params.count("i_step")) {
        val = (t <= 1e-12) ? inst.params.at("i_init") : inst.params.at("i_step");
    }

    if (inst.params.count("wave_type")) {
        const int w = static_cast<int>(inst.params.at("wave_type"));
        if (w == 1) { // SIN
            const double off = inst.params.count("i_offset") ? inst.params.at("i_offset") : 0.0;
            const double amp = inst.params.count("i_ampl") ? inst.params.at("i_ampl") : 1.0; // Consistent default 1.0
            const double freq = inst.params.count("i_freq") ? inst.params.at("i_freq") : 1000.0;
            const double delay = inst.params.count("i_delay") ? inst.params.at("i_delay") : 0.0;
            const double theta = inst.params.count("i_theta") ? inst.params.at("i_theta") : 0.0;
            const double phaseDeg = inst.params.count("i_phase") ? inst.params.at("i_phase") : 0.0;
            const double ncycles = inst.params.count("i_ncycles") ? inst.params.at("i_ncycles") : 0.0;

            const double phase = phaseDeg * M_PI / 180.0;
            const double te = (t > delay) ? (t - delay) : 0.0;

            double damping = 1.0;
            if (theta != 0 && t > delay) damping = std::exp(-te * theta);

            val = off + amp * damping * std::sin(2.0 * M_PI * freq * te + phase);

            if (ncycles > 0 && te > (ncycles / freq)) {
                val = off;
            }
        } else if (w == 2) { // PULSE
            const double i1 = inst.params.count("pulse_i1") ? inst.params.at("pulse_i1") : 0.0;
            const double i2 = inst.params.count("pulse_i2") ? inst.params.at("pulse_i2") : 1e-3;
            const double td = inst.params.count("pulse_td") ? inst.params.at("pulse_td") : 0.0;
            const double tr = std::max(inst.params.count("pulse_tr") ? inst.params.at("pulse_tr") : 1e-9, 1e-12);
            const double tf = std::max(inst.params.count("pulse_tf") ? inst.params.at("pulse_tf") : 1e-9, 1e-12);
            const double pw = std::max(inst.params.count("pulse_pw") ? inst.params.at("pulse_pw") : 1e-3, 0.0);
            const double per = std::max(inst.params.count("pulse_per") ? inst.params.at("pulse_per") : (pw + tr + tf + 1e-6), 1e-12);


            if (t < td) {
                val = i1;
            } else {
                const double local = std::fmod(t - td, per);
                if (local < tr) {
                    val = i1 + (i2 - i1) * (local / tr);
                } else if (local < tr + pw) {
                    val = i2;
                } else if (local < tr + pw + tf) {
                    const double x = (local - tr - pw) / tf;
                    val = i2 + (i1 - i2) * x;
                } else {
                    val = i1;
                }
            }
        }
    }

    matrix.addI(nPos, -val);
    matrix.addI(nNeg, val);
}

void CurrentSourceModel::stampAC(SimComplexMNAMatrix& matrix, const SimNetlist&, const SimComponentInstance& inst,
                                 double, int&) {
    if (inst.nodes.size() < 2) return;
    int nPos = inst.nodes[0], nNeg = inst.nodes[1];

    double mag = inst.params.count("ac_mag") ? inst.params.at("ac_mag") :
                 (inst.params.count("current") ? inst.params.at("current") : 1.0);
    double phaseDeg = inst.params.count("ac_phase") ? inst.params.at("ac_phase") : 0.0;
    double phase = phaseDeg * M_PI / 180.0;
    std::complex<double> i(mag * std::cos(phase), mag * std::sin(phase));

    matrix.addI(nPos, -i);
    matrix.addI(nNeg, i);
}

// --- Diode Model ---
void DiodeModel::stamp(SimMNAMatrix&, const SimNetlist&, const SimComponentInstance&, int&, double) {}
void DiodeModel::stampNonlinear(SimMNAMatrix& matrix, const SimNetlist&, const SimComponentInstance& inst, const std::vector<double>& solution, double, int&) {
    if (inst.nodes.size() < 2) return;
    int n1 = inst.nodes[0], n2 = inst.nodes[1];

    const double vd = (n1 > 0 ? solution[n1 - 1] : 0.0) - (n2 > 0 ? solution[n2 - 1] : 0.0);

    // Temperature-aware diode thermal voltage and parameter defaults.
    const double Is = inst.params.count("Is") ? std::max(1e-30, inst.params.at("Is")) : 1e-14;
    const double n = inst.params.count("N") ? std::max(0.5, inst.params.at("N")) : 1.0;
    const double tempC = inst.params.count("temp_c") ? inst.params.at("temp_c") : 27.0;
    const double tempK = std::max(1.0, tempC + 273.15);
    const double kOverQ = 8.617333262145e-5; // V/K
    const double nVt = std::max(1e-6, n * kOverQ * tempK);

    // Exponential limiting to avoid overflow and improve Newton robustness.
    const double arg = vd / nVt;
    const double argHi = 40.0;
    const double argLo = -40.0;

    double id = 0.0;
    double gd = 0.0;
    if (arg > argHi) {
        const double eHi = std::exp(argHi);
        const double linFactor = 1.0 + (arg - argHi);
        id = Is * (eHi * linFactor - 1.0);
        gd = Is * eHi / nVt;
    } else if (arg < argLo) {
        const double eLo = std::exp(argLo);
        id = Is * (eLo - 1.0);
        gd = Is * eLo / nVt;
    } else {
        const double e = std::exp(arg);
        id = Is * (e - 1.0);
        gd = Is * e / nVt;
    }

    if (gd < 1e-12) gd = 1e-12;
    matrix.addG(n1, n1, gd); matrix.addG(n2, n2, gd);
    matrix.addG(n1, n2, -gd); matrix.addG(n2, n1, -gd);
    matrix.addI(n1, -(id - gd * vd)); matrix.addI(n2, id - gd * vd);
}
void DiodeModel::stampAC(SimComplexMNAMatrix&, const SimNetlist&, const SimComponentInstance&, double, int&) {}

// --- BJT Model ---
void BJTModel::stampNonlinear(SimMNAMatrix& matrix, const SimNetlist&, const SimComponentInstance& inst, const std::vector<double>& solution, double, int&) {
    if (inst.nodes.size() < 3) return;
    int nC = inst.nodes[0], nB = inst.nodes[1], nE = inst.nodes[2];
    const bool isPNP = (inst.type == SimComponentType::BJT_PNP);

    const double vbeRaw = (nB > 0 ? solution[nB - 1] : 0.0) - (nE > 0 ? solution[nE - 1] : 0.0);
    // Work in equivalent NPN variables.
    const double vbeEqRaw = isPNP ? -vbeRaw : vbeRaw;

    const double Is = inst.params.count("Is") ? std::max(1e-30, inst.params.at("Is")) : 1e-14;
    const double beta = inst.params.count("Bf") ? std::max(1.0, inst.params.at("Bf")) : 100.0;
    const double n = inst.params.count("Nf") ? std::max(0.5, inst.params.at("Nf")) : 1.0;
    const double tempC = inst.params.count("temp_c") ? inst.params.at("temp_c") : 27.0;
    const double tempK = std::max(1.0, tempC + 273.15);
    const double kOverQ = 8.617333262145e-5; // V/K
    const double nVt = std::max(1e-6, n * kOverQ * tempK);

    // Voltage/exponential limiting to improve convergence and avoid overflow.
    const double vbeEq = std::clamp(vbeEqRaw, -5.0 * nVt, 1.2);
    const double arg = vbeEq / nVt;
    const double argHi = 40.0;
    const double argLo = -40.0;

    double expTerm = 0.0;
    if (arg > argHi) {
        const double eHi = std::exp(argHi);
        expTerm = eHi * (1.0 + (arg - argHi));
    } else if (arg < argLo) {
        expTerm = std::exp(argLo);
    } else {
        expTerm = std::exp(arg);
    }

    const double ibEq = (Is / beta) * (expTerm - 1.0);
    const double icEq = beta * ibEq;
    const double gbeEq = (Is / (beta * nVt)) * expTerm;
    const double gceEq = (Is / nVt) * expTerm;

    double gbe = gbeEq;
    double gce = gceEq;
    if (gbe < 1e-14) gbe = 1e-14;
    if (gce < 1e-14) gce = 1e-14;

    // RHS current terms (Fixed part of linearized model)
    double fixedIb = ibEq - gbeEq * vbeEq;
    double fixedIc = icEq - gceEq * vbeEq;

    // For PNP, physical currents and their derivatives are such that conductances 
    // remain the same but fixed RHS currents are negated.
    if (isPNP) {
        fixedIb = -fixedIb;
        fixedIc = -fixedIc;
    }

    matrix.addG(nB, nB, gbe); matrix.addG(nE, nE, gbe + gce);
    matrix.addG(nB, nE, -gbe); matrix.addG(nE, nB, -(gbe + gce));
    matrix.addG(nC, nB, gce); matrix.addG(nC, nE, -gce);

    matrix.addI(nB, -fixedIb);
    matrix.addI(nE, fixedIb + fixedIc);
    matrix.addI(nC, -fixedIc);
}
void BJTModel::stampAC(SimComplexMNAMatrix&, const SimNetlist&, const SimComponentInstance&, double, int&) {}

// --- MOSFET Model ---
void MOSFETModel::stampNonlinear(SimMNAMatrix& matrix, const SimNetlist&, const SimComponentInstance& inst, const std::vector<double>& solution, double, int&) {
    if (inst.nodes.size() < 3) return;
    int nD = inst.nodes[0], nG = inst.nodes[1], nS = inst.nodes[2];
    const double vgsRaw = (nG > 0 ? solution[nG - 1] : 0.0) - (nS > 0 ? solution[nS - 1] : 0.0);
    const double vdsRaw = (nD > 0 ? solution[nD - 1] : 0.0) - (nS > 0 ? solution[nS - 1] : 0.0);

    const bool isPMOS = (inst.type == SimComponentType::MOSFET_PMOS);
    const double Vth = std::max(1e-3, inst.params.count("Vt") ? std::abs(inst.params.at("Vt")) : 1.0);
    const double Kp = std::max(1e-12, inst.params.count("Kp") ? std::abs(inst.params.at("Kp")) : 2e-3);
    const double beta = std::max(1e-12, Kp / 2.0);
    const double lambda = std::max(0.0, inst.params.count("Lambda") ? inst.params.at("Lambda") : 0.0);

    // Work in NMOS-equivalent variables; PMOS uses inverted terminal voltages.
    const double vgsEq = isPMOS ? -vgsRaw : vgsRaw;
    const double vdsEq = isPMOS ? -vdsRaw : vdsRaw;

    double idEq = 0.0;
    double gmEq = 0.0;
    double gdsEq = 0.0;
    if (vgsEq > Vth) {
        if (vdsEq < vgsEq - Vth) { // Triode
            idEq = beta * (2.0 * (vgsEq - Vth) * vdsEq - vdsEq * vdsEq) * (1.0 + lambda * vdsEq);
            gmEq = 2.0 * beta * vdsEq * (1.0 + lambda * vdsEq);
            gdsEq = 2.0 * beta * (vgsEq - Vth - vdsEq) * (1.0 + lambda * vdsEq)
                  + beta * (2.0 * (vgsEq - Vth) * vdsEq - vdsEq * vdsEq) * lambda;
        } else { // Saturation
            idEq = beta * (vgsEq - Vth) * (vgsEq - Vth) * (1.0 + lambda * vdsEq);
            gmEq = 2.0 * beta * (vgsEq - Vth) * (1.0 + lambda * vdsEq);
            gdsEq = beta * (vgsEq - Vth) * (vgsEq - Vth) * lambda;
        }
    }

    // Map back to drain->source current sign convention used by stamping.
    double id = isPMOS ? -idEq : idEq;
    double gm = gmEq;
    double gds = gdsEq;

    if (gds < 1e-12) gds = 1e-12;

    matrix.addG(nD, nD, gds); matrix.addG(nS, nS, gds + gm);
    matrix.addG(nD, nS, -(gm + gds)); matrix.addG(nS, nD, -gds);
    matrix.addG(nD, nG, gm); matrix.addG(nS, nG, -gm);
    matrix.addI(nD, -(id - gm * vgsRaw - gds * vdsRaw));
    matrix.addI(nS, id - gm * vgsRaw - gds * vdsRaw);
}

// --- Op-Amp Macro Model ---
void OpAmpMacroModel::stamp(SimMNAMatrix& matrix, const SimNetlist&, const SimComponentInstance& inst, int& vSourceCounter, double) {
    if (inst.nodes.empty()) return;
    const int nOut = inst.nodes[0];
    const int vIdx = vSourceCounter++;
    matrix.addB(nOut, vIdx, 1.0);
    matrix.addC(vIdx, nOut, 1.0);
}

void OpAmpMacroModel::stampNonlinear(SimMNAMatrix& matrix, const SimNetlist&, const SimComponentInstance& inst,
                                     const std::vector<double>& solution, double, int& vSourceCounter) {
    if (inst.nodes.size() < 3) return;

    const int nInP = inst.nodes[1];
    const int nInN = inst.nodes[2];
    const int vIdx = vSourceCounter;

    const auto vNode = [&](int n) -> double {
        return (n > 0 && n - 1 < static_cast<int>(solution.size())) ? solution[n - 1] : 0.0;
    };

    const double vin = vNode(nInP) - vNode(nInN);
    const double gain = inst.params.count("gain") ? std::max(1.0, inst.params.at("gain")) : 1e5;

    double vHigh = inst.params.count("vmax") ? inst.params.at("vmax") : 15.0;
    double vLow = inst.params.count("vmin") ? inst.params.at("vmin") : -15.0;
    if (inst.nodes.size() >= 5) {
        vHigh = vNode(inst.nodes[3]);
        vLow = vNode(inst.nodes[4]);
    }
    if (vLow > vHigh) std::swap(vLow, vHigh);

    const double headroom = std::max(0.0, inst.params.count("headroom") ? inst.params.at("headroom") : 0.2);
    const double railSpan = std::max(1e-6, vHigh - vLow);
    const double mid = 0.5 * (vHigh + vLow);
    const double span = std::max(1e-6, 0.5 * railSpan - headroom);

    const double raw = gain * vin;
    const double x = (raw - mid) / span;
    const double sat = std::tanh(x);
    const double target = mid + span * sat;
    const double dTargetDvin = gain * (1.0 - sat * sat);
    const double rhs = target - dTargetDvin * vin;

    matrix.addC(vIdx, nInP, -dTargetDvin);
    matrix.addC(vIdx, nInN, dTargetDvin);
    matrix.addE(vIdx, rhs);
}

// --- Voltage-controlled Switch Model ---
void SwitchModel::stamp(SimMNAMatrix& matrix, const SimNetlist&, const SimComponentInstance& inst, int&, double) {
    if (inst.nodes.size() < 2) return;
    const int nP = inst.nodes[0];
    const int nN = inst.nodes[1];
    const double roff = std::max(1.0, inst.params.count("roff") ? inst.params.at("roff") : 1e9);
    const double gOff = 1.0 / roff;
    matrix.addG(nP, nP, gOff);
    matrix.addG(nN, nN, gOff);
    matrix.addG(nP, nN, -gOff);
    matrix.addG(nN, nP, -gOff);
}

void SwitchModel::stampNonlinear(SimMNAMatrix& matrix, const SimNetlist&, const SimComponentInstance& inst,
                                 const std::vector<double>& solution, double, int&) {
    if (inst.nodes.size() < 4) return;
    const int nP = inst.nodes[0];
    const int nN = inst.nodes[1];
    const int nCP = inst.nodes[2];
    const int nCN = inst.nodes[3];

    const auto vNode = [&](int n) -> double {
        return (n > 0 && n - 1 < static_cast<int>(solution.size())) ? solution[n - 1] : 0.0;
    };

    const double vp = vNode(nP);
    const double vn = vNode(nN);
    const double vc = vNode(nCP) - vNode(nCN);
    const double vt = inst.params.count("vt") ? inst.params.at("vt") : 2.5;
    const double vh = std::max(1e-6, inst.params.count("vh") ? std::abs(inst.params.at("vh")) : 0.2);
    const double ron = std::max(1e-6, inst.params.count("ron") ? inst.params.at("ron") : 1.0);
    const double roff = std::max(1.0, inst.params.count("roff") ? inst.params.at("roff") : 1e9);
    const double gOn = 1.0 / ron;
    const double gOff = 1.0 / roff;

    const double alpha = (vc - vt) / vh;
    const double sw = 0.5 * (1.0 + std::tanh(alpha));
    const double dsw_dvc = 0.5 * (1.0 - std::tanh(alpha) * std::tanh(alpha)) / vh;
    const double g = gOff + (gOn - gOff) * sw;
    const double dg_dvc = (gOn - gOff) * dsw_dvc;

    const double vpn = vp - vn;
    const double i = g * vpn;
    const double di_dvpn = g;
    const double di_dvc = dg_dvc * vpn;

    matrix.addG(nP, nP, di_dvpn);
    matrix.addG(nP, nN, -di_dvpn);
    matrix.addG(nN, nP, -di_dvpn);
    matrix.addG(nN, nN, di_dvpn);

    matrix.addG(nP, nCP, di_dvc);
    matrix.addG(nP, nCN, -di_dvc);
    matrix.addG(nN, nCP, -di_dvc);
    matrix.addG(nN, nCN, di_dvc);

    const double ieq = i - di_dvpn * vpn - di_dvc * vc;
    matrix.addI(nP, -ieq);
    matrix.addI(nN, ieq);
}

// --- Transmission-line (quasi-static) Model ---
void TransmissionLineModel::stamp(SimMNAMatrix& matrix, const SimNetlist&, const SimComponentInstance& inst, int&, double) {
    if (inst.nodes.size() < 4) return;
    const int aP = inst.nodes[0];
    const int aN = inst.nodes[1];
    const int bP = inst.nodes[2];
    const int bN = inst.nodes[3];
    const double z0 = std::max(1e-3, inst.params.count("z0") ? std::abs(inst.params.at("z0")) : 50.0);
    const double g = 1.0 / z0;

    matrix.addG(aP, aP, g); matrix.addG(bP, bP, g);
    matrix.addG(aP, bP, -g); matrix.addG(bP, aP, -g);

    matrix.addG(aN, aN, g); matrix.addG(bN, bN, g);
    matrix.addG(aN, bN, -g); matrix.addG(bN, aN, -g);
}

void TransmissionLineModel::stampAC(SimComplexMNAMatrix& matrix, const SimNetlist&, const SimComponentInstance& inst, double, int&) {
    if (inst.nodes.size() < 4) return;
    const int aP = inst.nodes[0];
    const int aN = inst.nodes[1];
    const int bP = inst.nodes[2];
    const int bN = inst.nodes[3];
    const double z0 = std::max(1e-3, inst.params.count("z0") ? std::abs(inst.params.at("z0")) : 50.0);
    const std::complex<double> g(1.0 / z0, 0.0);

    matrix.addG(aP, aP, g); matrix.addG(bP, bP, g);
    matrix.addG(aP, bP, -g); matrix.addG(bP, aP, -g);

    matrix.addG(aN, aN, g); matrix.addG(bN, bN, g);
    matrix.addG(aN, bN, -g); matrix.addG(bN, aN, -g);
}

// --- Behavioral Sources ---
void BehavioralVoltageSourceModel::stamp(SimMNAMatrix& matrix, const SimNetlist&, const SimComponentInstance& inst, int& vSourceCounter, double) {
    if (inst.nodes.size() < 2) return;
    int nPos = inst.nodes[0], nNeg = inst.nodes[1];
    int vIdx = vSourceCounter++;
    matrix.addB(nPos, vIdx, 1.0); matrix.addB(nNeg, vIdx, -1.0);
    matrix.addC(vIdx, nPos, 1.0); matrix.addC(vIdx, nNeg, -1.0);
}
void BehavioralVoltageSourceModel::stampNonlinear(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, const std::vector<double>& solution, double t, int& vIdx) {
    if (inst.nodes.size() < 2) return;
    int nPos = inst.nodes[0], nNeg = inst.nodes[1];
    std::string exprStr = inst.modelName.empty() ? "0" : inst.modelName;
    Sim::Expression expr(exprStr);
    std::map<std::string, double> vars;
    auto varNames = expr.getVariables();
    for (const auto& v : varNames) {
        if (v.rfind("V(", 0) == 0 && v.back() == ')') {
            int nId = netlist.findNode(v.substr(2, v.size() - 3));
            vars[v] = (nId > 0) ? solution[nId - 1] : 0.0;
        } else if (v.rfind("I(", 0) == 0 && v.back() == ')') {
            std::string compName = v.substr(2, v.size() - 3);
            int vIdxComp = -1;
            for (const auto& c : netlist.components()) if (c.name == compName) { vIdxComp = c.vIdx; break; }
            vars[v] = (vIdxComp >= 0) ? solution[netlist.nodeCount() - 1 + vIdxComp] : 0.0;
        } else if (v == "time" || v == "t") vars[v] = t;
        else vars[v] = 0.0;
    }
    double value = expr.evaluate(vars), jacobianSum = 0;
    for (const auto& v : varNames) {
        if (v.rfind("V(", 0) == 0 && v.back() == ')') {
            int nId = netlist.findNode(v.substr(2, v.size() - 3));
            if (nId > 0) {
                double df_dv = expr.derivative(v, vars);
                matrix.addC(vIdx, nId, -df_dv); 
                jacobianSum += df_dv * solution[nId - 1];
            }
        } else if (v.rfind("I(", 0) == 0 && v.back() == ')') {
            std::string compName = v.substr(2, v.size() - 3);
            int vIdxDeriv = -1;
            for (const auto& c : netlist.components()) if (c.name == compName) { vIdxDeriv = c.vIdx; break; }
            if (vIdxDeriv >= 0) {
                double df_di = expr.derivative(v, vars);
                matrix.addD(vIdx, vIdxDeriv, -df_di);
                jacobianSum += df_di * solution[netlist.nodeCount() - 1 + vIdxDeriv];
            }
        }
    }
    matrix.addE(vIdx, value - jacobianSum);
}


void BehavioralCurrentSourceModel::stamp(SimMNAMatrix&, const SimNetlist&, const SimComponentInstance&, int&, double) {}
void BehavioralCurrentSourceModel::stampNonlinear(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, const std::vector<double>& solution, double t, int&) {
    if (inst.nodes.size() < 2) return;
    int nPos = inst.nodes[0], nNeg = inst.nodes[1];
    std::string exprStr = inst.modelName.empty() ? "0" : inst.modelName;
    Sim::Expression expr(exprStr);
    std::map<std::string, double> vars;
    auto varNames = expr.getVariables();
    for (const auto& v : varNames) {
        if (v.rfind("V(", 0) == 0 && v.back() == ')') {
            int nId = netlist.findNode(v.substr(2, v.size() - 3));
            vars[v] = (nId > 0) ? solution[nId - 1] : 0.0;
        } else if (v.rfind("I(", 0) == 0 && v.back() == ')') {
            std::string compName = v.substr(2, v.size() - 3);
            int vIdxComp = -1;
            for (const auto& c : netlist.components()) if (c.name == compName) { vIdxComp = c.vIdx; break; }
            vars[v] = (vIdxComp >= 0) ? solution[netlist.nodeCount() - 1 + vIdxComp] : 0.0;
        } else if (v == "time" || v == "t") vars[v] = t;
        else vars[v] = 0.0;
    }
    double value = expr.evaluate(vars), jacobianSum = 0;
    for (const auto& v : varNames) {
        if (v.rfind("V(", 0) == 0 && v.back() == ')') {
            int nId = netlist.findNode(v.substr(2, v.size() - 3));
            if (nId > 0) { 
                double df_dv = expr.derivative(v, vars);
                matrix.addG(nPos, nId, df_dv); matrix.addG(nNeg, nId, -df_dv);
                jacobianSum += df_dv * solution[nId - 1];
            }
        } else if (v.rfind("I(", 0) == 0 && v.back() == ')') {
            std::string compName = v.substr(2, v.size() - 3);
            int vIdxDeriv = -1;
            for (const auto& c : netlist.components()) if (c.name == compName) { vIdxDeriv = c.vIdx; break; }
            if (vIdxDeriv >= 0) {
                double df_di = expr.derivative(v, vars);
                matrix.addB(nPos, vIdxDeriv, df_di); matrix.addB(nNeg, vIdxDeriv, -df_di);
                jacobianSum += df_di * solution[netlist.nodeCount() - 1 + vIdxDeriv];
            }
        }
    }
    matrix.addI(nPos, -(value - jacobianSum)); matrix.addI(nNeg, value - jacobianSum);
}


// --- Logic Gate Model ---
void LogicGateModel::stamp(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, int& vSourceCounter, double) {
    if (inst.nodes.empty()) return;
    int nOut = inst.nodes.back();
    int vIdx = vSourceCounter++;
    // The logic gate output is a voltage source.
    // V_out - V_logic = 0
    // KCL at nOut: I_out + ... = 0
    // V_out - V_logic = 0
    // This requires adding a new unknown (current through the voltage source)
    // and a new equation.
    // The current through the voltage source is I_vIdx.
    // KCL at nOut: ... + I_vIdx = 0
    // KCL at ground (if nOut is connected to ground via the source): ... - I_vIdx = 0
    // Equation: V_nOut - V_logic = 0
    // So, matrix.addB(nOut, vIdx, 1.0); matrix.addC(vIdx, nOut, 1.0);
    // The RHS will be handled in stampNonlinear.
    matrix.addB(nOut, vIdx, 1.0);
    matrix.addC(vIdx, nOut, 1.0);
    matrix.addD(vIdx, vIdx, -1e-3);
}

// --- VCVS Model ---
void VCVSModel::stamp(SimMNAMatrix& matrix, const SimNetlist&, const SimComponentInstance& inst, int& vSourceCounter, double) {
    if (inst.nodes.size() < 4) return;
    int nP = inst.nodes[0], nN = inst.nodes[1], cP = inst.nodes[2], cN = inst.nodes[3];
    int vIdx = vSourceCounter++;
    double gain = inst.params.count("gain") ? inst.params.at("gain") : 1.0;
    matrix.addB(nP, vIdx, 1.0); matrix.addB(nN, vIdx, -1.0);
    matrix.addC(vIdx, nP, 1.0); matrix.addC(vIdx, nN, -1.0);
    matrix.addC(vIdx, cP, -gain); matrix.addC(vIdx, cN, gain);
}

void VCVSModel::stampAC(SimComplexMNAMatrix& matrix, const SimNetlist&, const SimComponentInstance& inst, double, int& vSourceCounter) {
    if (inst.nodes.size() < 4) return;
    const int nP = inst.nodes[0], nN = inst.nodes[1], cP = inst.nodes[2], cN = inst.nodes[3];
    const int vIdx = vSourceCounter++;
    const std::complex<double> gain(inst.params.count("gain") ? inst.params.at("gain") : 1.0, 0.0);
    matrix.addB(nP, vIdx, 1.0); matrix.addB(nN, vIdx, -1.0);
    matrix.addC(vIdx, nP, 1.0); matrix.addC(vIdx, nN, -1.0);
    matrix.addC(vIdx, cP, -gain); matrix.addC(vIdx, cN, gain);
}

// --- VCCS Model ---
void VCCSModel::stamp(SimMNAMatrix& matrix, const SimNetlist&, const SimComponentInstance& inst, int&, double) {
    if (inst.nodes.size() < 4) return;
    int nP = inst.nodes[0], nN = inst.nodes[1], cP = inst.nodes[2], cN = inst.nodes[3];
    double gm = inst.params.count("gain") ? inst.params.at("gain") : 1e-3;
    matrix.addG(nP, cP, gm); matrix.addG(nP, cN, -gm);
    matrix.addG(nN, cP, -gm); matrix.addG(nN, cN, gm);
}

void VCCSModel::stampAC(SimComplexMNAMatrix& matrix, const SimNetlist&, const SimComponentInstance& inst, double, int&) {
    if (inst.nodes.size() < 4) return;
    const int nP = inst.nodes[0], nN = inst.nodes[1], cP = inst.nodes[2], cN = inst.nodes[3];
    const std::complex<double> gm(inst.params.count("gain") ? inst.params.at("gain") : 1e-3, 0.0);
    matrix.addG(nP, cP, gm); matrix.addG(nP, cN, -gm);
    matrix.addG(nN, cP, -gm); matrix.addG(nN, cN, gm);
}

// --- CCVS Model ---
void CCVSModel::stamp(SimMNAMatrix& matrix, const SimNetlist&, const SimComponentInstance& inst, int& vSourceCounter, double) {
    if (inst.nodes.size() < 4) return;
    int nP = inst.nodes[0], nN = inst.nodes[1], cP = inst.nodes[2], cN = inst.nodes[3];
    int vIdxSensor = vSourceCounter++;
    int vIdxOut = vSourceCounter++;
    double gain = inst.params.count("gain") ? inst.params.at("gain") : 1.0;
    // Sensor Eq
    matrix.addB(cP, vIdxSensor, 1.0); matrix.addB(cN, vIdxSensor, -1.0);
    matrix.addC(vIdxSensor, cP, 1.0); matrix.addC(vIdxSensor, cN, -1.0);
    // Output Eq
    matrix.addB(nP, vIdxOut, 1.0); matrix.addB(nN, vIdxOut, -1.0);
    matrix.addC(vIdxOut, nP, 1.0); matrix.addC(vIdxOut, nN, -1.0);
    matrix.addD(vIdxOut, vIdxSensor, -gain);
}

void CCVSModel::stampAC(SimComplexMNAMatrix& matrix, const SimNetlist&, const SimComponentInstance& inst, double, int& vSourceCounter) {
    if (inst.nodes.size() < 4) return;
    const int nP = inst.nodes[0], nN = inst.nodes[1], cP = inst.nodes[2], cN = inst.nodes[3];
    const int vIdxSensor = vSourceCounter++;
    const int vIdxOut = vSourceCounter++;
    const std::complex<double> gain(inst.params.count("gain") ? inst.params.at("gain") : 1.0, 0.0);
    matrix.addB(cP, vIdxSensor, 1.0); matrix.addB(cN, vIdxSensor, -1.0);
    matrix.addC(vIdxSensor, cP, 1.0); matrix.addC(vIdxSensor, cN, -1.0);
    matrix.addB(nP, vIdxOut, 1.0); matrix.addB(nN, vIdxOut, -1.0);
    matrix.addC(vIdxOut, nP, 1.0); matrix.addC(vIdxOut, nN, -1.0);
    matrix.addD(vIdxOut, vIdxSensor, -gain);
}

// --- CCCS Model ---
void CCCSModel::stamp(SimMNAMatrix& matrix, const SimNetlist&, const SimComponentInstance& inst, int& vSourceCounter, double) {
    if (inst.nodes.size() < 4) return;
    int nP = inst.nodes[0], nN = inst.nodes[1], cP = inst.nodes[2], cN = inst.nodes[3];
    int vIdxSensor = vSourceCounter++;
    double gain = inst.params.count("gain") ? inst.params.at("gain") : 1.0;
    // Sensor Eq
    matrix.addB(cP, vIdxSensor, 1.0); matrix.addB(cN, vIdxSensor, -1.0);
    matrix.addC(vIdxSensor, cP, 1.0); matrix.addC(vIdxSensor, cN, -1.0);
    // Output Current
    matrix.addB(nP, vIdxSensor, gain); matrix.addB(nN, vIdxSensor, -gain);
}

void CCCSModel::stampAC(SimComplexMNAMatrix& matrix, const SimNetlist&, const SimComponentInstance& inst, double, int& vSourceCounter) {
    if (inst.nodes.size() < 4) return;
    const int nP = inst.nodes[0], nN = inst.nodes[1], cP = inst.nodes[2], cN = inst.nodes[3];
    const int vIdxSensor = vSourceCounter++;
    const std::complex<double> gain(inst.params.count("gain") ? inst.params.at("gain") : 1.0, 0.0);
    matrix.addB(cP, vIdxSensor, 1.0); matrix.addB(cN, vIdxSensor, -1.0);
    matrix.addC(vIdxSensor, cP, 1.0); matrix.addC(vIdxSensor, cN, -1.0);
    matrix.addB(nP, vIdxSensor, gain); matrix.addB(nN, vIdxSensor, -gain);
}
void LogicGateModel::stampNonlinear(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, const std::vector<double>& solution, double, int& vIdx) {
    if (inst.nodes.size() < 2) return;
    const auto& cfg = netlist.analysis();
    const double vthLow = inst.params.count("vth_low") ? inst.params.at("vth_low") : cfg.digitalThresholdLow;
    const double vthHigh = inst.params.count("vth_high") ? inst.params.at("vth_high") : cfg.digitalThresholdHigh;
    const double voutLow = inst.params.count("vout_low") ? inst.params.at("vout_low") : cfg.digitalOutputLow;
    const double voutHigh = inst.params.count("vout_high") ? inst.params.at("vout_high") : cfg.digitalOutputHigh;
    const double band = std::max(1e-9, vthHigh - vthLow);

    auto normalizedLevel = [&](int n) -> double {
        const double v = (n > 0 && n - 1 < static_cast<int>(solution.size())) ? solution[n - 1] : 0.0;
        if (v <= vthLow) return 0.0;
        if (v >= vthHigh) return 1.0;
        return (v - vthLow) / band;
    };

    const int inputCount = static_cast<int>(inst.nodes.size()) - 1;
    double logic = 0.0;
    if (inst.type == SimComponentType::LOGIC_NOT) {
        logic = 1.0 - normalizedLevel(inst.nodes[0]);
    } else if (inst.type == SimComponentType::LOGIC_AND || inst.type == SimComponentType::LOGIC_NAND) {
        logic = 1.0;
        for (int i = 0; i < inputCount; ++i) logic *= normalizedLevel(inst.nodes[static_cast<size_t>(i)]);
        if (inst.type == SimComponentType::LOGIC_NAND) logic = 1.0 - logic;
    } else if (inst.type == SimComponentType::LOGIC_OR || inst.type == SimComponentType::LOGIC_NOR) {
        double invProd = 1.0;
        for (int i = 0; i < inputCount; ++i) invProd *= (1.0 - normalizedLevel(inst.nodes[static_cast<size_t>(i)]));
        logic = 1.0 - invProd;
        if (inst.type == SimComponentType::LOGIC_NOR) logic = 1.0 - logic;
    } else if (inst.type == SimComponentType::LOGIC_XOR) {
        if (inputCount <= 0) logic = 0.0;
        else {
            logic = normalizedLevel(inst.nodes[0]);
            for (int i = 1; i < inputCount; ++i) {
                const double b = normalizedLevel(inst.nodes[static_cast<size_t>(i)]);
                logic = logic + b - 2.0 * logic * b;
            }
        }
    } else {
        logic = normalizedLevel(inst.nodes[0]);
    }

    logic = std::clamp(logic, 0.0, 1.0);
    const double outV = voutLow + (voutHigh - voutLow) * logic;
    matrix.addE(vIdx, outV);
}

// --- SimComponentFactory ---
SimComponentModel* SimComponentFactory::getModel(SimComponentType type) {
    static ResistorModel res; static CapacitorModel cap; static InductorModel ind;
    static VoltageSourceModel vsrc; static CurrentSourceModel isrc; static DiodeModel diode; static BJTModel bjt;
    static MOSFETModel mos; static OpAmpMacroModel opamp; static SwitchModel sw; static TransmissionLineModel tline;
    static BehavioralVoltageSourceModel bv; static BehavioralCurrentSourceModel bi;
    static LogicGateModel logic;
    static FluxScriptModel fluxScript;
    switch (type) {
        case SimComponentType::FluxScript: return &fluxScript;
        case SimComponentType::Resistor: return &res; case SimComponentType::Capacitor: return &cap;
        case SimComponentType::Inductor: return &ind;
        case SimComponentType::VoltageSource: return &vsrc;
        case SimComponentType::CurrentSource: return &isrc;
        case SimComponentType::Diode: return &diode; case SimComponentType::BJT_NPN: case SimComponentType::BJT_PNP: return &bjt;
        case SimComponentType::MOSFET_NMOS: case SimComponentType::MOSFET_PMOS: return &mos;
        case SimComponentType::OpAmpMacro: return &opamp;
        case SimComponentType::Switch: return &sw;
        case SimComponentType::TransmissionLine: return &tline;
        case SimComponentType::SubcircuitInstance: return nullptr; // Must be expanded by flatten().
        case SimComponentType::VCVS: { static VCVSModel vcvs; return &vcvs; }
        case SimComponentType::VCCS: { static VCCSModel vccs; return &vccs; }
        case SimComponentType::CCVS: { static CCVSModel ccvs; return &ccvs; }
        case SimComponentType::CCCS: { static CCCSModel cccs; return &cccs; }
        case SimComponentType::B_VoltageSource: return &bv; case SimComponentType::B_CurrentSource: return &bi;
        case SimComponentType::LOGIC_AND: case SimComponentType::LOGIC_OR: case SimComponentType::LOGIC_XOR:
        case SimComponentType::LOGIC_NAND: case SimComponentType::LOGIC_NOR: case SimComponentType::LOGIC_NOT: return &logic;
        default: return nullptr;
    }
}
