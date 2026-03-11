#ifndef SIM_SPARSE_SOLVER_KLU_H
#define SIM_SPARSE_SOLVER_KLU_H

#include <vector>
#include <string>

/**
 * @brief High-performance Sparse LU Solver using the KLU algorithm.
 * Optimized for circuit simulation matrices which are highly sparse and
 * have a predictable structure (MNA).
 */
class SimSparseSolverKLU {
public:
    SimSparseSolverKLU();
    ~SimSparseSolverKLU();

    /**
     * @brief Setup the matrix structure (symbolic factorization).
     * Call this only when the netlist topology changes.
     */
    bool setupStructure(int n, const std::vector<int>& Ap, const std::vector<int>& Ai);

    /**
     * @brief Perform LU factorization and solve Ax = b.
     * @param Ax The non-zero values of the matrix (CSC format).
     * @param b The right-hand side vector (updated with solution x).
     */
    bool solve(const std::vector<double>& Ax, std::vector<double>& b);

    /**
     * @brief Returns the last error message if any.
     */
    std::string lastError() const { return m_error; }

    /**
     * @brief Check if KLU is supported in this build.
     */
    static bool isSupported();

private:
    void* m_symbolic = nullptr;
    void* m_numeric = nullptr;
    void* m_common = nullptr; // klu_common
    int m_n = 0;
    std::vector<int> m_Ap;
    std::vector<int> m_Ai;
    std::string m_error;
};

#endif // SIM_SPARSE_SOLVER_KLU_H
