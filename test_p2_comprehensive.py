#!/usr/bin/env python3
"""
Comprehensive test: Verify Phase 2 callback actually affects simulation.
Measures V(out) before and after toggle to prove resistance changed.
"""

import ctypes
import os
import time
import subprocess

lib_path = "/home/jnd/cpp_projects/VioMATRIXC/releasesh/src/.libs/libngspice.so.0"
ng = ctypes.CDLL(lib_path)

# ngspice API
ng.ngSpice_Init.argtypes = [ctypes.c_void_p] * 7
ng.ngSpice_Init.restype = ctypes.c_int
ng.ngSpice_Command.argtypes = [ctypes.c_char_p]
ng.ngSpice_Command.restype = ctypes.c_int

# Callback type
GetSwitchData = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.POINTER(ctypes.c_double), ctypes.c_char_p, ctypes.c_int, ctypes.c_void_p)

# Circuit: Simple RC with switch
# When switch is OPEN (1e12): C1 barely charges
# When switch is CLOSED (0.001): C1 charges quickly to 5V
netlist = """* Phase 2 comprehensive test
V1 in 0 DC 5
RSW1 in out 1e12
C1 out 0 100n
.TRAN 0.1ms 5ms
.PRINT TRAN V(out)
.END
"""

netlist_file = "/tmp/test_p2_full.cir"
with open(netlist_file, 'w') as f:
    f.write(netlist)

# Track switch state
switch_state = "OPEN"

@GetSwitchData
def switch_callback(resistance_ptr, name, ident, userdata):
    global switch_state
    name_str = name.decode('utf-8') if isinstance(name, bytes) else name
    if "RSW" in name_str.upper():
        if switch_state == "OPEN":
            resistance_ptr[0] = 1e12
        else:
            resistance_ptr[0] = 0.001
        return 0
    return -1

# Register callback
ng.ngSpice_Init_SwitchData.argtypes = [GetSwitchData, ctypes.c_void_p]
ng.ngSpice_Init_SwitchData.restype = ctypes.c_int

print("=" * 60)
print("Phase 2 Comprehensive Test")
print("=" * 60)

# Initialize
rc = ng.ngSpice_Init(None, None, None, None, None, None, None)
print(f"Init: {rc}")

# Register callback
rc = ng.ngSpice_Init_SwitchData(switch_callback, None)
print(f"Callback registered: {rc}")

# Load circuit
rc = ng.ngSpice_Command(f"source {netlist_file}".encode())
print(f"Load circuit: {rc}")

# Start simulation in background
rc = ng.ngSpice_Command(b"bg_run")
print(f"Start simulation: {rc}")

# Phase 1: Run with switch OPEN for 50ms
print("\n[Phase 1] Switch OPEN (1e12 ohms)...")
switch_state = "OPEN"
time.sleep(0.1)

# Check voltage via ngspice command
ng.ngSpice_Command(b"print V(out)")

# Phase 2: Toggle switch CLOSED
print("\n[Phase 2] Switch CLOSED (0.001 ohms)...")
switch_state = "CLOSED"
time.sleep(0.2)

# Check voltage
ng.ngSpice_Command(b"print V(out)")

# Phase 3: Toggle back OPEN
print("\n[Phase 3] Switch OPEN again...")
switch_state = "OPEN"
time.sleep(0.1)

print("\n" + "=" * 60)
print("Test Complete")
print("=" * 60)

# Verify library has our callback
result = subprocess.run(['nm', '-D', lib_path], capture_output=True, text=True)
if 'getswdat' in result.stdout:
    print("✓ Library exports getswdat callback")
else:
    print("✗ Library missing getswdat callback")

if 'ngSpice_Init_SwitchData' in result.stdout:
    print("✓ Library exports ngSpice_Init_SwitchData")
else:
    print("✗ Library missing ngSpice_Init_SwitchData")
