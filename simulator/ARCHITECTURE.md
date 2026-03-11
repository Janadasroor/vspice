# VioraEDA Circuit Simulator Module — Architecture Document

> **Purpose**: This document is the single source of truth for AI agents working on the
> simulator. It describes every class, interface, data structure, algorithm, and file in
> the module. Read this FIRST before modifying any code.

## 1. Overview

The simulator is a **built-in, pure C++ SPICE-subset engine** that runs inside VioraEDA.
It does NOT depend on external ngspice or any third-party library.
It takes a **netlist** (generated from the schematic) and performs circuit analysis,
producing time-domain or frequency-domain waveform data.

### 1.1 Supported Analyses
| Analysis   | SPICE Card | Status   |
|------------|-----------|----------|
| DC Operating Point | `.op`    | Phase 1 |
| Transient  | `.tran`   | Phase 2 |
| AC Small-Signal | `.ac` | Phase 3 |
| Noise (first-cut) | `.noise` | Phase 8 |
| Distortion / THD post-process | `.four`/THD-style | Phase 8 |
| Parametric Optimization | optimization mode | Phase 8 |

### 1.2 Supported Components (Phase 1-3)
| Component | SPICE Letter | Model |
|-----------|-------------|-------|
| Resistor  | R | Linear: V = I*R |
| Capacitor | C | Charge/Discharge: I = C*dV/dt |
| Inductor  | L | Flux: V = L*dI/dt |
| Independent Voltage Source | V | DC, AC, Pulse, Sine |
| Independent Current Source | I | DC, AC, Pulse, Sine |
| Diode     | D | Shockley: I = Is*(exp(V/Vt)-1) |
| BJT (NPN/PNP) | Q | Ebers-Moll (simplified) |
| MOSFET (NMOS/PMOS) | M | Level 1 Shichman-Hodges |
| Op-amp macro | X/Device | finite-gain rail-limited macro source |
| Voltage-controlled switch | S/Device | smooth Ron/Roff transition by control voltage |
| Transmission line (quasi-static) | T/Device | characteristic-impedance coupling approximation |

### 1.3 MVP Scope Freeze (2026-02-25)

This section freezes the simulator MVP boundary used by roadmap Step 0.1.
Any change to these boundaries must update this section and `simulator/ROADMAP.md`.

#### In Scope (MVP)

- Analyses:
  - `.op` DC operating point
  - `.tran` transient
  - `.ac` small-signal AC sweep
- Core devices:
  - R, C, L
  - Independent V/I sources
  - Diode
  - BJT (NPN/PNP)
  - MOSFET (NMOS/PMOS level-1)
- Generator profiles (source waveforms):
  - `DC`
  - `SINE`
  - `PULSE`
  - `EXP`
- Integration:
  - Schematic -> netlist bridge
  - Simulation panel run/configure/probe flow
  - Basic virtual instruments (oscilloscope, logic analyzer, voltmeter, ammeter)

#### Explicitly Out of Scope (MVP)

- BSIM-level transistor models and vendor encrypted model support
- Full SPICE card parity (`.noise`, advanced RF cards, full `.lib` semantics)
- Signal-integrity and power-integrity specialized analyses
- MCU/firmware co-simulation
- Production-grade optimization engines and auto-synthesis loops

#### Ownership Boundaries (Simulator vs Schematic)

| Area | Simulator Module Owns | Schematic Module Owns |
|------|------------------------|------------------------|
| Circuit solve | MNA assembly, device stamping, convergence, analysis execution | N/A |
| Netlist input | Validation + interpretation of `SimNetlist` | Building connectivity, mapping UI items to netlist components |
| Source waveforms | Runtime evaluation of DC/SINE/PULSE/EXP | Editing source profile fields and assigning to selected source items |
| Results | Waveform/node/current result generation | Visualization, probes, overlays, user interactions |
| Errors | Numerical/parser diagnostics and error categories | Error presentation and navigation to related schematic items |

### 1.4 SPICE Parser Compatibility (2026-02-25)

`simulator/core/sim_model_parser.*` supports hardened SPICE-library parsing with:

- `.model` parsing with base-model inheritance:
  - `.model NEW BASE (PARAM=...)` inherits type/params from `BASE`, then overrides.
- `.lib ... .endl` section filtering via parse options (`activeLibSection`).
- `.include` / `.inc` expansion via caller-provided include resolver callback.
- `.param` extraction into `SimNetlist` parameter store.
- line continuations using `+` syntax.
- source-location diagnostics:
  - each parse diagnostic includes severity, line number, source name, message, and offending text.

### 1.5 Mixed-Signal Synchronization Controls (2026-02-25)

Transient mixed-signal runs include explicit analog/digital synchronization knobs in `SimAnalysisConfig`:

- Digital thresholds:
  - `digitalThresholdLow`
  - `digitalThresholdHigh`
- Digital logic output levels:
  - `digitalOutputLow`
  - `digitalOutputHigh`
- Event-step refinement around switching regions:
  - `mixedSignalEnableEventRefinement`
  - `mixedSignalEventStep`
  - `mixedSignalLogEvents`

Logic gates use threshold-window interpolation (instead of hard single-threshold switching) to reduce chatter and improve solver stability near transitions.

### 1.6 Parametric Optimization Engine (2026-02-25)

`SimAnalysisType::Optimization` provides deterministic multi-parameter search with:

- Cartesian multi-dimensional parameter sweep (`optimizationParams` list)
- weighted target objective on probe signal (`optimizationTargetSignal`, `optimizationTargetValue`)
- constraint penalties (`optimizationConstraints`)
- deterministic best-candidate selection and summary measurements:
  - `optimization_best_objective`
  - `optimization_best_<component.param>`
  - `optimization_best_target`
- optional yield summary around best candidate using seeded Monte Carlo perturbation:
  - `optimization_yield_valid_samples`
  - `optimization_yield_pass_samples`
  - `optimization_yield_ratio`

### 1.7 Convergence Assistant and Auto-Fix Guidance (2026-02-25)

`simulator/core/sim_convergence_assistant.*` provides deterministic failure diagnostics and fix suggestions.

- Inputs:
  - `SimNetlist`
  - `SimAnalysisConfig`
  - failure kind/context (`op_convergence_failure`, `transient_convergence_failure`, ...)
- Output:
  - diagnostic summary strings
  - actionable fix suggestions (grounding, floating-node checks, timestep/tolerance advice, nonlinear tuning hints)
- Integration:
  - `SimEngine` attaches assistant output to `SimResults.diagnostics` and `SimResults.fixSuggestions` on convergence-related failures.
  - `SimManager` emits first suggestion as a log hint when OP/transient run fails.

### 1.8 Advanced Probing and Equation Channels (2026-02-25)

`SimEngine` now supports derived probe/equation channels driven by `SimNetlist::autoProbes()`.

- Supported math channel forms:
  - direct arithmetic expressions over signals (for example: `V(OUT)-V(IN)`, `V(OUT)/V(IN)`)
  - derivative wrappers: `D(expr)`, `DDT(expr)`, `DERIV(expr)`
- Signal references are resolved against existing waveform names (case-insensitive, whitespace-insensitive).
- Equation channels are appended to `SimResults.waveforms` with the user expression as the waveform name.
- Invalid expressions do not fail the run; they are reported through `SimResults.diagnostics` as skipped probe expressions.

### 1.9 Generator Designer and Waveform Library (2026-02-25)

`schematic/ui/simulation_panel.*` now includes a generator workflow that avoids manual waveform-string editing:

- Visual PWL editor dialog with point-table editing.
- PWL CSV import/export (`time,value`) for waveform round-tripping.
- Shared built-in waveform templates (DC/SIN/PULSE/PWL/FM examples).
- User preset save/load/delete with persistent storage in `generator_presets.json`
  under project directory (or app data fallback).

### 1.10 Large-Circuit Performance Path (2026-02-25)

The simulation core includes performance-focused execution updates for larger circuits:

- Matrix memory reuse in hot loops:
  - Newton-Raphson (`solveNR`) reuses one `SimMNAMatrix` across iterations.
  - Transient solve loop reuses matrix storage across timestep NR iterations.
  - AC sweep reuses one complex matrix across frequency points.
- Dense matrix solve avoids per-solve full matrix/RHS copies by solving in-place on the working matrix.
- Parametric sweep workers reuse a thread-local `SimEngine` instance across sweep points.
- Transient waveform vectors reserve storage upfront based on step/cap settings to reduce realloc pressure.

### 1.11 Cross-Simulator Verification Framework (2026-02-25)

Nightly cross-simulator drift reporting is integrated through scripts and CI workflow hooks:

- `scripts/cross_verify_simulators.py`
  - compares per-deck/per-check native results against reference results (or expected targets fallback)
  - emits JSON + Markdown reports with tolerance-aware drift status
  - updates rolling history (`cross_simulator_verification_history.json`) for trend tracking
- `scripts/run_cross_simulator_verification.sh`
  - CI-oriented wrapper for paths, fallback mode, and fail-on-drift behavior
- `.github/workflows/ci.yml` scheduled performance job:
  - runs cross-verification step
  - uploads drift report + history artifacts

### 1.12 Post-Processing and Export Workflows (2026-02-25)

`schematic/ui/simulation_panel.*` now includes reproducible post-processing/export paths:

- Waveform comparison overlay:
  - optional "Overlay Previous" trace layer (`Prev: <signal>`) for current-vs-last-run visual comparison.
- Structured exports:
  - waveform CSV export (`<signal>_x`, `<signal>_y` columns)
  - full results JSON export (schema metadata, waveforms, measurements, diagnostics)
- Report template export:
  - Markdown report with run metadata, measurement section, waveform summary, and diagnostics/suggestions.

### 1.13 Schematic-Native Simulation UX Completion (2026-02-25)

Simulation debug workflows are now executable directly from schematic canvas interactions:

- `SimulationPanel` can request immediate canvas tool activation for:
  - pin/net probing
  - oscilloscope instrument placement
  - voltmeter placement
- Simulation issue list inside panel:
  - parses diagnostic targets (component/net) from simulator log messages
  - double-click navigation emits target events back to editor
- `SchematicEditor` bridges these panel requests to:
  - `SchematicView` tool switching
  - probe/instrument signal wiring
  - `navigateToSimulationTarget(...)` highlighting/centering behavior

## 2. Module File Map

```
simulator/
├── ARCHITECTURE.md          ← This file (READ FIRST)
├── TROUBLESHOOTING.md       ← Failure taxonomy and operator playbook
├── BETA_READINESS_CHECKLIST.md ← Beta go/no-go checklist and signoff sheet
├── component-model-mapping.md ← Schematic->sim mapping contract
├── pin-order-normalization.md ← pin role ordering and alias normalization
├── hierarchical-flattening.md ← hierarchy->primitive flattening contract
├── CMakeLists.txt           ← Build config for static library
│
├── core/                    ← Pure math engine (NO Qt dependency)
│   ├── sim_netlist.h/cpp    ← Netlist data structures
│   ├── sim_component.h/cpp  ← Component models (R, C, L, D, Q, M, V, I)
│   ├── sim_matrix.h/cpp     ← Sparse MNA matrix solver
│   ├── sim_engine.h/cpp     ← Top-level simulation engine
│   ├── sim_analysis.h/cpp   ← Analysis types (.op, .tran, .ac)
│   └── sim_results.h/cpp    ← Waveform result storage
│
├── bridge/                  ← Integration with VioraEDA (Qt-dependent)
│   ├── sim_schematic_bridge.h/cpp  ← Schematic scene → Netlist converter
│   └── sim_manager.h/cpp    ← QObject wrapper, signals/slots
│
└── tests/                   ← Self-contained test circuits
    ├── test_dc_op.cpp        ← Tests: V divider, series/parallel R
    ├── test_transient.cpp    ← Tests: RC charge/discharge
    └── test_circuits.h       ← Known-good reference circuits
```

## 3. Core Data Structures

### 3.1 SimNetlist (sim_netlist.h)
Represents a parsed, flattened circuit. This is the ONLY input to the engine.

```cpp
struct SimNode {
    int id;              // 0 = ground, 1..N = internal nodes
    std::string name;    // Human-readable: "Net1", "VCC", "GND"
};

struct SimComponentInstance {
    std::string name;        // "R1", "C2", "V1"
    SimComponentType type; // Resistor, Capacitor, etc.
    std::vector<int> nodes;  // Connected node IDs (ordered by pin)
    std::map<std::string, double> params; // "resistance"=1000, "capacitance"=1e-6
};

struct SimAnalysisConfig {
    SimAnalysisType type; // OP, Transient, AC
    double tStart, tStop, tStep;         // Transient params
    double fStart, fStop; int fPoints;   // AC params
};

class SimNetlist {
public:
    int addNode(const std::string& name);     // Returns node ID
    int groundNode() const;               // Always 0
    void addComponent(const SimComponentInstance& comp);
    void setAnalysis(const SimAnalysisConfig& config);

    int nodeCount() const;
    const std::vector<SimComponentInstance>& components() const;
    const SimAnalysisConfig& analysis() const;
    int findNode(const std::string& name) const;  // -1 if not found
};
```

### 3.2 SimComponent Models (sim_component.h)
Each component knows how to "stamp" itself into the MNA matrix.

```cpp
// Component type enumeration
enum class SimComponentType {
    Resistor, Capacitor, Inductor,
    VoltageSource, CurrentSource,
    Diode, BJT_NPN, BJT_PNP,
    MOSFET_NMOS, MOSFET_PMOS
};

// Abstract base for all component models
class SimComponentModel {
public:
    virtual ~SimComponentModel() = default;

    // LINEAR stamp: called once for linear analysis
    virtual void stamp(SimMNAMatrix& matrix, const SimComponentInstance& inst) = 0;

    // NONLINEAR stamp: called each Newton-Raphson iteration
    virtual void stampNonlinear(SimMNAMatrix& matrix,
                                const SimComponentInstance& inst,
                                const std::vector<double>& solution) {}

    // Does this component need Newton-Raphson iteration?
    virtual bool isNonlinear() const { return false; }
};
```

### 3.3 MNA Matrix (sim_matrix.h)
Modified Nodal Analysis matrix. Size = (N + M) where N = nodes, M = voltage sources.

```
    | G   B | | v |   | i |
    | C   D | | j | = | e |
    
    G = conductance matrix (NxN)
    B = voltage source coupling (NxM)
    C = B^T (MxN)
    D = zero (MxM, for ideal sources)
    v = node voltages
    j = voltage source currents
    i = current excitation vector
    e = voltage source values
```

```cpp
class SimMNAMatrix {
public:
    void resize(int nodes, int voltageSources);
    void clear();  // Zero all entries

    // Stamp operations (the ONLY way to modify the matrix)
    void stampConductance(int n1, int n2, double g);   // Resistor
    void stampVoltageSource(int n_pos, int n_neg, int vsIndex, double voltage);
    void stampCurrentSource(int n_pos, int n_neg, double current);

    // For nonlinear: stamp companion model (Norton equivalent)
    void stampCompanion(int n1, int n2, double geq, double ieq);

    // Solve Ax = b, returns solution vector [v1..vN, j1..jM]
    std::vector<double> solve();

    int size() const;
};
```

### 3.4 SimResults (sim_results.h)
```cpp
struct SimWaveform {
    std::string name;           // "V(Net1)", "I(R1)"
    std::vector<double> xData;  // Time or frequency points
    std::vector<double> yData;  // Voltage/current values
};

class SimResults {
public:
    void addWaveform(const SimWaveform& wf);
    const std::vector<SimWaveform>& waveforms() const;
    SimWaveform findWaveform(const std::string& name) const;

    // DC OP results
    std::map<std::string, double> nodeVoltages;
    std::map<std::string, double> branchCurrents;
};
```

## 4. Algorithms

### 4.1 DC Operating Point (.op)
```
1. Build SimNetlist from schematic
2. Create MNA matrix of size (N + M)
3. For each component: call stamp() to fill matrix
4. If circuit has nonlinear components:
   a. Initial guess: all voltages = 0
   b. REPEAT (Newton-Raphson):
      i.   Clear matrix
      ii.  stamp() all linear components
      iii. stampNonlinear() all nonlinear components using current solution
      iv.  Solve matrix → new solution
      v.   Check convergence: |v_new - v_old| < ε for all nodes
   c. UNTIL converged OR max iterations reached
5. Otherwise: solve matrix once
6. Extract node voltages and branch currents → SimResults
```

### 4.2 Transient Analysis (.tran)
```
1. Compute DC operating point (initial conditions)
2. FOR t = tStart to tStop, step tStep:
   a. For each dynamic component (C, L):
      - Convert to companion model using integration method:
        - Backward Euler: geq = C/h, ieq = C/h * v_prev
        - Trapezoidal: geq = 2C/h, ieq = 2C/h * v_prev + i_prev
   b. Build and solve MNA matrix (with Newton-Raphson if nonlinear)
   c. Store solution point in SimResults
3. Return waveforms
```

### 4.3 Component Stamp Reference

**Resistor R between nodes n1 and n2, value = R:**
```
G[n1][n1] += 1/R
G[n1][n2] -= 1/R
G[n2][n1] -= 1/R
G[n2][n2] += 1/R
```

**Voltage Source V between n+ and n-, value = Vs, index = k:**
```
B[n+][k] += 1
B[n-][k] -= 1
C[k][n+] += 1
C[k][n-] -= 1
e[k] = Vs
```

**Current Source I from n+ to n-, value = Is:**
```
i[n+] -= Is  (current leaves n+)
i[n-] += Is  (current enters n-)
```

**Capacitor companion (Backward Euler), C between n1, n2, h = timestep:**
```
geq = C / h
ieq = geq * v_prev(n1 - n2)
Stamp as: conductance geq between n1,n2 + current source ieq
```

**Inductor companion (Backward Euler), L between n1, n2, h = timestep:**
```
geq = h / L
ieq = i_prev + geq * v_prev(n1 - n2)
Stamp as: conductance (1/L)*h ... (use voltage source formulation)
```

**Diode (Newton-Raphson linearization):**
```
Id = Is * (exp(Vd / Vt) - 1)
gd = Is / Vt * exp(Vd / Vt)   // conductance at operating point
ieq = Id - gd * Vd             // equivalent current
Stamp: conductance gd between anode,cathode + current source ieq
```

## 5. Integration with VioraEDA

### 5.1 Bridge: Schematic → Netlist (sim_schematic_bridge.h)
```cpp
class SimSchematicBridge {
public:
    // Extract netlist from the current schematic scene
    static SimNetlist buildNetlist(QGraphicsScene* scene, NetManager* netManager);

private:
    // Map SchematicItem types to SimComponentType
    static SimComponentType mapItemType(int schematicItemType);

    // Parse component value string: "10k" → 10000.0, "4.7u" → 4.7e-6
    static double parseValue(const QString& valueStr);
};
```

### 5.2 Manager: Qt Wrapper (sim_manager.h)
```cpp
class SimManager : public QObject {
    Q_OBJECT
public:
    static SimManager& instance();
    bool isAvailable() const { return true; } // Always available (built-in)

    void runDCOP(QGraphicsScene* scene, NetManager* netMgr);
    void runTransient(QGraphicsScene* scene, NetManager* netMgr,
                      double tStop, double tStep);
    void runAC(QGraphicsScene* scene, NetManager* netMgr,
               double fStart, double fStop, int points);
    void stop();

signals:
    void simulationStarted();
    void simulationProgress(int percent);
    void simulationFinished(const SimResults& results);
    void errorOccurred(const QString& msg);
    void logMessage(const QString& msg);
};
```

## 6. Phase Plan

### Phase 1: DC Operating Point
- [x] SimNetlist data structures
- [x] SimMNAMatrix with Gaussian elimination
- [x] Resistor stamp
- [x] Voltage source stamp
- [x] Current source stamp
- [x] SimEngine::solveDCOP()
- [x] SimSchematicBridge
- [x] SimManager Qt wrapper
- [x] Test: voltage divider (2 resistors + 1 voltage source)

### Phase 2: Transient Analysis
- [x] Capacitor companion model (Backward Euler)
- [x] Inductor companion model (Backward Euler)
- [x] Time-stepping loop
- [x] Test: RC charging circuit

### Phase 3: Nonlinear Components
- [x] Newton-Raphson iteration loop
- [x] Diode model (Shockley)
- [ ] BJT model (Ebers-Moll simplified)
- [ ] MOSFET Level 1
- [ ] Convergence control

### Phase 4: AC Analysis
- [ ] Complex MNA matrix
- [ ] Frequency sweep
- [ ] Bode plot data generation

## 7. Conventions for AI Agents

1. **All core/ files must have ZERO Qt dependencies**. Use `std::vector`, `std::string`,
   `std::map` in core/. Only bridge/ and the tests may use Qt.
2. **Every public function must have a doc comment** explaining inputs, outputs, and edge cases.
3. **Every stamp function must reference this document** (Section 4.3) so behavior is traceable.
4. **Test-driven**: Write the test FIRST, then implement the feature.
5. **No external dependencies**: No ngspice, no Eigen, no BLAS. Pure C++ STL + our own solver.
6. **Error handling**: Never crash. Return error strings through SimResults or SimManager signals.
7. **Numerical stability**: Use partial pivoting in the matrix solver. Guard against division by zero.
