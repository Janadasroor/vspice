import math

# Qt properties:
# X increases to Right. Y increases DOWN.
# drawArc angles: 0 is Right (3 o'clock). 90 is UP (12 o'clock).
# Span angle: Positive is CCW (Right->Up->Left->Down). Negative is CW.

# KiCad coordinates
# X increases to Right. Y increases DOWN after flipped by importer.
KICAD_SCALE = 10.0
# KiCad coords in file: start(0, -1.524), mid(-0.508, -1.902), end(-1.016, -1.524)
# Importer parses it and flips Y!
x1 = 0 * KICAD_SCALE
y1 = -(-1.524) * KICAD_SCALE   # 15.24
x2 = -0.508 * KICAD_SCALE
y2 = -(-1.902) * KICAD_SCALE   # 19.02
x3 = -1.016 * KICAD_SCALE
y3 = -(-1.524) * KICAD_SCALE   # 15.24

D = 2 * (x1*(y2 - y3) + x2*(y3 - y1) + x3*(y1 - y2))
cx = ((x1*x1 + y1*y1)*(y2 - y3) + (x2*x2 + y2*y2)*(y3 - y1) + (x3*x3 + y3*y3)*(y1 - y2)) / D
cy = ((x1*x1 + y1*y1)*(x3 - x2) + (x2*x2 + y2*y2)*(x1 - x3) + (x3*x3 + y3*y3)*(x2 - x1)) / D
# cx = -5.08, cy = 13.716

# To map to Qt angles, Qt 0 is right(+X), Qt 90 is UP(-Y)
# Standard atan2(Y, X) gives: 0 is Right, +90 is +Y (DOWN)
# We want: 0 is Right, +90 is UP (-Y)
# So we use atan2(-Y, X)
ang1 = math.degrees(math.atan2(cy - y1, x1 - cx))
ang2 = math.degrees(math.atan2(cy - y2, x2 - cx))
ang3 = math.degrees(math.atan2(cy - y3, x3 - cx))

if ang1 < 0: ang1 += 360.0
if ang2 < 0: ang2 += 360.0
if ang3 < 0: ang3 += 360.0

ccw12 = ang2 - ang1; 
if ccw12 < 0: ccw12 += 360.0
ccw23 = ang3 - ang2; 
if ccw23 < 0: ccw23 += 360.0
cw12  = ang1 - ang2; 
if cw12 < 0: cw12 += 360.0
cw23  = ang2 - ang3; 
if cw23 < 0: cw23 += 360.0

if (ccw12 + ccw23) < 360.0:
    span = ccw12 + ccw23
else:
    span = -(cw12 + cw23)

print(f"Arc 1 (Left): start={ang1:.1f}, mid={ang2:.1f}, end={ang3:.1f}, span={span:.1f}")
# start=343.3, mid=270, end=196.7, span=-146.6
# In Qt, 343.3 is Bottom-Right (near 360/0, dipping slightly below).
# 270 is Bottom (6 o'clock). 196.7 is Bottom-Left.
# Span is CW (-146.6). This means it draws Bottom-Right -> Bottom -> Bottom-Left.
# This visibly forms a U SHAPE (bulges DOWN).

# Now for Arc 2 (Right)
x1 = 0 * KICAD_SCALE
y1 = -(-1.524) * KICAD_SCALE  # 15.24
x2 = 0.508 * KICAD_SCALE
y2 = -(-1.1274) * KICAD_SCALE # 11.274
x3 = 1.016 * KICAD_SCALE
y3 = -(-1.524) * KICAD_SCALE  # 15.24

D = 2 * (x1*(y2 - y3) + x2*(y3 - y1) + x3*(y1 - y2))
cx = ((x1*x1 + y1*y1)*(y2 - y3) + (x2*x2 + y2*y2)*(y3 - y1) + (x3*x3 + y3*y3)*(y1 - y2)) / D
cy = ((x1*x1 + y1*y1)*(x3 - x2) + (x2*x2 + y2*y2)*(x1 - x3) + (x3*x3 + y3*y3)*(x2 - x1)) / D
# cy = 16.51
ang1 = math.degrees(math.atan2(cy - y1, x1 - cx))
ang2 = math.degrees(math.atan2(cy - y2, x2 - cx))
ang3 = math.degrees(math.atan2(cy - y3, x3 - cx))

if ang1 < 0: ang1 += 360.0
if ang2 < 0: ang2 += 360.0
if ang3 < 0: ang3 += 360.0

ccw12 = ang2 - ang1; 
if ccw12 < 0: ccw12 += 360.0
ccw23 = ang3 - ang2; 
if ccw23 < 0: ccw23 += 360.0
cw12  = ang1 - ang2; 
if cw12 < 0: cw12 += 360.0
cw23  = ang2 - ang3; 
if cw23 < 0: cw23 += 360.0

if (ccw12 + ccw23) < 360.0:
    span = ccw12 + ccw23
else:
    span = -(cw12 + cw23)

print(f"Arc 2 (Right): start={ang1:.1f}, mid={ang2:.1f}, end={ang3:.1f}, span={span:.1f}")
# start=194.0, mid=90.0, end=346.0, span=-208.1
# In Qt, 194 is Bottom-Left. 90 is Top. 346 is Bottom-Right.
# Span is CW (-208.1). From 194 -> Top(90) -> 346.
# This forms an ^ SHAPE (bulges UP).

