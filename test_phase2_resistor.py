#!/usr/bin/env python3
"""
Test Phase 2: Interactive switch callback with RESISTOR-based switches.
VioSpice generates switches as resistors (RSW1 n1 n2 1e12), not SW devices.
This test verifies the callback in resload.c works correctly.
"""

import ctypes
import os
import time

lib_path = "/home/jnd/cpp_projects/VioMATRIXC/releasesh/src/.libs/libngspice.so.0"
if not os.path.exists(lib_path):
    print(f"ERROR: Library not found: {lib_path}")
    exit(1)

ng = ctypes.CDLL(lib_path)

# Function signatures
ng.ngSpice_Init.argtypes = [ctypes.c_void_p] * 7
ng.ngSpice_Init.restype = ctypes.c_int
ng.ngSpice_Command.argtypes = [ctypes.c_char_p]
ng.ngSpice_Command.restype = ctypes.c_int

# GetSwitchData callback type
GetSwitchData = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.POINTER(ctypes.c_double), ctypes.c_char_p, ctypes.c_int, ctypes.c_void_p)

# Test circuit: RC with resistor-based switch (exactly like VioSpice generates)
# RSW1 is the "switch" - starts OPEN (1e12), we'll toggle it CLOSED (0.001) via callback
netlist = """* Phase 2 Test: Resistor-based interactive switch
V1 in 0 DC 5
R1 in out 1k
RSW1 out 0 1e12
C1 out 0 100p
.TRAN 0.1ms 50ms
.PRINT TRAN V(out)
.END
"""

netlist_file = "/tmp/test_resistor_switch.cir"
with open(netlist_file, 'w') as f:
    f.write(netlist)

# Track switch state - this simulates what VioSpice does when user clicks
switch_resistance = 1e12  # Start OPEN

@GetSwitchData
def switch_callback(resistance_ptr, name, ident, userdata):
    global switch_resistance
    name_str = name.decode('utf-8') if isinstance(name, bytes) else name
    # Check if this is our switch resistor
    if name_str.startswith("RSW") or name_str == "RSW1":
        resistance_ptr[0] = switch_resistance
        return 0  # Success - return override value
    return -1  # No override for other resistors

# Register callback
ng.ngSpice_Init_SwitchData.argtypes = [GetSwitchData, ctypes.c_void_p]
ng.ngSpice_Init_SwitchData.restype = ctypes.c_int

print("=" * 60)
print("Phase 2 Test: Resistor-based interactive switch callback")
print("=" * 60)
print()

# Initialize ngspice
print("[1] Initializing ngspice...")
rc = ng.ngSpice_Init(None, None, None, None, None, None, None)
print(f"    ngSpice_Init returned: {rc}")

# Register switch callback
print("[2] Registering switch callback...")
rc = ng.ngSpice_Init_SwitchData(switch_callback, None)
print(f"    ngSpice_Init_SwitchData returned: {rc}")

# Load netlist
print("[3] Loading netlist (RSW1=1e12, switch OPEN)...")
cmd = f"source {netlist_file}".encode()
rc = ng.ngSpice_Command(cmd)
print(f"    source returned: {rc}")

# Start background simulation
print("[4] Starting background simulation (bg_run)...")
rc = ng.ngSpice_Command(b"bg_run")
print(f"    bg_run returned: {rc}")

# Let it run briefly with switch OPEN
print("[5] Running 10ms with switch OPEN (RSW1=1e12)...")
time.sleep(0.05)

# Toggle switch CLOSED via callback (simulates user click)
print("[6] Toggling switch CLOSED via callback (RSW1=0.001)...")
switch_resistance = 0.001

# Let it run with switch CLOSED
print("[7] Running 10ms with switch CLOSED (RSW1=0.001)...")
time.sleep(0.1)

# Toggle back OPEN
print("[8] Toggling switch OPEN via callback (RSW1=1e12)...")
switch_resistance = 1e12

print("[9] Running until simulation completes...")
time.sleep(1)

# Check results
print("[10] Querying results...")
ng.ngSpice_Command(b"print tran")

# Check raw file
raw_path = "/tmp/test_resistor_switch.raw"
if os.path.exists(raw_path):
    size = os.path.getsize(raw_path)
    print(f"    Raw file exists: {raw_path} ({size} bytes)")
else:
    print(f"    Raw file not found at {raw_path}")

print()
print("=" * 60)
print("RESULT: Phase 2 test completed!")
print("=" * 60)
print()
print("If the callback is working, the switch resistance changed")
print("mid-simulation WITHOUT pausing or restarting.")
print()
print("In VioSpice GUI:")
print("  - Clicking a switch updates the callback map")
print("  - ngspice reads the new value at every timestep")
print("  - Waveform shows instant state change (zero latency)")
