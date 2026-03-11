# Hierarchical Sheet Flattening Rules

This document defines the canonical flattening behavior from hierarchical schematic/subcircuit structure into a primitive `SimNetlist` used by the solver.

## Goals

- Produce deterministic primitive component lists for simulation.
- Preserve electrical equivalence between hierarchical and manually flattened circuits.
- Keep generated names stable enough for diagnostics and debug traces.

## Canonical Rules

1. Ground handling
- Node `0` is global ground and is never remapped to a non-zero node.
- Ground aliases (for example `GND`, `VSS`, `COM`) resolve to node `0` in netlist lookup.

2. Subcircuit pin mapping
- For an instance `Xn` with `k` pins, local subcircuit nodes `1..k` map to the instance's connected external nodes in order.
- If fewer external nodes are provided than pins declared, only available pin mappings are applied.

3. Internal node allocation
- Any subcircuit-local node index greater than pin count is treated as internal.
- Each internal node gets a unique global node with a deterministic name:
  - `<instance-prefix><instance-name>:<local-node-id>`
  - Example: `X1:4`, `Top:U3:7`

4. Recursive expansion
- Flattening walks component lists recursively.
- Nested instances append to prefix using `:` separator.
- Primitive component names are prefixed with full instance path:
  - `<prefix><primitive-name>`
  - Example: `X1:Rin`, `Top:U3:X2:E1`

5. Primitive node remapping
- After prefixing, each primitive component's node IDs are remapped via the active local-to-global mapping for that expansion frame.
- Unmapped node IDs remain unchanged (already-global nodes).

## Acceptance Contract

- Hierarchical and manually flattened equivalents must produce matching operating-point voltages within numerical tolerance.
- Regression test: `testHierarchicalFlatteningEquivalence()` in `simulator/tests/test_basic_circuits.cpp`.
