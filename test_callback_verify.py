#!/usr/bin/env python3
"""
Verify Phase 2 callback actually changes circuit behavior.
We measure V(out) before and after toggling to prove the callback works.
"""

import ctypes
import os
import time

lib_path = "/home/jnd/cpp_projects/VioMATRIXC/releasesh/src/.libs/libngspice.so.0"
ng = ctypes.CDLL(lib_path)

ng.ngSpice_Init.argtypes = [ctypes.c_void_p] * 7
ng.ngSpice_Init.restype = ctypes.c_int
ng.ngSpice_Command.argtypes = [ctypes.c_char_p]
ng.ngSpice_Command.restype = ctypes.c_int

GetSwitchData = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.POINTER(ctypes.c_double), ctypes.c_char_p, ctypes.c_int, ctypes.c_void_p)

# Circuit: V1--RSW1--C1--GND
# RSW1 controls how fast C1 charges
# If RSW1=1e12 (open), C1 barely charges
# If RSW1=0.001 (closed), C1 charges to 5V quickly
netlist = """* Callback verification test
V1 in 0 DC 5
RSW1 in out 1e12
C1 out 0 1u
.TRAN 1ms 10ms
.PRINT TRAN V(out)
.END
"""

netlist_file = "/tmp/test_callback_verify.cir"
with open(netlist_file, 'w') as f:
    f.write(netlist)

# Start with switch OPEN (high resistance)
switch_resistance = 1e12

@GetSwitchData
def switch_callback(resistance_ptr, name, ident, userdata):
    global switch_resistance
    name_str = name.decode('utf-8') if isinstance(name, bytes) else name
    if name_str.startswith("RSW"):
        resistance_ptr[0] = switch_resistance
        return 0
    return -1

ng.ngSpice_Init_SwitchData.argtypes = [GetSwitchData, ctypes.c_void_p]
ng.ngSpice_Init_SwitchData.restype = ctypes.c_int

print("=" * 60)
print("Phase 2 Verification: Callback changes circuit behavior")
print("=" * 60)
print()

ng.ngSpice_Init(None, None, None, None, None, None, None)
ng.ngSpice_Init_SwitchData(switch_callback, None)
ng.ngSpice_Command(f"source {netlist_file}".encode())
ng.ngSpice_Command(b"bg_run")

# Wait for some simulation time with switch OPEN
print("[1] Running with switch OPEN (RSW1=1e12)...")
time.sleep(0.1)

# Check voltage - should be near 0 (capacitor barely charging through 1e12)
ng.ngSpice_Command(b"print tran")

# Now close the switch
print("[2] Closing switch (RSW1=0.001)...")
switch_resistance = 0.001

# Wait for capacitor to charge through 0.001 ohm
print("[3] Running with switch CLOSED (RSW1=0.001)...")
time.sleep(0.2)

# Check voltage - should be near 5V now
ng.ngSpice_Command(b"print tran")

# Check raw file
raw_path = "/tmp/test_callback_verify.raw"
if os.path.exists(raw_path):
    size = os.path.getsize(raw_path)
    print(f"\nRaw file: {raw_path} ({size} bytes)")
    # Try to read it
    with open(raw_path, 'rb') as f:
        header = f.read(200).decode('utf-8', errors='ignore')
        print("Raw file header preview:")
        for line in header.split('\n')[:10]:
            print(f"  {line}")
else:
    print(f"\nRaw file not found")

print()
print("=" * 60)
print("If V(out) went from ~0V to ~5V, the callback is working!")
print("=" * 60)
