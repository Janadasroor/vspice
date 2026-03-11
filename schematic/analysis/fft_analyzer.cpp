// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Janada Sroor

#include "fft_analyzer.h"
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Flux {
namespace Analysis {

FftResult FftAnalyzer::compute(const std::vector<double>& time, const std::vector<double>& values) {
    FftResult res;
    if (time.size() < 2) return res;

    double t_total = time.back() - time.front();
    if (t_total <= 0) return res;

    // Use a power of 2 for Radix-2 FFT
    size_t n = 1;
    while (n < time.size()) n <<= 1;
    if (n < 1024) n = 1024; // Minimum resolution
    
    double dt = t_total / (time.size() - 1);
    double fs = 1.0 / dt;

    // Resample to uniform grid for FFT
    std::vector<double> interp_values(n, 0.0);
    for (size_t i = 0; i < n; ++i) {
        double ti = time.front() + (double)i * t_total / (n - 1);
        auto it = std::lower_bound(time.begin(), time.end(), ti);
        if (it == time.begin()) {
            interp_values[i] = values.front();
        } else if (it == time.end()) {
            interp_values[i] = values.back();
        } else {
            size_t idx = std::distance(time.begin(), it);
            double t1 = time[idx - 1], t2 = time[idx];
            double v1 = values[idx - 1], v2 = values[idx];
            interp_values[i] = v1 + (v2 - v1) * (ti - t1) / (t2 - t1);
        }
    }

    apply_hann_window(interp_values);

    std::vector<std::complex<double>> a(n);
    for (size_t i = 0; i < n; ++i) {
        a[i] = std::complex<double>(interp_values[i], 0.0);
    }

    fft(a, false);

    size_t half_n = n / 2 + 1;
    res.frequencies.resize(half_n);
    res.magnitudes.resize(half_n);
    res.phases.resize(half_n);

    for (size_t i = 0; i < half_n; ++i) {
        res.frequencies[i] = i * fs / n;
        double mag = std::abs(a[i]) * 2.0 / n; 
        res.magnitudes[i] = 20.0 * std::log10(std::max(mag, 1e-18));
        res.phases[i] = std::arg(a[i]) * 180.0 / M_PI;
    }

    return res;
}

void FftAnalyzer::fft(std::vector<std::complex<double>>& a, bool invert) {
    size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        double ang = 2 * M_PI / len * (invert ? -1 : 1);
        std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1);
            for (size_t j = 0; j < len / 2; j++) {
                std::complex<double> u = a[i+j], v = a[i+j+len/2] * w;
                a[i+j] = u + v;
                a[i+j+len/2] = u - v;
                w *= wlen;
            }
        }
    }
    if (invert) {
        for (std::complex<double>& x : a) x /= n;
    }
}

void FftAnalyzer::apply_hann_window(std::vector<double>& data) {
    size_t n = data.size();
    for (size_t i = 0; i < n; ++i) {
        double w = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (n - 1)));
        data[i] *= w;
    }
}

} // namespace Analysis
} // namespace Flux
