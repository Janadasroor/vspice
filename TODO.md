# TODO

## CLI (AI-Friendly)
1. `netlist-run --stats` (min/max/avg/rms per signal). ✅
2. `netlist-run --range t0:t1` (time window export). ✅
3. `netlist-run --measure <expr>` (e.g., `V(net1)@t=1ms`, `V(net1)_peak`). ✅
4. `raw-info --summary` (counts + default signal list). ✅
5. `raw-export --signal-regex <pattern>`. ✅
6. `schematic-netlist --out <file.cir>` (write netlist to file). ✅
7. `netlist-validate` (syntax + unknown model report). ✅
8. `schematic-probe --auto` (auto‑probe all nets). ✅
9. `--json` stable ordering for all commands. ✅
10. `--quiet` truly silent (suppress ngspice warnings). ✅
11. `--exit-on-warning` (non‑zero exit for warnings in CI). ✅

## Raw Data & Waveforms
1. `--max-points` smart decimation (min/max bucket) ✅
2. `--base-signal <name>` to drive decimation (optional). ✅
3. `raw-export --format parquet` (future). ✅

## UI/UX (Behavioral Voltage Source)
1. Add BV expression editor with presets, validation, and tips. ✅
2. Add Behavioral (BV) to voltage source properties dialog. ✅
3. Use LTspice dialog for BV right‑click instead of simple prompt. ✅

## Quality & Stability
1. Add regression tests for CLI waveform export. ✅
2. Add test fixtures for ngspice `.raw` parsing. ✅
