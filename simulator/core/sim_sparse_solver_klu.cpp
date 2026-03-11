#include "sim_sparse_solver_klu.h"
#include <iostream>

#ifdef FLUX_HAS_KLU
#include <klu.h>
#endif

SimSparseSolverKLU::SimSparseSolverKLU() {
#ifdef FLUX_HAS_KLU
    m_common = new klu_common();
    klu_defaults((klu_common*)m_common);
#endif
}

SimSparseSolverKLU::~SimSparseSolverKLU() {
#ifdef FLUX_HAS_KLU
    if (m_symbolic) klu_free_symbolic((klu_symbolic**)&m_symbolic, (klu_common*)m_common);
    if (m_numeric) klu_free_numeric((klu_numeric**)&m_numeric, (klu_common*)m_common);
    delete (klu_common*)m_common;
#endif
}

bool SimSparseSolverKLU::isSupported() {
#ifdef FLUX_HAS_KLU
    return true;
#else
    return false;
#endif
}

bool SimSparseSolverKLU::setupStructure(int n, const std::vector<int>& Ap, const std::vector<int>& Ai) {
    m_n = n;
    m_Ap = Ap;
    m_Ai = Ai;
#ifdef FLUX_HAS_KLU
    if (m_symbolic) klu_free_symbolic((klu_symbolic**)&m_symbolic, (klu_common*)m_common);
    
    m_symbolic = klu_analyze(n, (int*)m_Ap.data(), (int*)m_Ai.data(), (klu_common*)m_common);
    
    if (!m_symbolic) {
        m_error = "KLU Analysis failed";
        return false;
    }
    return true;
#else
    m_error = "KLU Solver not supported.";
    return false;
#endif
}

bool SimSparseSolverKLU::solve(const std::vector<double>& Ax, std::vector<double>& b) {
#ifdef FLUX_HAS_KLU
    if (!m_symbolic) {
        m_error = "Symbolic factorization missing.";
        return false;
    }

    // Numerical factorization
    if (m_numeric) klu_free_numeric((klu_numeric**)&m_numeric, (klu_common*)m_common);
    
    m_numeric = klu_factor((int*)m_Ap.data(), (int*)m_Ai.data(), (double*)Ax.data(), 
                           (klu_symbolic*)m_symbolic, (klu_common*)m_common);
    
    if (!m_numeric) {
        m_error = "KLU Factorization failed (singular matrix?)";
        return false;
    }

    // Solve Ax = b (b is updated in place)
    klu_tsolve((klu_symbolic*)m_symbolic, (klu_numeric*)m_numeric, m_n, 1, b.data(), (klu_common*)m_common);
    
    return true;
#else
    m_error = "KLU Solver not supported.";
    return false;
#endif
}
