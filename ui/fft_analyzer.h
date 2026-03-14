// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Janada Sroor

#pragma once

#include <vector>
#include <complex>

namespace viospice {

struct FftResult {
    std::vector<double> frequencies;
    std::vector<double> magnitudes; // dBV
    std::vector<double> phases;
};

class FftAnalyzer {
public:
    static FftResult compute(const std::vector<double>& time, const std::vector<double>& values);
    
private:
    static void fft(std::vector<std::complex<double>>& a, bool invert);
    static void apply_hann_window(std::vector<double>& data);
};

} // namespace viospice
