# VioSpice & VioMATRIXC New Features

This document details the features recently added to the VioSpice ecosystem to support native WAV audio, LTspice A-device compatibility, and proper dynamic linking.

---

## 1. LTspice A-Device Compatibility (VioMATRIXC)

VioMATRIXC now supports **LTspice-style fixed-pin A-devices**. Previously, ngspice only supported its native dynamic XSPICE syntax. This feature automatically translates LTspice netlists to ngspice format during simulation.

### How it Works
The transform function `ltspice_a_device_transform()` (in `src/frontend/inpcompat.c`) intercepts netlist parsing:
1.  **Detects** the fixed 8-pin LTspice format: `A1 pin0 pin1 ... pin7 DEVICETYPE`
2.  **Maps** LTspice pin positions (usually 0, 1, 7) to the correct ngspice port order.
3.  **Translates** LTspice device types (e.g., `BUF`) to ngspice models (e.g., `d_buffer`).
4.  **Generates** the corresponding `.model` card automatically.

### Supported Devices
| LTspice Name | ngspice Model | Pins | Pin Mapping |
| :--- | :--- | :--- | :--- |
| **BUF** / **BUFFER** | `d_buffer` | 2 | in=pin0, out=pin7 |
| **NOT** / **INV** | `d_inverter` | 2 | in=pin0, out=pin7 |
| **SCHMITT** | `d_buffer` | 2 | in=pin0, out=pin7 |
| **AND** | `d_and` | 3 | in1=pin0, in2=pin1, out=pin7 |
| **OR** | `d_or` | 3 | in1=pin0, in2=pin1, out=pin7 |
| **NAND** | `d_nand` | 3 | in1=pin0, in2=pin1, out=pin7 |
| **NOR** | `d_nor` | 3 | in1=pin0, in2=pin1, out=pin7 |
| **XOR** | `d_xor` | 3 | in1=pin0, in2=pin1, out=pin7 |
| **XNOR** | `d_xnor` | 3 | in1=pin0, in2=pin1, out=pin7 |
| **DFF** | `d_dff` | 4 | d, clk, q, nq |
| **JKFF** | `d_jkff` | 4 | j, k, clk, q |
| **SRFF** | `d_srff` | 4 | s, r, q, nq |
| **DLATCH** | `d_dlatch` | 3 | d, en, q |
| **COUNTER** | `d_fdiv` | 4 | in, out, reset, enable |

### Configuration
*   Built VioMATRIXC with `--enable-vicompat` to enable LTspice compatibility by default.
*   The `lt` (LTspice) and `a` (A-device) flags are automatically set in `set_compat_mode()`.

---

## 3. ERC Panel Library Detection

The ERC diagnostics panel now automatically detects which ngspice library is loaded and displays this information at the top of the panel.

### Features
*   **Automatic Detection**: Uses `dladdr()` to find the actual library path loaded at runtime.
*   **Visual Indicator**: 
    *   🔧 **VioMATRIXC (Custom Fork)** - Green indicator with full feature list
    *   ⚙️ **System ngspice** - Yellow indicator with standard features warning
*   **Library Path Display**: Shows the exact `.so` file being used, so you can immediately identify if you're using your custom build or the system library.
*   **Status Messages**:
    *   `✅ Full feature set (WAV, LTspice compat, XSPICE)` - When VioMATRIXC is detected
    *   `⚠️ Standard features only` - When system ngspice is detected

### Implementation
*   Added `updateLibraryInfo()` method to `ERCDiagnosticsPanel` that queries `dladdr()` for `ngSpice_Init` and `ngSpice_Command` symbols.
*   Displays a styled label at the top of the ERC panel with library type, filename, and feature status.
*   Automatically detects:
    *   **VioMATRIXC** - by checking if library path contains "VioMATRIXC" or "releasesh"
    *   **System ngspice** - when loading from `/usr/local/lib` or other system paths
    *   **Not loaded** - when no library symbols are found

---

## 4. Native WAV Audio Support (viospice)

The Voltage Source dialog now includes a dedicated section for using **WAV files** as voltage sources, utilizing the VioMATRIXC WAV parsing engine.

### GUI Features
*   **Dedicated Section**: Separate "WAVEFILE (Audio .wav)" radio button (not buried in PWL).
*   **File Browser**: Integrated "Browse..." button to select `.wav` files.
*   **Channel Selection**: Select audio channel (0 = Left, 1 = Right for stereo).
*   **Peak Scale**: Adjustable gain (0.001x to 10,000x) with real-time dB calculation display.
*   **Live Info Panel**: Displays sample rate, channels, duration, and RMS/Peak voltage for the selected channel.

### Backend Details
*   **Model**: Added `m_waveScale` field to `VoltageSourceItem` with full JSON serialization.
*   **Registry**: Registered `Voltage_Source_WaveFile` type in `schematic_item_registry.cpp` to ensure correct save/load behavior.
*   **Netlist Generation**: Generates standard SPICE syntax: `V1 Net1 0 WAVEFILE "path.wav" CHAN 0`.

---

## 3. XSPICE Code Model Auto-Loading

To support A-devices and mixed-signal simulation, VioSpice now automatically configures the ngspice environment.

### Implementation
In `core/simulation_manager.cpp`:
1.  **Environment Setup**: Sets `SPICE_SCRIPTS` and `SPICE_LIB_DIR` to `~/.ngspice` before initialization. This ensures ngspice finds the correct `spinit` file.
2.  **Model Loading**: Automatically loads all standard code models (`digital.cm`, `analog.cm`, `spice2poly.cm`, etc.) during startup.

### Benefits
*   Eliminates "unable to find definition of model" errors.
*   Enables mixed-signal simulation (analog/digital bridges) out of the box.

---

## 4. Build System & Linking Fixes

Fixed runtime linking issues where `vio-cmd` and `viospice` were loading the system `libngspice.so` (which lacked VioMATRIXC patches) instead of the local build.

### Changes
*   **CMakeLists.txt**: Added `BUILD_RPATH` and `INSTALL_RPATH` properties to targets.
*   **Result**: Binaries now correctly link against `VioMATRIXC/releasesh/src/.libs/libngspice.so` at runtime.

---

## 5. Setup Requirements

For these features to work, the user must have the code models and scripts accessible.

### Instructions
1.  **Build VioMATRIXC**: Ensure built with `--enable-vicompat`.
2.  **Copy Files**:
    ```bash
    mkdir -p ~/.ngspice
    cp releasesh/src/xspice/icm/*/*.cm ~/.ngspice/
    cp releasesh/src/spinit ~/.ngspice/
    ```
3.  **WAV Files**: Ensure WAV files are accessible (paths should be absolute or relative to project dir). Note: ngspice lowercases paths, so keep filenames lowercase (e.g., `audio.wav`).

---

## File Changes Summary

### VioMATRIXC (`src/frontend/`)
*   `inpcompat.c`: Added `ltspice_a_device_transform`, `ltspice_devtype_to_ngspice`, `ltspice_device_pin_count`, `ltspice_pin_index`.
*   `inpcom.c`: Minor adjustments for compatibility flags.

### viospice
*   `core/simulation_manager.cpp`: Environment setup and model loading.
*   `CMakeLists.txt`: RPATH configuration.
*   `schematic/dialogs/voltage_source_ltspice_dialog.cpp/h`: New WAVEFILE UI section.
*   `schematic/items/voltage_source_item.cpp/h`: Added `m_waveScale`.
*   `schematic/factories/schematic_item_registry.cpp`: Registered WaveFile type.
