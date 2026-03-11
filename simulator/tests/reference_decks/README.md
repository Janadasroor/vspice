# Simulator Reference Deck Suite

This directory contains baseline reference decks for simulator regression and validation.

Scope target for roadmap Step 0.2:
- At least 30 decks
- Coverage across linear, nonlinear, and dynamic behaviors
- Expected outputs tracked in `expected_outputs.json`

Directory layout:
- `linear/`: mostly linear OP/AC style circuits
- `nonlinear/`: diode/transistor/controlled/behavioral nonlinear cases
- `dynamic/`: transient/dynamic response cases

Notes:
- Deck syntax is SPICE-like and used as canonical test fixtures.
- Expected values are tolerance-based to support solver evolution.

## Comparator Utility

Use the tolerance-based comparator to validate actual simulator run outputs:

```bash
python3 scripts/check_sim_reference_decks.py \
  --expected simulator/tests/reference_decks/expected_outputs.json \
  --actual <path/to/actual_results.json>
```

During incremental adoption, missing deck results can be treated as warnings:

```bash
python3 scripts/check_sim_reference_decks.py \
  --expected simulator/tests/reference_decks/expected_outputs.json \
  --actual <path/to/actual_results.json> \
  --allow-missing
```

Actual results schema (minimal):

```json
{
  "results": [
    {
      "id": "op_voltage_divider_equal",
      "analysis": "op",
      "converged": true,
      "node_voltages": {"N2": 5.0},
      "ac": {"N2": [{"hz": 1591.5, "mag": 0.707}]},
      "waves": {"N2": {"x": [0.0, 1.0], "y": [0.0, 5.0]}}
    }
  ]
}
```

## Cross-Simulator Drift Reports

When native and trusted-reference actual results are available, generate drift + history reports:

```bash
./scripts/run_cross_simulator_verification.sh \
  docs/reports/reference_deck_native_actual.json \
  docs/reports/reference_deck_ngspice_actual.json \
  simulator/tests/reference_decks/expected_outputs.json \
  docs/reports/cross_simulator_verification.json \
  docs/refactor/cross-simulator-verification.md \
  docs/reports/cross_simulator_verification_history.json
```

Notes:
- If the reference JSON is missing, the script falls back to `native_vs_expected` mode.
- Use `scripts/cross_verify_simulators.py --help` for advanced options and strict fail behavior.
