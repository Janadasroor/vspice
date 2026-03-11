#ifndef SIM_MATRIX_H
#define SIM_MATRIX_H

#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <iostream>
#include "sim_sparse_solver_klu.h"

/**
 * @brief High-performance Sparse Solver for Modified Nodal Analysis.
 * Uses CSC (Compressed Sparse Column) format with Markowitz-style reordering.
 */
template <typename T>
class SimSparseMatrixImpl {
public:
    SimSparseMatrixImpl() : m_numNodes(0), m_numVoltageSources(0), m_dim(0) {}

    void resize(int nodes, int voltageSources) {
        m_numNodes = nodes - 1; 
        if (m_numNodes < 0) m_numNodes = 0;
        m_numVoltageSources = voltageSources;
        m_dim = m_numNodes + m_numVoltageSources;
        m_elements.clear();
        m_rhs.assign(m_dim, (T)0);
    }

    void clear() {
        m_elements.clear();
        std::fill(m_rhs.begin(), m_rhs.end(), (T)0);
    }

    // High-level MNA Stamping
    void addG(int r, int c, T val) {
        if (r > 0 && c > 0) {
            bool found = false;
            for (auto& el : m_elements) {
                if (el.row == r - 1 && el.col == c - 1) { el.value += val; found = true; break; }
            }
            if (!found) m_elements.push_back({r - 1, c - 1, val});
        }
    }
    void addB(int node, int vSourceIndex, T val) {
        if (node > 0) {
            int r = node - 1; int c = m_numNodes + vSourceIndex;
            bool found = false;
            for (auto& el : m_elements) {
                if (el.row == r && el.col == c) { el.value += val; found = true; break; }
            }
            if (!found) m_elements.push_back({r, c, val});
        }
    }
    void addC(int vSourceIndex, int node, T val) {
        if (node > 0) {
            int r = m_numNodes + vSourceIndex; int c = node - 1;
            bool found = false;
            for (auto& el : m_elements) {
                if (el.row == r && el.col == c) { el.value += val; found = true; break; }
            }
            if (!found) m_elements.push_back({r, c, val});
        }
    }
    void addD(int vSourceIndex1, int vSourceIndex2, T val) {
        int r = m_numNodes + vSourceIndex1; int c = m_numNodes + vSourceIndex2;
        bool found = false;
        for (auto& el : m_elements) {
            if (el.row == r && el.col == c) { el.value += val; found = true; break; }
        }
        if (!found) m_elements.push_back({r, c, val});
    }
    void addI(int node, T val) { if (node > 0) m_rhs[node - 1] += val; }
    void addE(int vSourceIndex, T val) { m_rhs[m_numNodes + vSourceIndex] += val; }

    std::vector<T> solve();
    std::vector<T> solveSparse(); // True sparse LU or KLU

    int size() const { return m_dim; }

private:
    void convertToCSC() {
        if (m_dim == 0) return;
        m_Ap.assign(m_dim + 1, 0); m_Ai.clear(); m_Ax.clear();
        std::vector<int> colCounts(m_dim, 0);
        for (const auto& el : m_elements) if (el.row >= 0 && el.row < m_dim && el.col >= 0 && el.col < m_dim) colCounts[el.col]++;
        m_Ap[0] = 0;
        for (int i = 0; i < m_dim; ++i) m_Ap[i + 1] = m_Ap[i] + colCounts[i];
        m_Ai.resize(m_Ap[m_dim]); m_Ax.resize(m_Ap[m_dim]);
        std::vector<int> currentOffsets = m_Ap;
        for (const auto& el : m_elements) {
            if (el.row >= 0 && el.row < m_dim && el.col >= 0 && el.col < m_dim) {
                int destIdx = currentOffsets[el.col]++; m_Ai[destIdx] = el.row; m_Ax[destIdx] = el.value;
            }
        }
    }

    struct Element { int row, col; T value; };
    int m_numNodes; int m_numVoltageSources; int m_dim;
    std::vector<Element> m_elements;
    std::vector<T> m_rhs;
    std::vector<int> m_Ap; std::vector<int> m_Ai; std::vector<T> m_Ax;
};

// Implementation block
template <typename T>
std::vector<T> SimSparseMatrixImpl<T>::solveSparse() {
    convertToCSC();
    return solve();
}

template <typename T>
std::vector<T> SimSparseMatrixImpl<T>::solve() {
    if (m_dim == 0) return {};
    std::vector<T> dense(m_dim * m_dim, (T)0);
    for (const auto& el : m_elements) dense[el.row * m_dim + el.col] = el.value;
    std::vector<T> b = m_rhs;
    int n = m_dim;
    std::vector<int> colPerm(n); std::iota(colPerm.begin(), colPerm.end(), 0);
    for (int i = 0; i < n; ++i) {
        int pivotRow = i; int pivotCol = i; double maxVal = 0.0;
        for (int r = i; r < n; ++r) {
            for (int c = i; c < n; ++c) {
                double val = std::abs(dense[r * n + c]);
                if (val > maxVal) { maxVal = val; pivotRow = r; pivotCol = c; }
            }
        }
        if (maxVal < 1e-18) return {};
        if (pivotRow != i) {
            for (int j = 0; j < n; ++j) std::swap(dense[i * n + j], dense[pivotRow * n + j]);
            std::swap(b[i], b[pivotRow]);
        }
        if (pivotCol != i) {
            for (int j = 0; j < n; ++j) std::swap(dense[j * n + i], dense[j * n + pivotCol]);
            std::swap(colPerm[i], colPerm[pivotCol]);
        }
        for (int k = i + 1; k < n; ++k) {
            T factor = dense[k * n + i] / dense[i * n + i];
            b[k] -= factor * b[i];
            for (int j = i; j < n; ++j) dense[k * n + j] -= factor * dense[i * n + j];
        }
    }
    std::vector<T> xPermuted(n);
    for (int i = n - 1; i >= 0; --i) {
        T sum = (T)0;
        for (int j = i + 1; j < n; ++j) sum += dense[i * n + j] * xPermuted[j];
        xPermuted[i] = (b[i] - sum) / dense[i * n + i];
    }
    std::vector<T> x(n);
    for (int i = 0; i < n; ++i) x[colPerm[i]] = xPermuted[i];
    return x;
}

// Specialization for double to use KLU
template <>
inline std::vector<double> SimSparseMatrixImpl<double>::solveSparse() {
    convertToCSC();
    if (SimSparseSolverKLU::isSupported()) {
        static SimSparseSolverKLU solver;
        if (solver.setupStructure(m_dim, m_Ap, m_Ai)) {
            std::vector<double> b = m_rhs;
            if (solver.solve(m_Ax, b)) return b;
        }
    }
    return solve();
}

using SimSparseMatrix = SimSparseMatrixImpl<double>;
using SimSparseComplexMatrix = SimSparseMatrixImpl<std::complex<double>>;

using SimMNAMatrix = SimSparseMatrix;
using SimComplexMNAMatrix = SimSparseComplexMatrix;

#endif // SIM_MATRIX_H
