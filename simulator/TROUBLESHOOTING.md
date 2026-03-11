# Simulator Failure Taxonomy and Troubleshooting Playbook

Last updated: 2026-02-25

This guide maps real simulator error signatures to likely root causes, fast checks, and corrective actions.

## Quick Triage Flow

1. Capture the exact error text from logs/UI.
2. Classify it using the taxonomy table below.
3. Run the "Fast checks" for that class.
4. Apply the "Fix actions" in order.
5. Re-run with tighter logging if still failing:
   - enable `logNRIterationProgress`
   - enable `logNRFailureDiagnostics`
   - enable `logHomotopyProgress`
   - enable `logTransientStepControl`

## Failure Taxonomy

| Class | Typical signature | Severity | Owner |
|---|---|---|---|
| Netlist mapping | `Simulator mapping warnings (...)` | Medium | Schematic/Bridge |
| Connectivity fallback | `SchematicConnectivity returned no nets, falling back ...` | Medium | Schematic/Bridge |
| Structural singularity | `Singular MNA matrix at elimination step ...` | High | Schematic + Solver |
| OP non-convergence | `DC OP failed to converge after trying all recovery methods.` | High | Models/Solver |
| NR stall | `Simulator NR Failure: maxIter=...` | High | Models/Solver |
| Transient rejection storm | `reject(nonconverged)` / `reject(error)` / `Transient simulation aborted at t=...` | High | Models/Solver/Config |
| Empty result surface | `no data generated` / `no traces generated` | Medium | UI/Bridge/Config |
| Subcircuit resolution | `Subcircuit '...' not found.` | High | Netlist/Model library |
| Expression evaluation | `Stack underflow for operator/function` | Medium | Behavioral source expression |

## Catalog and Remediation

### 1) Netlist mapping warnings

Signatures:
- `Simulator mapping warnings (N):`
- `unsupported item type enum ...`
- `pin-order normalization missing required roles [...]`
- `insufficient connected pins (...)`

Likely causes:
- Unsupported schematic item type or type-name mismatch.
- Missing/ambiguous pin naming on symbols.
- Component placed but not fully wired.

Fast checks:
- Verify component type appears in `simulator/component-model-mapping.md`.
- Confirm pin names match alias rules in `simulator/pin-order-normalization.md`.
- Check each failing component has required pin count connected.

Fix actions:
- Add/adjust mapping in `simulator/bridge/sim_schematic_bridge.cpp`.
- Normalize symbol pin naming to supported aliases.
- Add wiring/ERC checks for missing pins before simulation.

### 2) Connectivity fallback path activated

Signature:
- `Simulator: SchematicConnectivity returned no nets, falling back to NetlistGenerator path.`

Likely causes:
- `SchematicConnectivity::buildConnectivity()` returned empty due to scene/net-manager mismatch.
- Connectivity graph build regression.

Fast checks:
- Confirm scene has conductive and component items.
- Run connectivity-only tests/integration tests.

Fix actions:
- Fix connectivity extraction first; fallback is compatibility path, not target path.
- Add regression test reproducing the empty-net condition.

### 3) Structural singular matrix

Signatures:
- `Simulator Error: Singular MNA matrix at elimination step ...`
- `Simulator Error: Singular sparse MNA matrix at elimination step ...`
- Follow-up hints from solver:
  - `conflicting ideal voltage sources ...`
  - `floating node/open circuit ...`
  - `unobservable variable ...`
  - `severe ill-conditioning ...`

Likely causes:
- Floating nets without DC path to ground.
- Ideal-source constraints conflict.
- Missing shunt/series parasitics on idealized networks.

Fast checks:
- Ensure at least one ground reference exists.
- Verify every major node has a DC reference path.
- Check for source loops of ideal voltage sources.

Fix actions:
- Add realistic series/shunt resistances where appropriate.
- Fix connectivity shorts/opens in schematic.
- Reduce ideal constraints or add model parasitics.

### 4) OP non-convergence and NR stall

Signatures:
- `Simulator: DC OP failed to converge after trying all recovery methods.`
- `Simulator NR Failure: maxIter=... sourceFactor=... gmin=...`
- `Simulator: homotopy fallbacks exhausted without convergence ...`

Likely causes:
- Hard nonlinear bias point.
- Overly aggressive model parameters.
- Poor initial conditions or insufficient damping limits.

Fast checks:
- Enable NR/homotopy logs and inspect residual trend.
- Try simplified circuit (remove optional nonlinear blocks).
- Compare with known-good deck behavior.

Fix actions:
- Increase `maxNRIterations`.
- Tune `nrMaxVoltageStep`, `nrMinDamping`.
- Increase `gmin` and/or strengthen stepping schedule:
  - `gminSteppingStart`, `gminSteppingSteps`
  - `sourceSteppingInitial`, `sourceSteppingMaxStep`
  - `combinedHomotopySteps`

### 5) Transient rejection storm / abort

Signatures:
- `Simulator TRAN: reject(nonconverged) ...`
- `Simulator TRAN: reject(error) ...`
- `Transient simulation failed to converge at t=...`
- `Transient simulation aborted at t=...`

Likely causes:
- Time step too large for circuit stiffness.
- Strong switching discontinuities with limited damping.
- LTE constraints impossible with current model settings.

Fast checks:
- Inspect `h` changes in transient step control logs.
- Check source edge speeds (`PULSE` rise/fall) and stiff RC/L networks.

Fix actions:
- Reduce `tStep` and cap `transientMaxStep`.
- Set explicit `transientMinStep`.
- Relax tolerances moderately (`relTol`, `absTol`) if physically acceptable.
- Revisit source waveform edge parameters.

### 6) Empty results from manager layer

Signatures:
- `Simulation failed to converge or matrix is singular.`
- `Transient simulation failed (no data generated).`
- `AC simulation failed (no data generated).`
- `Parametric Sweep Error: no traces generated for '...'`

Likely causes:
- Upstream convergence failure.
- Invalid analysis configuration.
- Sweep parameter target not matched (`component.param` mismatch).

Fast checks:
- Re-run underlying OP/TRAN/AC without UI wrapper.
- Confirm sweep parameter exists on target component.

Fix actions:
- Resolve underlying solver/netlist issue first.
- Validate analysis config before run dispatch.
- Add per-analysis preflight validation in bridge/UI.

### 7) Subcircuit resolution failures

Signature:
- `Simulator Error: Subcircuit '...' not found.`

Likely causes:
- Missing `.subckt` definition in loaded library.
- Name mismatch/case mismatch.

Fast checks:
- Verify parser loaded expected subcircuit names.
- Confirm include/library load order.

Fix actions:
- Load required model library before netlist flatten.
- Normalize naming conventions in import path.

### 8) Behavioral expression parser underflow

Signatures:
- `Stack underflow for operator`
- `Stack underflow for function`

Likely causes:
- Malformed expression token stream.
- Unsupported or truncated behavioral expression.

Fast checks:
- Isolate expression string from source/component.
- Validate parentheses and operator arity.

Fix actions:
- Reject invalid expressions earlier with clear diagnostics.
- Extend parser validation for operator/function arity.

## Recommended Logging Presets

### Convergence deep-dive preset

- `logNRIterationProgress = true`
- `logNRFailureDiagnostics = true`
- `logHomotopyProgress = true`
- `maxNRIterations = 300`

### Transient stability preset

- `logTransientStepControl = true`
- `transientMinStep = 1e-12` (or design-specific lower bound)
- `transientMaxStep = tStep`
- `useAdaptiveStep = true`

## Escalation Rules

Escalate from support/integration to solver owners when any of these occur:
- Singular matrix reproduced on minimal deck with clear ground/reference.
- NR fails after homotopy retries on previously passing regression deck.
- Deterministic crash/hang reproduced by parser fuzz harness.

Escalate to schematic/bridge owners when:
- Mapping warnings spike after symbol library or pin schema changes.
- Connectivity fallback appears in normal project flows.
- Diagnostic target extraction cannot map runtime error back to net/component.
