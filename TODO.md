# SPICE Directive Roadmap

## In Progress

- [ ] Add validation and conflict warnings for SPICE directive blocks
  - [ ] Detect duplicate analysis cards in a directive block
  - [ ] Detect malformed `.subckt` / `.ends` nesting
  - [ ] Detect duplicate element references inside a directive block
  - [ ] Warn when directive element references collide with schematic references

## Next

- [ ] Improve directive block editor UX
  - [ ] Syntax highlighting for SPICE cards and element lines
  - [ ] Multiline editing polish for pasted LTspice netlist blocks
  - [ ] Selected-block netlist preview

- [ ] Expand LTspice compatibility
  - [ ] Better continuation-line handling with `+`
  - [ ] Support more cards: `.param`, `.func`, `.ic`, `.nodeset`, `.options`, `.save`, `.probe`
  - [ ] Preserve LTspice comment and whitespace quirks more closely
  - [ ] Improve `.asc` import/export for directive blocks
  - [ ] Rewrite more behavioral functions for ngspice compatibility
    - [ ] `buf(x)`
    - [ ] `inv(x)`
    - [ ] `uramp(x)`
    - [ ] `limit(x,y,z)`
    - [ ] `idtmod(x[,ic[,m[,o]]])`
  - [ ] Preserve or warn on LTspice B-source instance options
    - [ ] `ic=`
    - [ ] `tripdv=` / `tripdt=`
    - [ ] `laplace=` / `window=` / `nfft=` / `mtol=`
  - [ ] Improve source compatibility for LTspice restart semantics
    - [ ] Warn on `Trigger=` for `PULSE`, `PWL`, `SINE`, `EXP`, `SFFM`
    - [ ] Preserve `tripdv=` / `tripdt=` where possible
  - [ ] Expand `.meas` compatibility coverage
    - [ ] `DERIV`
    - [ ] `TRIG` / `TARG`
    - [ ] `RISE` / `FALL` / `CROSS` / `LAST`
    - [ ] `AVG` / `MAX` / `MIN` / `PP` / `RMS` / `INTEG`

- [ ] Improve schematic/manual-netlist coexistence
  - [ ] Add clear precedence rules for generated vs manual SPICE
  - [ ] Warn on duplicate rails, duplicate refs, and duplicate model declarations
  - [ ] Add an option to suppress generated parts when a manual block is intended to take over

- [ ] Build reusable subcircuit workflow
  - [ ] Save custom `.subckt` blocks with the project
  - [ ] Reuse custom macromodel blocks across schematics
  - [ ] Add symbol-to-subckt pin mapping helper
  - [ ] Auto-generate a symbol from a pasted `.subckt`

## Validation

- [ ] Add regression tests for mixed schematic + raw SPICE netlists
- [ ] Add golden netlist tests for LTspice-style examples
- [ ] Add end-to-end ngspice smoke tests for directive blocks
