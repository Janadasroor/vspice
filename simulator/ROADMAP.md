# VioraEDA Simulator Full Platform Plan

This is the implementation roadmap to make the simulator reliable for real circuits and tightly integrated with schematic editing.

Last updated: 2026-02-25

## Status Legend

- `Pending`
- `In Progress`
- `Completed`
- `Blocked`

## Success Criteria (Definition of Done)

- At least 90% of reference analog test circuits pass numerical checks against known-good data.
- Typical transient and AC analyses complete without crash/hang on medium schematics (1k components).
- Schematic simulation UX supports setup, run, probe, and error navigation in one flow.
- Wide virtual instrument coverage exists in schematic (oscilloscope, voltmeter, ammeter, and related lab tools).
- Source generator coverage exists in schematic simulation setup (DC, SIN, PULSE, EXP, SFFM, PWL, and AM/FM profiles).
- CI runs simulator unit tests + integration tests + regression deck tests on every PR.
- Performance budget and memory budget are enforced in CI for simulator workloads.

## Plan Overview

- Total steps: 54
- Completed steps: 54
- In Progress steps: 0
- Pending steps: 0

---

## Phase 0 - Baseline, Scope, and Test Harness

- [ ] Step 0.1: Freeze MVP simulator scope (DC OP, TRAN, AC, supported devices) - **Status: Completed**
  - Deliverables: `simulator/ARCHITECTURE.md` scope table updated.
  - Acceptance: no ambiguous feature ownership between simulator and schematic.

- [ ] Step 0.2: Build reference circuit deck suite - **Status: Completed**
  - Deliverables: `simulator/tests/reference_decks/` with expected outputs.
  - Acceptance: at least 30 decks across linear, nonlinear, and dynamic cases.

- [ ] Step 0.3: Add golden-result comparator utility - **Status: Completed**
  - Deliverables: test utility for waveform/value tolerance checks.
  - Acceptance: CI fails when numeric drift exceeds thresholds.

---

## Phase 1 - Netlist Fidelity from Schematic (Critical Path)

- [ ] Step 1.1: Unify connectivity extraction for simulation - **Status: Completed**
  - Deliverables: strengthen `schematic/analysis/schematic_connectivity.cpp` and bridge usage.
  - Acceptance: no net naming mismatch between ERC/connectivity and simulation netlist.

- [ ] Step 1.2: Canonical SPICE value/unit parsing - **Status: Completed**
  - Deliverables: shared parser for `k m u n p meg` suffixes and scientific notation.
  - Acceptance: parser tests cover malformed/edge values and locale-safe handling.

- [ ] Step 1.3: Component-to-model mapping contract - **Status: Completed**
  - Deliverables: explicit mapping rules for `schematic/items/*` to simulator types.
  - Acceptance: unsupported parts are reported with deterministic actionable errors.

- [ ] Step 1.4: Simulation pin-order normalization - **Status: Completed**
  - Deliverables: pin-order registry for polarized and multi-pin devices.
  - Acceptance: polarity-sensitive circuits (diodes, BJTs, MOSFETs) stamp correctly.

- [ ] Step 1.5: Hierarchical sheet flattening rules - **Status: Completed**
  - Deliverables: flattening logic from schematic hierarchy into simulator netlist.
  - Acceptance: hierarchical test decks produce same results as flattened equivalents.

---

## Phase 2 - Solver Robustness and Numerical Stability

- [ ] Step 2.1: Deterministic MNA assembly and ordering - **Status: Completed**
  - Deliverables: stable node/source indexing in `simulator/core/sim_matrix.cpp`.
  - Acceptance: repeated runs generate identical matrix topology and results.

- [ ] Step 2.2: Pivoting and singular-matrix diagnostics - **Status: Completed**
  - Deliverables: improved pivot strategy + singularity root-cause messages.
  - Acceptance: floating nodes/shorted sources produce clear error guidance.

- [ ] Step 2.3: Newton-Raphson convergence guards - **Status: Completed**
  - Deliverables: iteration caps, damping/limiting, residual logging.
  - Acceptance: nonlinear failures provide reproducible diagnostics in logs.

- [ ] Step 2.4: Gmin/source stepping fallback path - **Status: Completed**
  - Deliverables: optional stepped homotopy for hard DC operating points.
  - Acceptance: known hard decks converge more often without false positives.

- [ ] Step 2.5: Adaptive timestep control for transient - **Status: Completed**
  - Deliverables: LTE-based step control with min/max bounds.
  - Acceptance: transient runtime improves while preserving waveform tolerance.

---

## Phase 3 - Device Model Correctness

- [ ] Step 3.1: Passive model audit (R, C, L) - **Status: Completed**
  - Deliverables: verified stamps and transient companion model checks.
  - Acceptance: RC/RL/RLC references pass tolerance targets.

- [ ] Step 3.2: Source model audit (DC/AC/PULSE/SINE) - **Status: Completed**
  - Deliverables: source waveform evaluator with unit-tested edge behavior.
  - Acceptance: time-domain source waveforms match expected vectors.

- [ ] Step 3.3: Diode model stabilization - **Status: Completed**
  - Deliverables: robust exponential limiting and temperature-ready parameters.
  - Acceptance: rectifier decks converge and match expected DC/transient outputs.

- [ ] Step 3.4: BJT and MOSFET model hardening - **Status: Completed**
  - Deliverables: parameter validation + safer defaults + convergence helpers.
  - Acceptance: common amplifier/inverter decks pass and remain stable.

- [ ] Step 3.5: Initial condition and operating-point handoff - **Status: Completed**
  - Deliverables: consistent `.op -> .tran` initialization pipeline.
  - Acceptance: startup transients are deterministic and physically plausible.

---

## Phase 4 - Schematic Integration and UX Completion

- [ ] Step 4.1: Simulation setup schema + persistence - **Status: Completed**
  - Deliverables: durable analysis config in schematic documents.
  - Files: `schematic/ui/simulation_setup_dialog.*`, `schematic/io/schematic_file_io.*`
  - Acceptance: saved project reloads all simulation settings exactly.

- [ ] Step 4.2: One-click run from schematic editor - **Status: Completed**
  - Deliverables: run actions and state handling in editor toolbar/menus.
  - Files: `schematic/editor/schematic_editor_ui.cpp`, `schematic/editor/schematic_editor.h`, `simulator/bridge/sim_manager.*`
  - Acceptance: user can run/cancel and see progress/errors without manual plumbing.

- [ ] Step 4.3: Probe workflow cleanup - **Status: Completed**
  - Deliverables: reliable probe add/remove/select flows.
  - Files: `schematic/tools/schematic_probe_tool.*`, `schematic/ui/simulation_panel.*`, `schematic/editor/schematic_editor.cpp`
  - Acceptance: probe placement and waveform assignment are predictable and undo-safe.

- [ ] Step 4.4: Error-to-schematic navigation - **Status: Completed**
  - Deliverables: clickable errors linked to offending component/net.
  - Files: `simulator/bridge/sim_schematic_bridge.*`, `schematic/editor/schematic_editor_ui.cpp`
  - Acceptance: each simulation error has a direct schematic highlight path.

- [ ] Step 4.5: Operating-point overlays and result markers - **Status: Completed**
  - Deliverables: voltage/current overlays with visibility controls.
  - Files: `schematic/editor/schematic_editor.cpp`, `schematic/ui/simulation_window.*`, `schematic/editor/schematic_editor_ui.cpp`
  - Acceptance: overlays are fast, legible, and can be toggled/reset reliably.

---

## Phase 5 - Waveform and Virtual Instrument Layer

- [ ] Step 5.1: Waveform data contract versioning - **Status: Completed**
  - Deliverables: stable result schema between core engine and UI views.
  - Files: `simulator/core/sim_engine.h`, `simulator/core/sim_engine.cpp`, `schematic/ui/simulation_panel.cpp`
  - Acceptance: waveform UI can evolve without breaking engine integration.

- [ ] Step 5.2: Plot renderer performance optimization - **Status: Completed**
  - Deliverables: decimation/streaming for large transient result sets.
  - Files: `schematic/ui/simulation_panel.cpp`
  - Acceptance: 1M+ points remain interactable on target hardware.

- [ ] Step 5.3: Measurement tools (cursor, delta, RMS, FFT baseline) - **Status: Completed**
  - Deliverables: core measurement functions in simulation window.
  - Files: `schematic/ui/simulation_panel.h`, `schematic/ui/simulation_panel.cpp`
  - Acceptance: measurements reproduce expected values on regression waveforms.

- [ ] Step 5.4: Virtual instrument stability pass - **Status: Completed**
  - Deliverables: improve `schematic/ui/virtual_instruments.*` lifecycle and refresh model.
  - Files: `schematic/ui/virtual_instruments.*`, `schematic/ui/simulation_panel.cpp`, `schematic/ui/instrument_window.cpp`
  - Acceptance: no stale views, no crash on rapid reruns/reconfiguration.

- [ ] Step 5.5: Expand schematic virtual instrument catalog - **Status: Completed**
  - Deliverables: add instrument models and UI wiring for voltmeter, ammeter, wattmeter, frequency counter, and logic probe.
  - Files: `schematic/ui/virtual_instruments.*`, `schematic/items/*`, `schematic/tools/*`
  - Acceptance: instruments can be placed/attached in schematic and produce correct readings during simulation.

- [ ] Step 5.6: Instrument accuracy and calibration tests - **Status: Completed**
  - Deliverables: reference tests validating instrument readings against simulator ground truth.
  - Acceptance: instrument readings stay within defined tolerance across DC, transient, and AC scenarios.

- [ ] Step 5.7: Add source generator section and profile coverage - **Status: Completed**
  - Deliverables: generator UI for DC/SIN/PULSE/EXP/SFFM/PWL/AM/FM with source-assignment workflow in schematic simulation panel.
  - Files: `schematic/ui/simulation_panel.*`, `simulator/bridge/sim_schematic_bridge.*`, `simulator/core/sim_component.*`
  - Acceptance: selected source components can be configured with generator profiles and produce expected transient behavior.

---

## Phase 6 - Performance and Resource Control

- [ ] Step 6.1: Simulator profiling harness - **Status: Completed**
  - Deliverables: reproducible benchmark runner for representative circuits.
  - Acceptance: per-analysis CPU/memory metrics exported in CI artifacts.

- [ ] Step 6.2: Memory growth controls for long transients - **Status: Completed**
  - Deliverables: ring buffers/decimated storage mode options.
  - Acceptance: long runs avoid unbounded RAM growth.

- [ ] Step 6.3: Parallel sweep execution strategy - **Status: Completed**
  - Deliverables: controlled parallelism for sweep jobs.
  - Acceptance: faster sweeps without UI deadlocks or nondeterministic outputs.

- [ ] Step 6.4: CI performance budgets for simulator - **Status: Completed**
  - Deliverables: budget check script integrated into workflow.
  - Acceptance: regressions fail CI with actionable delta output.

---

## Phase 7 - Quality Gates and Release Readiness

- [ ] Step 7.1: Expand simulator unit test matrix - **Status: Completed**
  - Deliverables: dedicated tests for parser, models, matrix solver, convergence logic.
  - Acceptance: broad branch coverage for `simulator/core/*`.

- [ ] Step 7.2: Add schematic-simulator integration tests - **Status: Completed**
  - Deliverables: end-to-end tests from schematic scene to final waveforms.
  - Acceptance: catches bridge/UX regressions before merge.

- [ ] Step 7.3: Add fuzz tests for netlist/model parsing - **Status: Completed**
  - Deliverables: parser fuzz harness (invalid cards, malformed params, extremes).
  - Acceptance: no crashes, no hangs, bounded error handling.

- [ ] Step 7.4: Failure taxonomy and troubleshooting docs - **Status: Completed**
  - Deliverables: actionable error catalog and operator playbook.
  - Acceptance: support/debug cycle time drops for simulation issues.

- [ ] Step 7.5: Beta readiness checklist and signoff - **Status: Completed**
  - Deliverables: go/no-go criteria covering accuracy, stability, UX, and performance.
  - Acceptance: checklist completed and signed by maintainers.

---

## Phase 8 - Advanced Simulation Capability Expansion

- [ ] Step 8.1: SPICE compatibility layer hardening - **Status: Completed**
  - Deliverables: deeper `.model`/`.lib` support, parameter inheritance, and parser diagnostics with source location.
  - Acceptance: broader vendor model decks parse and run with actionable errors on unsupported cards.

- [ ] Step 8.2: Device model portfolio expansion - **Status: Completed**
  - Deliverables: op-amp macro-model support, transmission lines, switch/relay behavior, and controlled-source refinements.
  - Acceptance: analog macro-model reference decks pass expected operating behavior.

- [ ] Step 8.3: Noise and distortion analysis suite - **Status: Completed**
  - Deliverables: `.noise`, THD, and harmonic post-processing pipeline with report output.
  - Acceptance: canonical distortion/noise benchmark decks produce stable expected metrics.

- [ ] Step 8.4: Mixed-signal co-simulation improvements - **Status: Completed**
  - Deliverables: stronger analog/digital synchronization, threshold controls, and event-driven scheduling stability.
  - Acceptance: mixed-signal reference circuits run without timing drift regressions.

- [ ] Step 8.5: Parametric optimization engine - **Status: Completed**
  - Deliverables: multi-dimensional sweeps, target/constraint optimizer, and yield summary outputs.
  - Acceptance: optimization jobs converge deterministically on benchmark tasks.

- [ ] Step 8.6: Convergence assistant and auto-fix guidance - **Status: Completed**
  - Deliverables: diagnostics that map failures to likely causes and suggest repair actions.
  - Acceptance: common convergence failures become one-iteration debuggable.

- [ ] Step 8.7: Advanced probing and equation channels - **Status: Completed**
  - Deliverables: math traces (`V(out)-V(in)`, ratios, derivatives), persistent cursors, and scripted measurements.
  - Acceptance: measurement features match expected results against known waveforms.

- [ ] Step 8.8: Generator designer and waveform library - **Status: Completed**
  - Deliverables: visual PWL editor, CSV import/export, reusable source presets, and shared waveform templates.
  - Acceptance: users can define/reuse complex sources without manual string editing.

- [ ] Step 8.9: Large-circuit performance path - **Status: Completed**
  - Deliverables: sparse solver improvements, matrix reuse, multithreaded sweeps, and bounded memory streaming.
  - Acceptance: large-circuit simulations scale with clear CPU/RAM budget adherence.

- [ ] Step 8.10: Cross-simulator verification framework - **Status: Completed**
  - Deliverables: nightly comparison against trusted references (e.g., ngspice/LTspice-equivalent decks).
  - Acceptance: numeric drift is reported with tolerance thresholds and trend history.

- [ ] Step 8.11: Post-processing and export workflows - **Status: Completed**
  - Deliverables: waveform comparison overlays, report templates, and structured export formats.
  - Acceptance: analysis artifacts are reproducible and automation-friendly.

- [ ] Step 8.12: Schematic-native simulation UX completion - **Status: Completed**
  - Deliverables: instrument placement on schematic, pin-attached probes, and clickable error-to-item navigation.
  - Acceptance: end-to-end simulate/debug loop is executable from schematic canvas alone.

---

## Phase 9 - High-End Simulation Analysis

- [ ] Step 9.1: Global Parameter Expressions - **Status: Completed**
  - Deliverables: evaluateExpressions() in SimNetlist using Sim::Expression engine.
  - Acceptance: Components resolve {EXPR} values based on .PARAM or project global parameters.

- [ ] Step 9.2: Advanced Monte Carlo (Gaussian & Lots) - **Status: Completed**
  - Deliverables: Support for Normal/Gaussian distributions and LotId-based variation sync.
  - Acceptance: Statistical analysis produces realistic bell-curve distributions and tracks correlated component lots.

- [ ] Step 9.3: Built-in FFT Spectrum Analysis - **Status: Completed**
  - Deliverables: solveFFT() implementation with resampling, windowing (Hann), and dB magnitude output.
  - Acceptance: Users can see frequency-domain magnitude plots directly from transient simulation results.

---

## Immediate Execution Order (First 10 Steps)

1. Step 0.1
2. Step 0.2
3. Step 0.3
4. Step 1.1
5. Step 1.2
6. Step 1.3
7. Step 1.4
8. Step 2.1
9. Step 2.2
10. Step 4.1

## Progress Tracking Rule

When any step is finished, update the same line to `**Status: Completed**` and keep acceptance notes in the relevant module docs/tests.
