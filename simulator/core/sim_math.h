#ifndef SIM_MATH_H
#define SIM_MATH_H

#include <vector>
#include <complex>
#include <cmath>

/**
 * @brief Professional DSP and Numerical helpers for simulation.
 */
class SimMath {
public:
    /**
     * @brief Performs a Fast Fourier Transform (Cooley-Tukey).
     * @param data Input time-domain samples (must be power of 2 size).
     * @return Complex frequency-domain coefficients.
     */
    static std::vector<std::complex<double>> fft(const std::vector<std::complex<double>>& data) {
        size_t n = data.size();
        if (n <= 1) return data;

        std::vector<std::complex<double>> even(n / 2);
        std::vector<std::complex<double>> odd(n / 2);
        for (size_t i = 0; i < n / 2; ++i) {
            even[i] = data[i * 2];
            odd[i] = data[i * 2 + 1];
        }

        std::vector<std::complex<double>> resEven = fft(even);
        std::vector<std::complex<double>> resOdd = fft(odd);

        std::vector<std::complex<double>> res(n);
        for (size_t k = 0; k < n / 2; ++k) {
            std::complex<double> t = std::exp(std::complex<double>(0, -2.0 * M_PI * k / n)) * resOdd[k];
            res[k] = resEven[k] + t;
            res[k + n / 2] = resEven[k] - t;
        }
        return res;
    }

    /**
     * @brief Resamples data to the nearest power of 2 for FFT processing.
     */
    static std::vector<double> resample(const std::vector<double>& x, const std::vector<double>& y, int targetSize) {
        if (y.size() < 2) return y;
        std::vector<double> result(targetSize);
        double xMin = x.front();
        double xMax = x.back();
        double step = (xMax - xMin) / (targetSize - 1);

        for (int i = 0; i < targetSize; ++i) {
            double tx = xMin + i * step;
            // Linear interpolation
            auto it = std::lower_bound(x.begin(), x.end(), tx);
            if (it == x.begin()) result[i] = y.front();
            else if (it == x.end()) result[i] = y.back();
            else {
                auto prev = std::prev(it);
                double t = (tx - *prev) / (*it - *prev);
                size_t idx = std::distance(x.begin(), it);
                result[i] = y[idx - 1] * (1.0 - t) + y[idx] * t;
            }
        }
        return result;
    }
};

#endif // SIM_MATH_H
