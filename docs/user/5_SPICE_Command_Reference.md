# SPICE Command Reference

VioSpice uses the high-performance **VioMATRIXC** engine (based on ngspice). It supports industry-standard SPICE syntax along with several advanced enhancements for modern circuit design.

---

## 1. Simulation Analyses

These commands define the type of simulation to be performed.

### Transient Analysis (`.tran`)
Observes circuit behavior over time.
*   **Syntax**: `.tran <step> <stop> [start] [max_step]`
*   **Example**: `.tran 10u 10m` (Run for 10ms with 10us steps)

### AC Analysis (`.ac`)
Performs a small-signal frequency domain sweep.
*   **Syntax**: `.ac <type> <points> <start_freq> <stop_freq>`
*   **Example**: `.ac dec 100 10 1meg` (Logarithmic sweep, 100 pts/decade, from 10Hz to 1MHz)

### DC Sweep (`.dc`)
Sweeps a source voltage or current to see DC transfer characteristics.
*   **Syntax**: `.dc <source> <start> <stop> <increment>`
*   **Example**: `.dc V1 0 5 0.1` (Sweep V1 from 0V to 5V in 100mV steps)

### Operating Point (`.op`)
Calculates the steady-state DC voltages and currents.
*   **Syntax**: `.op`

---

## 2. Component Syntax (Primitives)

| Prefix | Component Type | Example Syntax |
| :--- | :--- | :--- |
| **R** | Resistor | `R1 N1 N2 1k` |
| **C** | Capacitor | `C1 N1 N2 10u` |
| **L** | Inductor | `L1 N1 N2 1m` |
| **D** | Diode | `D1 N_anode N_cathode MyDiodeModel` |
| **Q** | BJT | `Q1 collector base emitter MyBJTModel` |
| **M** | MOSFET | `M1 drain gate source bulk MyMOSModel` |
| **V** | Voltage Source | `V1 plus minus DC 5` |
| **I** | Current Source | `I1 plus minus 10m` |
| **X** | Subcircuit | `X1 in out gnd MY_OPAMP_MACRO` |

---

## 3. Advanced Source Features

VioSpice adds native support for external data-backed sources.

### Native PWL File (`pwlfile`)
Loads Piece-Wise Linear data directly from a text file. This avoids netlist bloat and line-length errors.
*   **Example**: `V1 N1 0 pwlfile="/path/to/data.pwl"`

### WAVEFILE Source
Uses a standard `.wav` audio file as a voltage or current source.
*   **Example**: `V1 N1 0 WAVEFILE="input.wav" CHAN=0`

---

## 4. Control & Logic

### Models (`.model`)
Defines the physics parameters for semiconductors and complex components.
*   **Example**: `.model MyDiode D(Is=1e-14 Rs=0.1)`

### Subcircuits (`.subckt`)
Encapsulates a collection of components into a reusable block.
*   **Example**:
    ```spice
    .subckt MY_FILTER IN OUT GND
    R1 IN OUT 1k
    C1 OUT GND 1u
    .ends
    ```

### Parameters (`.param`)
Defines variables that can be used in component values.
*   **Example**: 
    ```spice
    .param RVAL=4.7k
    R1 N1 0 {RVAL}
    ```

### Include (`.include`)
Loads external library or model files.
*   **Example**: `.include "./models/opamps.lib"`

---

## 5. Engineering Suffixes

VioSpice supports the following standard suffixes:

| Suffix | Name | Value |
| :--- | :--- | :--- |
| **T** | Tera | 10¹² |
| **G** | Giga | 10⁹ |
| **Meg** | Mega | 10⁶ |
| **K** | Kilo | 10³ |
| **m** | milli | 10⁻³ |
| **u** | micro | 10⁻⁶ |
| **n** | nano | 10⁻⁹ |
| **p** | pico | 10⁻¹² |
| **f** | femto | 10⁻¹⁵ |

*Note: Suffixes are case-insensitive (e.g., `1M` and `1m` are both milli). Use `1Meg` for Mega.*
