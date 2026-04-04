# OTA (Operational Transconductance Amplifier) Support

## 1. Overview

The Operational Transconductance Amplifier (OTA) is a voltage-controlled current source that produces an output current proportional to the differential input voltage. OTAs are fundamental components in analog circuit design, particularly in:

- Voltage-controlled amplifiers (VCA)
- Tunable active filters
- Analog multipliers and mixers
- Current-mode signal processing
- Oscillator and waveform generation circuits

VioraEDA provides full LTspice-compatible OTA support through automatic netlist translation to ngspice-compatible behavioral sources.

## 2. Device Characteristics

### 2.1 Fundamental Relationship

The ideal OTA is defined by:

```
I_out = gm * (V_IN+ - V_IN-)
```

Where:
- `I_out` = Output current (Amperes)
- `gm` = Transconductance (Siemens)
- `V_IN+` = Non-inverting input voltage
- `V_IN-` = Inverting input voltage

### 2.2 Comparison with Operational Amplifiers

| Characteristic | Operational Amplifier | OTA |
|----------------|----------------------|-----|
| Output quantity | Voltage | Current |
| Output impedance | Low | High |
| Gain control | External feedback network | Direct (gm parameter) |
| Primary application | Voltage amplification | Current sourcing, filtering |

## 3. Implementation in VioraEDA

### 3.1 Architecture

LTspice OTA elements (A-prefix) are not natively supported by ngspice. VioraEDA performs automatic translation during netlist generation, converting OTA specifications into equivalent behavioral source (B-element) representations.

The translation is implemented in `SpiceNetlistGenerator::buildNgspiceOtaTranslation()` located in `schematic/analysis/spice_netlist_generator.cpp`.

### 3.2 Translation Process

**Input (LTspice format):**

```spice
A1 IN+ IN- OUT+ OUT- VCC VEE OUT GND OTA g=1m iout=10u isink=10u rout=1Meg cout=10p
```

**Processing:**

1. Parse differential input voltage
2. Apply transconductance multiplication
3. Apply current limiting (linear or hyperbolic tangent-based)
4. Apply compliance voltage clamping (if specified)
5. Generate companion output resistance and capacitance elements

**Output (ngspice-compatible):**

```spice
* OTA_TRANSLATED A1
B__OTA_A1 OUT GND I={<computed_expression>}
R__OTA_A1 OUT GND 1Meg
C__OTA_A1 OUT GND 10p
```

### 3.3 Pin Configuration

The OTA symbol defines eight terminals:

| Pin | Name | Function |
|-----|------|----------|
| 1 | IN+ | Non-inverting differential input |
| 2 | IN- | Inverting differential input |
| 3 | OUT+ | Positive output terminal |
| 4 | OUT- | Negative output terminal |
| 5 | VCC | Positive supply rail |
| 6 | VEE | Negative supply rail |
| 7 | OUT | Output node |
| 8 | GND | Ground reference |

### 3.4 Parameter Reference

| Parameter | Description | Default | Example |
|-----------|-------------|---------|---------|
| `g` | Transconductance (A/V) | 1u | `g=1m` |
| `iout` | Maximum source current | 10u | `iout=50m` |
| `isink` | Maximum sink current | -(iout) | `isink=50m` |
| `rout` | Output resistance | Open circuit | `rout=1Meg` |
| `cout` | Output capacitance | None | `cout=10p` |
| `vhigh` | High compliance voltage offset | None | `vhigh=0.5` |
| `vlow` | Low compliance voltage offset | None | `vlow=0.5` |
| `linear` | Use hard current limiting | Soft (tanh) | `linear` |
| `ref` | Reference offset voltage | 0 | `ref=1.5` |

## 4. Current Limiting Modes

### 4.1 Soft Limiting (Default)

The default implementation uses hyperbolic tangent-based soft clipping:

```
I_pos = u(I_raw) * I_limit * tanh(I_raw / I_limit)
I_neg = u(-I_raw) * |I_limit| * tanh(-I_raw / |I_limit|)
I_out = I_pos - I_neg
```

### 4.2 Hard Limiting

When the `linear` parameter is specified:

```
I_out = min(max(I_raw, I_sink), I_out)
```

## 5. Compliance Voltage Clamping

When `vhigh` or `vlow` parameters are specified, output current is modulated:

```
I_final = I_out * u((V_high - V_out + epsilon)) * u((V_out - V_low + epsilon))
```

Where `u()` denotes the unit step function.

## 6. VCCS versus OTA

VioraEDA supports both Voltage-Controlled Current Sources (VCCS, G-element) and OTAs (A-element). The selection depends on application requirements.

### 6.1 VCCS (G-element)

Use VCCS when:

- Simple linear transconductance is sufficient
- Current limiting is not required
- Minimal parameter configuration is desired
- Direct simulator stamping is preferred

**Component mapping:**

- TypeName: `g`, `g2`, or `vccs` (case-insensitive)
- Value: `g=1m`
- Pins: Out+, Out-, Ctrl+, Ctrl-

### 6.2 OTA (A-element)

Use OTA when:

- Current limiting is required (iout/isink)
- Output impedance modeling is needed (rout, cout)
- Compliance voltage clamping is required (vhigh, vlow)
- Importing LTspice designs
- Realistic macromodeling of OTA integrated circuits (LM13700, CA3080)

**Component mapping:**

- Reference: A-prefix (e.g., A1, A_OTA1)
- Value: `OTA g=1m iout=10m isink=10m rout=100k`
- Pins: IN+, IN-, OUT+, OUT-, VCC, VEE, OUT, GND

### 6.3 Comparison Summary

| Feature | VCCS (G-element) | OTA (A-element) |
|---------|------------------|-----------------|
| Simulator support | Native MNA stamping | B-source translation |
| Pin count | 4 | 8 |
| Parameters | gm | gm, iout, isink, rout, cout, vhigh, vlow |
| Current limiting | Not supported | Supported (soft/hard) |
| Application | Simple transconductance | Complete OTA macromodels |

## 7. Example Configurations

### 7.1 Basic Transconductance Stage

```
Value: OTA
SPICE Model: g=1m
```

Produces: `I_out = 1m * (V_IN+ - V_IN-)`

### 7.2 Current-Limited OTA

```
Value: OTA
SPICE Model: g=2m iout=5m isink=5m rout=500k
```

Output current limited to +/-5mA with 500kOhm output resistance.

### 7.3 Voltage-Controlled Transconductance

```
Value: OTA
SPICE Model: g={VCTRL} iout=1m isink=1m cout=100p
```

Transconductance controlled by external voltage source VCTRL.

## 8. Simulation Guidelines

### 8.1 Transconductance Selection

Recommended gm values:

- LM13700: `gm = 19.2 * I_abc` (I_abc is bias current)
- CA3080: `gm = 9.6 * I_abc`
- Simple model: `gm = 1m`

### 8.2 Convergence Considerations

If simulation fails to converge:

- Add `rout` parameter to prevent floating output node
- Reduce gm value (begin with 1m or lower)
- Use `linear` flag instead of soft limiting
- Verify all pins are properly connected

### 8.3 Output Loading

Include `rout` and `cout` parameters for accurate high-frequency behavior:

```
rout=100k cout=5p
```

## 9. Troubleshooting

### 9.1 Component has no simulator mapping

**Cause:** OTA symbol is missing or incorrectly named.

**Resolution:** Ensure component reference begins with `A` (e.g., A1, A_OTA1).

### 9.2 Simulation divergence

**Cause:** Floating output node or unrealistic parameters.

**Resolution:**
- Add `rout=1Meg` to provide DC path to ground
- Set reasonable iout/isink limits
- Verify all eight pins are connected

### 9.3 Zero output current

**Cause:** Inputs not properly biased or gm equals zero.

**Resolution:**
- Verify V_IN+ - V_IN- has nonzero differential
- Confirm g parameter is set (e.g., `g=1m`)
- Ensure input nodes are not shorted

## 10. Component Mapping Contract

The deterministic mapping from schematic components to simulator types is defined in the following contract:

| Schematic Type | Condition | Simulator Type | Notes |
|----------------|-----------|----------------|-------|
| CustomType | typeName in {g, g2, vccs} | VCCS | Requires 4 pins |
| CustomType | typeName in {e, e2, vcvs} | VCVS | Requires 4 pins |
| CustomType | Reference starts with A AND symbol has OTA pins | SubcircuitInstance | LTspice OTA, translated to B-source |

## 11. Pin Order Normalization

The following pin aliases are accepted for OTA components:

| Role | Accepted Aliases |
|------|------------------|
| IN+ | 1, IN+, INP, IP, NONINV, PLUS |
| IN- | 2, IN-, INN, IN, NINV, MINUS |
| OUT+ | 3, OUT+, OUTP, OP |
| OUT- | 4, OUT-, OUTN, ON |
| VCC | 5, VCC, VDD, V+, VP, POS_SUPPLY |
| VEE | 6, VEE, VSS, V-, VN, NEG_SUPPLY |
| OUT | 7, OUT, OUTPUT, VO |
| GND | 8, GND, GROUND, 0, REF |

## 12. References

- LM13700 Datasheet, National Semiconductor
- CA3080 Datasheet, RCA Corporation
- LTspice Documentation: Special Devices, OTA
- ngspice Manual: Behavioral Sources (B-element)

## 13. Related Documentation

- [Simulator Architecture](../simulator/ARCHITECTURE.md)
- [Component Mapping Contract](../simulator/component-model-mapping.md)
- [Pin Order Normalization](../simulator/pin-order-normalization.md)
- [SPICE Directive Roadmap](../TODO.md)
