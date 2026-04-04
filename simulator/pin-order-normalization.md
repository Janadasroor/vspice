# Simulation Pin-Order Normalization Contract

Recorded on: 2026-02-25

This document defines normalized pin ordering for simulator component stamping.

## Purpose

Library symbols can use numeric or named pins (for example `1/2` or `A/K`).
The simulator requires deterministic node ordering by model role.

## Role Order and Accepted Pin Aliases

- Diode (role order: `Anode`, `Cathode`)
  - `Anode`: `1`, `A`, `ANODE`, `+`
  - `Cathode`: `2`, `K`, `CATHODE`, `KATHODE`, `-`

- BJT (role order: `Collector`, `Base`, `Emitter`)
  - `Collector`: `1`, `C`, `COLLECTOR`
  - `Base`: `2`, `B`, `BASE`
  - `Emitter`: `3`, `E`, `EMITTER`

- MOSFET (role order: `Drain`, `Gate`, `Source`)
  - `Drain`: `1`, `D`, `DRAIN`
  - `Gate`: `2`, `G`, `GATE`
  - `Source`: `3`, `S`, `SOURCE`

- Two-pin passive/source models (role order: `Pin1`, `Pin2`)
  - `Pin1`: `1`, `P`, `POS`, `+`, `ANODE`
  - `Pin2`: `2`, `N`, `NEG`, `-`, `CATHODE`

- VCCS / VCVS (role order: `Out+`, `Out-`, `Ctrl+`, `Ctrl-`)
  - `Out+`: `1`, `P`, `POS`, `+`, `OUT+`, `OP`, `OUTP`
  - `Out-`: `2`, `N`, `NEG`, `-`, `OUT-`, `ON`, `OUTN`
  - `Ctrl+`: `3`, `CP`, `CTRL+`, `IN+`, `IP`, `CTRL_P`
  - `Ctrl-`: `4`, `CN`, `CTRL-`, `IN-`, `IN`, `CTRL_N`

- OTA (Operational Transconductance Amplifier) (role order: `IN+`, `IN-`, `OUT+`, `OUT-`, `VCC`, `VEE`, `OUT`, `GND`)
  - `IN+`: `1`, `IN+`, `INP`, `IP`, `NONINV`, `PLUS`
  - `IN-`: `2`, `IN-`, `INN`, `IN`, `NINV`, `MINUS`
  - `OUT+`: `3`, `OUT+`, `OUTP`, `OP`
  - `OUT-`: `4`, `OUT-`, `OUTN`, `ON`
  - `VCC`: `5`, `VCC`, `VDD`, `V+`, `VP`, `POS_SUPPLY`
  - `VEE`: `6`, `VEE`, `VSS`, `V-`, `VN`, `NEG_SUPPLY`
  - `OUT`: `7`, `OUT`, `OUTPUT`, `VO`
  - `GND`: `8`, `GND`, `GROUND`, `0`, `REF`

## Failure Policy

When required role pins are missing:

- Component is skipped from simulation netlist.
- Deterministic warning is emitted with:
  - component identity and type details
  - missing role set
  - available normalized pins

This prevents polarity-sensitive mis-stamping for diodes and transistors.
