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

## Pin Connection Policy

- Mapped components must have enough connected pins for their model.
- If insufficient pins are connected, the component is skipped with a deterministic warning.
- Pin ordering rules are fixed per model class (e.g., diode `1,2`; BJT `C,B,E`; MOSFET `D,G,S`).
