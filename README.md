# VIOSpice 🚀

**VIOSpice** is a high-performance, open-source SPICE-class circuit simulator with a modern, interactive schematic editor. Designed for speed and visual excellence, it bridges the gap between traditional simulation engines and modern user interfaces.

![VIOSpice Banner](assets/v-letter.png) <!-- Assuming this exists or will be added -->

## Key Features

- ⚡ **High-Performance Simulation**: Native Sparse LU solver and Verilog-A JIT compilation for lightning-fast analysis.
- 🎨 **Modern Interactive UI**: A stunning, hardware-accelerated schematic editor with smooth animations and professional aesthetics.
- 🔬 **Real-Time Oscilloscope**: Integrated CRT-style analog oscilloscope for live signal visualization.
- 🔴 **Automatic Probing**: LTspice-inspired hover-probing system for effortless voltage measurements.
- 🤖 **Gemini AI Co-Pilot**: Integrated AI assistant to help you debug ERC violations and generate FluxScript snippets.
- 📦 **Hierarchical Design**: Full support for `.SUBCKT` expansion and complex hierarchical schematics.
- 🛠️ **Virtual Instruments**: A full suite of virtual tools including Logic Analyzers, Voltmeters, Ammeters, and Wattmeters.

## Tech Stack

- **Core**: C++20 / Qt6
- **Graphics**: Qt Graphics View Framework
- **Sim Engine**: Custom Sparse LU Solver / ngspice compatible
- **Scripting**: FluxScript (Custom DSL)
- **AI**: Google Gemini API Integration

## Getting Started

### Prerequisites

- **Qt 6.5+**
- **CMake 3.16+**
- **C++20 Compiler** (GCC 10+, Clang 12+, or MSVC 2019+)

### Installation

1. **Clone the repository**:
   ```bash
   git clone https://github.com/[your-username]/viospice.git
   cd viospice
   ```

2. **Configure and build**:
   ```bash
   mkdir build && cd build
   cmake ..
   make -j$(nproc)
   ```

3. **Run the application**:
   ```bash
   ./viospice
   ```

## Workflow

1. **Draw**: Build your circuit using the extensive component library.
2. **Probing**: Place probes or simply hover over wires while simulating to see real-time waveforms.
3. **Analyze**: Use the integrated oscilloscope and measurement tools to verify your design.
4. **Automate**: Write FluxScripts to automate complex simulation tasks.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---
*Built with ❤️ by the VioraEDA Team.*
