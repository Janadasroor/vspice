# Component to Simulator Model Mapping Contract

Recorded on: 2026-02-25

This document defines the deterministic mapping from schematic `ECOComponent` entries to simulator `SimComponentType`.

## Mapping Rules

| Schematic Item Type | Condition | Simulator Type | Notes |
|---|---|---|---|
| `ResistorType` | always | `Resistor` | `resistance` from parsed value |
| `CapacitorType` | always | `Capacitor` | `capacitance` from parsed value |
| `InductorType` | always | `Inductor` | `inductance` from parsed value |
| `DiodeType` | always | `Diode` | standard 2-pin ordering |
| `TransistorType` | value contains `PNP` | `BJT_PNP` | else defaults as below |
| `TransistorType` | value contains `NMOS` | `MOSFET_NMOS` |  |
| `TransistorType` | value contains `PMOS` | `MOSFET_PMOS` |  |
| `TransistorType` | otherwise | `BJT_NPN` | deterministic default |
| `VoltageSourceType` | always | `VoltageSource` | supports DC/SINE/PULSE/EXP params |
| `PowerType` | ground alias (`GND`, `0`, etc.) | skipped | not a source component |
| `PowerType` | non-ground rail | `VoltageSource` | positive rail to GND mapping |
| `ComponentType` | `typeName in {OscilloscopeInstrument, VoltmeterInstrument, AmmeterInstrument, WattmeterInstrument, FrequencyCounterInstrument, LogicProbeInstrument}` | `Resistor` | mapped as high-Z probe (`100M`) + auto-probed nets |
| `ICType` | value starts `B_V` | `B_VoltageSource` | behavioral voltage source |
| `ICType` | value starts `B_I` | `B_CurrentSource` | behavioral current source |
| `ICType` | otherwise | `VoltageSource` | macro/subckt fallback |
| `CustomType` | `spiceModel` set, or `value`/`typeName` indicates subckt | `SubcircuitInstance` | uses subckt name from `spiceModel` or `value` |
| `CustomType` | `typeName` in {"g", "g2", "vccs"} (case-insensitive) | `VCCS` | voltage-controlled current source, requires 4 pins |
| `CustomType` | `typeName` in {"e", "e2", "vcvs"} (case-insensitive) | `VCVS` | voltage-controlled voltage source, requires 4 pins |
| `CustomType` | Reference starts with `A` (case-insensitive) AND symbol has OTA pins | `SubcircuitInstance` | LTspice OTA, translated to B-source at netlist generation |

## Unsupported Components Policy

When a component has no mapping:

- Simulation does not crash.
- Component is skipped.
- A deterministic warning is emitted with:
  - component reference
  - item type enum
  - `typeName`
  - value text
  - specific reason

Warning format (example):

- `U7 [type=1, typeName=SomeType, value=foo] -> generic component type 'SomeType' has no simulator mapping`

Warnings are sorted before logging to keep output stable and actionable.

## OTA (Operational Transconductance Amplifier) Notes

OTAs are handled differently from other components because **ngspice does not have native OTA support**. Instead of direct simulator stamping, OTAs go through a **netlist translation phase** before simulation.

### OTA Processing Pipeline

1. **Schematic Capture**: User places OTA symbol with 8 pins (IN+, IN-, OUT+, OUT-, VCC, VEE, OUT, GND)
2. **Bridge Mapping**: Component mapped as `SubcircuitInstance` (generic handling)
3. **Netlist Generation**: `SpiceNetlistGenerator::buildNgspiceOtaTranslation()` detects A-prefix elements
4. **Translation**: OTA line converted to B-source + optional R/C companion elements
5. **Simulation**: ngspice executes translated B-source netlist normally

### OTA Detection Criteria

A schematic component is recognized as an OTA if:
- Reference prefix starts with `A` (case-insensitive)
- Symbol defines the expected OTA pin structure
- Value/typeName contains OTA-related identifiers

### OTA vs VCCS Distinction

| Feature | OTA (A-element) | VCCS (G-element) |
|---------|-----------------|------------------|
| Simulator support | Via B-source translation | Native MNA stamping |
| Pins | 8 pins (full OTA model) | 4 pins (differential I/O + control) |
| Parameters | gm, iout, isink, rout, cout, vhigh, vlow, linear | gm only |
| Current limiting | Yes (soft/hard) | No (linear only) |
| Use case | Complete OTA macromodels | Simple transconductance blocks |

## Pin Connection Policy

- Mapped components must have enough connected pins for their model.
- If insufficient pins are connected, the component is skipped with a deterministic warning.
- Pin ordering rules are fixed per model class (e.g., diode `1,2`; BJT `C,B,E`; MOSFET `D,G,S`).
