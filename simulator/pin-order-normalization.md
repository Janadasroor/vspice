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

## Failure Policy

When required role pins are missing:

- Component is skipped from simulation netlist.
- Deterministic warning is emitted with:
  - component identity and type details
  - missing role set
  - available normalized pins

This prevents polarity-sensitive mis-stamping for diodes and transistors.
