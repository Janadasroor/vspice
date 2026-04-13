# VioSpice Python API — System Prompt for AI Assistants

> **Purpose**: This document describes the `vspice` Python module API. Send this
> to Gemini/ChatGPT/any LLM so it can write correct Python code for VioSpice
> circuit design and analysis.

---

## Overview

`vspice` is a Python module (built with nanobind) that provides direct access
to the VioSpice headless solver engine. It runs **without Qt or any GUI
dependency** — suitable for scripts, Jupyter notebooks, and server-side ML
pipelines.

```python
import vspice
```

---

## SPICE Number Utilities

### `parse_spice_number(text: str) -> (value: float, ok: bool)`

Parse SPICE engineering-suffix notation.

```python
val, ok = vspice.parse_spice_number("4.7u")    # (4.7e-6, True)
val, ok = vspice.parse_spice_number("1k")      # (1000.0, True)
val, ok = vspice.parse_spice_number("2meg")    # (2000000.0, True)
val, ok = vspice.parse_spice_number("33n")     # (3.3e-8, True)
val, ok = vspice.parse_spice_number("invalid") # (0.0, False)
```

Supported suffixes: `t`, `g`, `meg`, `k`, `m`, `u`, `n`, `p`, `f`, `mil`.
Units (`V`, `A`, `Hz`, `F`, `H`, `ohm`) are silently accepted and ignored.
Unicode micro (`µ`, `μ`) and omega (`Ω`, `℧`) signs are normalized.

### `format_spice_number(value: float, precision: int = 6) -> str`

```python
vspice.format_spice_number(0.0047)   # "4.7m"
vspice.format_spice_number(1000)      # "1k"
vspice.format_spice_number(4.7e-9)    # "4.7n"
```

---

## Enums

### `SimAnalysisType`

```
OP, Transient, AC, DC, MonteCarlo, Sensitivity, ParametricSweep,
Noise, Distortion, Optimization, FFT, RealTime, SParameter
```

### `SimIntegrationMethod`

```
BackwardEuler, Trapezoidal, Gear2
```

### `SimTransientStorageMode`

```
Full, Strided, AutoDecimate
```

### `SimComponentType`

```
Resistor, Capacitor, Inductor,
VoltageSource, CurrentSource,
Diode, BJT_NPN, BJT_PNP,
MOSFET_NMOS, MOSFET_PMOS,
JFET_NJF, JFET_PJF,
OpAmpMacro, Switch, TransmissionLine, SubcircuitInstance,
VCVS, VCCS, CCVS, CCCS,
B_VoltageSource, B_CurrentSource
```

### `ToleranceDistribution`

```
Uniform, Gaussian, WorstCase
```

---

## Data Structures

### `SimNode`

```python
node = vspice.SimNode(1, "INPUT")
print(node.id)     # 1
print(node.name)   # "INPUT"
```

### `SimWaveform`

```python
wf = vspice.SimWaveform("V(out)", [0.0, 1e-6, 2e-6], [0.0, 1.5, 3.0])
wf.name            # "V(out)"
wf.x               # list[float]
wf.y               # list[float]
wf.phase           # list[float] (for AC analysis)

# Statistics
stats = wf.stats()  # {'count': 3.0, 'min': 0.0, 'max': 3.0, 'avg': 1.5, 'rms': 1.936, 'pp': 3.0}

# Modify waveform data (replace entire list)
wf.y = [0.0, 2.0, 4.0]
```

### `SimResults`

Container for simulation output.

```python
results = vspice.SimResults()
results.waveforms = [...]
results.node_voltages = {"OUT": 3.3, "IN": 5.0}
results.branch_currents = {"V1": 0.001}
results.diagnostics = []  # read-only
results.s_parameter_results = []  # list of SParameterPoint
results.rf_z0 = 50.0
results.analysis_type = vspice.SimAnalysisType.Transient
results.schema_version  # int
results.is_schema_compatible()  # bool

# Quick summary
info = results.to_dict()
# {'analysis_type': 0, 'waveform_count': 3, 'node_count': 2, ...}
```

### `SParameterPoint`

```python
pt.frequency    # float (Hz)
pt.s11          # complex
pt.s12          # complex
pt.s21          # complex
pt.s22          # complex
```

### `SimTolerance`

```python
tol = vspice.SimTolerance(0.05, vspice.ToleranceDistribution.Gaussian)
tol.value         # 0.05 (relative, 5%)
tol.distribution  # ToleranceDistribution.Gaussian
tol.lot_id        # str — components with same lot_id vary together
```

### `SimComponentInstance`

```python
# Create: name, type, connected node IDs
r1 = vspice.SimComponentInstance("R1", vspice.SimComponentType.Resistor, [1, 2])
r1.params["resistance"] = 1000.0

# Symbolic parameters (unresolved expressions)
r1.param_expressions["resistance"] = "{RVAL*1.2}"

# Tolerances
r1.tolerances["resistance"] = vspice.SimTolerance(0.01, vspice.ToleranceDistribution.Uniform)

# Link to model or subcircuit
r1.model_name = "RES_1K_MODEL"

# Generate SPICE netlist line
spice_line = r1.to_spice()  # e.g. "R1 1 2 resistance=1000"
```

### `SimSubcircuit`

```python
sub = vspice.SimSubcircuit("VOLTAGE_DIVIDER", ["IN", "OUT", "GND"])
sub.components = [r1, r2]
sub.models = {"RES": model}
sub.parameters = {"RATIO": 0.5}
```

### `SimModel`

```python
model = vspice.SimModel("2N3904", vspice.SimComponentType.BJT_NPN)
model.params["IS"] = 6.734e-15
model.params["BF"] = 416.4
model.params["NF"] = 1.0
```

### `SimAnalysisConfig`

```python
cfg = vspice.SimAnalysisConfig()
cfg.type = vspice.SimAnalysisType.Transient
cfg.t_start = 0.0
cfg.t_stop = 0.01       # 10ms
cfg.t_step = 1e-6       # 1us
cfg.f_start = 1.0       # AC start frequency
cfg.f_stop = 1e6        # AC stop frequency
cfg.f_points = 100      # AC points per decade
cfg.rf_z0 = 50.0        # Reference impedance
cfg.integration_method = vspice.SimIntegrationMethod.Trapezoidal
cfg.rel_tol = 1e-3
cfg.abs_tol = 1e-6
```

### `SimNetlist`

The main circuit container.

```python
netlist = vspice.SimNetlist()

# Nodes
n_in = netlist.add_node("IN")   # returns node ID (1, 2, 3, ...)
n_gnd = netlist.ground_node()   # always 0

# Components
r1 = vspice.SimComponentInstance("R1", vspice.SimComponentType.Resistor, [n_in, n_gnd])
r1.params["resistance"] = 1000.0
netlist.add_component(r1)

# Models
model = vspice.SimModel("2N3904", vspice.SimComponentType.BJT_NPN)
model.params["IS"] = 6.734e-15
netlist.add_model(model)

# Subcircuits
sub = vspice.SimSubcircuit("AMP", ["IN", "OUT", "GND"])
netlist.add_subcircuit(sub)

# Parameters
netlist.set_parameter("TEMP", 27.0)
temp = netlist.get_parameter("TEMP", default=25.0)

# Probing
netlist.add_auto_probe("V(IN)")
probes = netlist.auto_probes()  # list[str]

# Access
print(netlist.node_count())        # int
print(netlist.find_node("IN"))     # int or -1
print(netlist.node_name(1))        # str
print(netlist.find_model("2N3904")) # SimModel or None
print(netlist.find_subcircuit("AMP")) # SimSubcircuit or None

# Properties (read-only accessors)
comps = netlist.components      # list[SimComponentInstance]
models = netlist.models          # dict[str, SimModel]
subs = netlist.subcircuits       # dict[str, SimSubcircuit]
diags = netlist.diagnostics      # list[str]

# Analysis configuration
netlist.analysis.type = vspice.SimAnalysisType.OP
netlist.analysis.rel_tol = 1e-6

# Processing
netlist.flatten()                # Expand subcircuits into primitives
netlist.evaluate_expressions()   # Resolve {EXPR} to numeric values

# Summary
print(netlist.to_dict())
# {'nodes': 3, 'components': 1, 'models': 1, 'subcircuits': 1, 'diagnostics': []}
```

---

## Example: Build a Voltage Divider

```python
import vspice

# Create netlist
nl = vspice.SimNetlist()
vin = nl.add_node("VIN")
vout = nl.add_node("VOUT")
gnd = nl.ground_node()

# Resistors
r1 = vspice.SimComponentInstance("R1", vspice.SimComponentType.Resistor, [vin, vout])
r1.params["resistance"] = 1000.0
nl.add_component(r1)

r2 = vspice.SimComponentInstance("R2", vspice.SimComponentType.Resistor, [vout, gnd])
r2.params["resistance"] = 2000.0
nl.add_component(r2)

# Voltage source
v1 = vspice.SimComponentInstance("V1", vspice.SimComponentType.VoltageSource, [vin, gnd])
v1.params["dc_voltage"] = 5.0
nl.add_component(v1)

# Expected Vout = 5 * 2000/(1000+2000) = 3.333V
print(f"Netlist: {nl.to_dict()}")
for comp in nl.components:
    print(f"  {comp.to_spice()}")
```

---

## What `vspice` CANNOT Do (Yet)

The `vspice` module provides **data structures and parsers only**. It does not
yet include the actual ngspice solver engine or simulation execution. The
following are NOT available in `vspice`:

- ❌ Running actual circuit simulations
- ❌ Connecting to ngspice shared library
- ❌ Creating .raw waveform files
- ❌ FFT functions (available in C++ but not yet bound)

To run actual simulations, use one of:
1. The existing `python/ai_pipeline/api/ml_dataset_api.py` FastAPI service
2. The `vio-cmd` CLI tool via subprocess
3. The MCP server (`python/scripts/viospice_mcp_server.py`)

---

## Rules for AI-Generated Code

1. **Always check `ok` from `parse_spice_number`** before using the value.
2. **Node IDs start at 1** — ground is always 0.
3. **Component names** should follow SPICE conventions (R1, C1, V1, Q1, etc.).
4. **Model names** are case-sensitive in SPICE.
5. **When building circuits**, use `SimComponentInstance` with the correct
   `SimComponentType` enum.
6. **Use `.to_spice()`** on components to verify the generated netlist lines.
7. **Set analysis config** before simulation (`netlist.analysis.type = ...`).
8. **Never assume simulation execution** is available via `vspice` — use the
   CLI or API service for that.
