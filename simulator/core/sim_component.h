#ifndef SIM_COMPONENT_H
#define SIM_COMPONENT_H

#include "sim_netlist.h"
#include "sim_matrix.h"
#include "sim_expression.h"

/**
 * @brief Abstract base class for all component models in the simulator.
 */
class SimComponentModel {
public:
    virtual ~SimComponentModel() = default;

    /**
     * @brief Stamping logic for linear analysis (or linear part of nonlinear).
     * @param vSourceCounter Counter for index in B, C, D blocks if component adds equations (e.g. voltage source).
     * @param sourceFactor Scaling factor (0.0 to 1.0) for independent sources (used in source stepping).
     */
    virtual void stamp(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, 
                       int& vSourceCounter, double sourceFactor = 1.0) = 0;

    /**
     * @brief Nonlinear stamping during Newton-Raphson iteration.
     */
    virtual void stampNonlinear(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, 
                                const std::vector<double>& solution, double t, int& vSourceCounter) {}

    /**
     * @brief Stamping for transient analysis (Time-stepping).
     * @param h Current time step size.
     * @param prevSolution Solution from previous time step.
     * @param prev2Solution Solution from two steps ago.
     * @param method Integration method (Backward Euler, Trapezoidal).
     */
    virtual void stampTransient(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst,
                               double t, double h, const std::vector<double>& prevSolution, 
                               const std::vector<double>& prev2Solution,
                               SimIntegrationMethod method,
                               int& vSourceCounter) {
        // Default: just stamp normally (for time-invariant components like resistors)
        (void)t; (void)h; (void)prevSolution; (void)prev2Solution; (void)method;
        stamp(matrix, netlist, inst, vSourceCounter, 1.0);
    }

    /**
     * @brief Stamping for AC analysis (Frequency sweep).
     * @param omega Angular frequency (2 * pi * f).
     */
    virtual void stampAC(SimComplexMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst,
                        double omega, int& vSourceCounter) {
        // Default: just stamp normally (for frequency-invariant components like resistors)
    }

    virtual double calculateLTE(const SimComponentInstance& inst, double h,
                               const std::vector<double>& sol,
                               const std::vector<double>& prevSol,
                               const std::vector<double>& prev2Sol,
                               int nodes, int& vSourceCounter) { 
        (void)inst; (void)h; (void)sol; (void)prevSol; (void)prev2Sol;
        (void)nodes; (void)vSourceCounter;
        return 0.0; 
    }

    virtual bool isNonlinear() const { return false; }
    
    /**
     * @brief Allows a component to request a solver rollback if a discontinuity was detected.
     */
    virtual bool shouldRollback(const SimComponentInstance& inst, double t, double h, 
                               const std::vector<double>& currentSol, 
                               const std::vector<double>& prevSol) { 
        (void)inst; (void)t; (void)h; (void)currentSol; (void)prevSol;
        return false; 
    }

    /**
     * @brief Number of extra equations (voltage source branches) this component adds.
     */
    virtual int voltageSourceCount(const SimComponentInstance& inst) const { return 0; }
};

/**
 * @brief Registry and Factory for component models.
 */
class SimComponentFactory {
public:
    static SimComponentModel* getModel(SimComponentType type);
};

// --- Concrete Linear Models ---

class ResistorModel : public SimComponentModel {
public:
    void stamp(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, int& vSourceCounter, double sourceFactor = 1.0) override;
    void stampAC(SimComplexMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst,
                double omega, int& vSourceCounter) override;
};

class VoltageSourceModel : public SimComponentModel {
public:
    void stamp(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, int& vSourceCounter, double sourceFactor = 1.0) override;
    void stampAC(SimComplexMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst,
                double omega, int& vSourceCounter) override;
    void stampTransient(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst,
                       double t, double h, const std::vector<double>& prevSolution, 
                       const std::vector<double>& prev2Solution,
                       SimIntegrationMethod method,
                       int& vSourceCounter) override;
    int voltageSourceCount(const SimComponentInstance& inst) const override { return 1; }
};

class CurrentSourceModel : public SimComponentModel {
public:
    void stamp(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, int& vSourceCounter, double sourceFactor = 1.0) override;
    void stampAC(SimComplexMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst,
                double omega, int& vSourceCounter) override;
    void stampTransient(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst,
                       double t, double h, const std::vector<double>& prevSolution,
                       const std::vector<double>& prev2Solution,
                       SimIntegrationMethod method,
                       int& vSourceCounter) override;
};

class DiodeModel : public SimComponentModel {
public:
    void stamp(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, int& vSourceCounter, double sourceFactor = 1.0) override;
    void stampAC(SimComplexMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst,
                double omega, int& vSourceCounter) override; // Small signal
    void stampNonlinear(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, 
                        const std::vector<double>& solution, double t, int& vSourceCounter) override;
    bool isNonlinear() const override { return true; }
};

class CapacitorModel : public SimComponentModel {
public:
    void stamp(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, int& vSourceCounter, double sourceFactor = 1.0) override; 
    void stampTransient(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst,
                       double t, double h, const std::vector<double>& prevSolution, 
                       const std::vector<double>& prev2Solution,
                       SimIntegrationMethod method,
                       int& vSourceCounter) override;
    void stampAC(SimComplexMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst,
                double omega, int& vSourceCounter) override;
    double calculateLTE(const SimComponentInstance& inst, double h,
                       const std::vector<double>& sol,
                       const std::vector<double>& prevSol,
                       const std::vector<double>& prev2Sol,
                       int nodes, int& vSourceCounter) override;
};

class InductorModel : public SimComponentModel {
public:
    void stamp(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, int& vSourceCounter, double sourceFactor = 1.0) override; 
    void stampTransient(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst,
                       double t, double h, const std::vector<double>& prevSolution, 
                       const std::vector<double>& prev2Solution,
                       SimIntegrationMethod method,
                       int& vSourceCounter) override;
    void stampAC(SimComplexMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst,
                double omega, int& vSourceCounter) override;
    int voltageSourceCount(const SimComponentInstance& inst) const override { return 1; }
    double calculateLTE(const SimComponentInstance& inst, double h,
                       const std::vector<double>& sol,
                       const std::vector<double>& prevSol,
                       const std::vector<double>& prev2Sol,
                       int nodes, int& vSourceCounter) override;
};

class BJTModel : public SimComponentModel {
public:
    void stamp(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, int& vSourceCounter, double sourceFactor = 1.0) override {}
    void stampNonlinear(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, 
                        const std::vector<double>& solution, double t, int& vSourceCounter) override;
    bool isNonlinear() const override { return true; }
    void stampAC(SimComplexMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst,
                double omega, int& vSourceCounter) override;
};

class VCVSModel : public SimComponentModel {
public:
    void stamp(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, int& vSourceCounter, double sourceFactor = 1.0) override;
    void stampAC(SimComplexMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst,
                double omega, int& vSourceCounter) override;
    int voltageSourceCount(const SimComponentInstance& inst) const override { return 1; }
};

class VCCSModel : public SimComponentModel {
public:
    void stamp(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, int& vSourceCounter, double sourceFactor = 1.0) override;
    void stampAC(SimComplexMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst,
                double omega, int& vSourceCounter) override;
};

class CCVSModel : public SimComponentModel {
public:
    void stamp(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, int& vSourceCounter, double sourceFactor = 1.0) override;
    void stampAC(SimComplexMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst,
                double omega, int& vSourceCounter) override;
    int voltageSourceCount(const SimComponentInstance& inst) const override { return 2; }
};

class CCCSModel : public SimComponentModel {
public:
    void stamp(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, int& vSourceCounter, double sourceFactor = 1.0) override;
    void stampAC(SimComplexMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst,
                double omega, int& vSourceCounter) override;
    int voltageSourceCount(const SimComponentInstance& inst) const override { return 1; }
};

class MOSFETModel : public SimComponentModel {
public:
    void stamp(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, int& vSourceCounter, double sourceFactor = 1.0) override {}
    void stampNonlinear(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, 
                        const std::vector<double>& solution, double t, int& vSourceCounter) override;
    bool isNonlinear() const override { return true; }
};

class OpAmpMacroModel : public SimComponentModel {
public:
    void stamp(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, int& vSourceCounter, double sourceFactor = 1.0) override;
    void stampNonlinear(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst,
                        const std::vector<double>& solution, double t, int& vSourceCounter) override;
    int voltageSourceCount(const SimComponentInstance& inst) const override { return 1; }
    bool isNonlinear() const override { return true; }
};

class SwitchModel : public SimComponentModel {
public:
    void stamp(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, int& vSourceCounter, double sourceFactor = 1.0) override;
    void stampNonlinear(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst,
                        const std::vector<double>& solution, double t, int& vSourceCounter) override;
    bool isNonlinear() const override { return true; }
};

class TransmissionLineModel : public SimComponentModel {
public:
    void stamp(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, int& vSourceCounter, double sourceFactor = 1.0) override;
    void stampAC(SimComplexMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst,
                double omega, int& vSourceCounter) override;
};

class BehavioralVoltageSourceModel : public SimComponentModel {
public:
    void stamp(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, int& vSourceCounter, double sourceFactor = 1.0) override;
    void stampNonlinear(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, 
                        const std::vector<double>& solution, double t, int& vSourceCounter) override;
    int voltageSourceCount(const SimComponentInstance& inst) const override { return 1; }
    bool isNonlinear() const override { return true; }
};

class BehavioralCurrentSourceModel : public SimComponentModel {
public:
    void stamp(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, int& vSourceCounter, double sourceFactor = 1.0) override;
    void stampNonlinear(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, 
                        const std::vector<double>& solution, double t, int& vSourceCounter) override;
    bool isNonlinear() const override { return true; }
};

class LogicGateModel : public SimComponentModel {
public:
    void stamp(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, int& vSourceCounter, double sourceFactor = 1.0) override;
    void stampNonlinear(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, 
                        const std::vector<double>& solution, double t, int& vSourceCounter) override;
    int voltageSourceCount(const SimComponentInstance& inst) const override { return 1; }
    bool isNonlinear() const override { return true; }
};

#endif // SIM_COMPONENT_H
