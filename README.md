# VioraEDA 

**VioraEDA** is a high-performance, open-source SPICE-class circuit simulator with a modern, interactive schematic editor. Designed for speed and visual excellence, it bridges the gap between traditional simulation engines and modern user interfaces.


## Key Features

- **High-Performance Simulation**: Native Sparse LU solver and Verilog-A JIT compilation for lightning-fast analysis.
- **Modern Interactive UI**: A stunning, hardware-accelerated schematic editor with smooth animations and professional aesthetics.
- **Real-Time Oscilloscope**: Integrated CRT-style analog oscilloscope for live signal visualization.
- **Automatic Probing**: LTspice-inspired hover-probing system for effortless voltage measurements.
- **Gemini AI Co-Pilot**: Integrated AI assistant to help you debug ERC violations and generate FluxScript snippets.
- **Hierarchical Design**: Full support for `.SUBCKT` expansion and complex hierarchical schematics.
- **Virtual Instruments**: A full suite of virtual tools including Logic Analyzers, Voltmeters, Ammeters, and Wattmeters.
- **ML Dataset API**: Local HTTP API for batch simulation and rich JSONL dataset export for AI training workflows.

## Tech Stack

- **Core**: C++20 / Qt6
- **Graphics**: Qt Graphics View Framework
- **Sim Engine**: Custom Sparse LU Solver / ngspice compatible
- **AI**: Google Gemini API Integration

## Getting Started

### Prerequisites

- **Qt 6.5+**
- **CMake 3.16+**
- **C++20 Compiler** (GCC 10+, Clang 12+, or MSVC 2019+)

### Simulation Engine Setup

Before building VioraEDA, ensure that the custom **ngspice-shared** library is installed to your system:

```bash
cd path/to/ngspice/release
sudo make install
```

### Installation

1. **Clone the repository**:
   ```bash
   git clone https://github.com/Janadasroor/VioraEDA.git
   cd VioraEDA
   ```

2. **Configure and build**:
   ```bash
   mkdir build && cd build
   cmake ..
   make -j$(nproc)
   ```

3. **Run the application**:
   ```bash
   ./VioraEDA
   ```

## Workflow

1. **Draw**: Build your circuit using the extensive component library.
2. **Probing**: Place probes or simply hover over wires while simulating to see real-time waveforms.
3. **Analyze**: Use the integrated oscilloscope and measurement tools to verify your design.
4. **Automate**: Write FluxScripts to automate complex simulation tasks.

## ML Dataset API

VioSpice now includes a Python HTTP API for ML-oriented simulation pipelines. It can run single jobs or large concurrent batches and emit dataset records containing netlists, waveforms, stats, measures, and custom labels.

```bash
python3 python/scripts/ml_dataset_api.py --port 8787
```

Documentation: [docs/ML_DATASET_API.md](docs/ML_DATASET_API.md)
ML engineer guide: [docs/ML_ENGINEER_GUIDE.md](docs/ML_ENGINEER_GUIDE.md)
Examples: [examples/ml_api/README.md](examples/ml_api/README.md)

## License

This project is licensed under the Apache-2.0 license - see the [LICENSE](LICENSE) file for details.

---
*Built with by the Janadasroor Team.*
