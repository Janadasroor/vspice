# Simulator Beta Readiness Checklist and Signoff

Last updated: 2026-02-25

This checklist defines the go/no-go criteria for simulator beta release.

## Release Decision

- `GO`: all blocking gates pass, no open High severity defects, signoffs recorded.
- `NO-GO`: any blocking gate fails, unresolved crash/data-loss bug, or missing owner signoff.

## Blocking Gates (Must Pass)

| Gate | Criteria | Evidence | Status |
|---|---|---|---|
| Accuracy | >= 90% reference analog decks pass numeric tolerances | `simulator/tests/reference_decks` run report | [ ] |
| Stability | No crash/hang across OP/TRAN/AC regression and integration suites | CI results + local repro logs | [ ] |
| Integration | Schematic->bridge->engine integration tests pass | `integration.schematic_simulator` ctest | [ ] |
| Parser robustness | Parser fuzz harness passes without crash/hang | `simulator.parser_fuzz` ctest | [ ] |
| Performance | Simulator budgets pass in CI (CPU/RSS thresholds) | performance workflow artifacts | [ ] |
| Diagnostics | Failure taxonomy + troubleshooting docs published and reviewed | `simulator/TROUBLESHOOTING.md` | [ ] |
| UX flow | Setup, run, probe, error navigation validated by maintainer smoke test | test notes + screenshots/logs | [ ] |

## Quality Gates (Must Be Reviewed)

| Area | Acceptance | Status | Notes |
|---|---|---|---|
| Determinism | Repeat runs produce stable outputs within tolerance | [ ] |  |
| Numerical safeguards | NR, gmin/source stepping, adaptive timestep controls verified | [ ] |  |
| Coverage trend | Unit/integration/regression suite trend is stable (no drop) | [ ] |  |
| Memory behavior | Long transient does not show unbounded growth | [ ] |  |
| Error mapping | Runtime errors map to net/component when possible | [ ] |  |

## Pre-Release Execution Checklist

1. Run full simulator-related ctest labels:
   - `ctest --test-dir build -L simulator --output-on-failure`
   - `ctest --test-dir build -L integration --output-on-failure`
   - `ctest --test-dir build -L regression --output-on-failure`
2. Run performance budget checks and archive reports.
3. Review open simulator issues and classify severity:
   - Blockers: crash, hang, incorrect major results, severe resource regression.
   - Non-blockers: minor UI polish, low-impact warnings with documented workaround.
4. Execute beta smoke deck set (minimum):
   - DC divider and bias network
   - RC/RL/RLC transient
   - diode rectifier
   - BJT stage
   - MOS inverter
   - parameter sweep
5. Confirm troubleshooting playbook covers all observed beta failures.

## Defect Exit Policy

- Allowed open defects at beta cut:
  - `Critical`: 0
  - `High`: 0
  - `Medium`: <= 5, each with workaround
  - `Low`: tracked in backlog
- Any new crash/hang in beta candidate resets status to `NO-GO`.

## Signoff Matrix

| Role | Owner | Decision | Date | Notes |
|---|---|---|---|---|
| Simulator maintainer |  | [ ] GO / [ ] NO-GO |  |  |
| Schematic/bridge maintainer |  | [ ] GO / [ ] NO-GO |  |  |
| QA/release maintainer |  | [ ] GO / [ ] NO-GO |  |  |

## Final Beta Decision Record

- Candidate build/tag:
- Decision date:
- Outcome: `[ ] GO` / `[ ] NO-GO`
- Blocking issues (if NO-GO):
1.
2.
3.
