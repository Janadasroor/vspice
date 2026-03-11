import math

def test_arc2():
    KICAD_SCALE = 10.0
    x1 = 0 * KICAD_SCALE
    y1 = -(-1.524) * KICAD_SCALE
    x2 = 0.508 * KICAD_SCALE
    y2 = -(-1.1274) * KICAD_SCALE
    x3 = 1.016 * KICAD_SCALE
    y3 = -(-1.524) * KICAD_SCALE

    D = 2 * (x1*(y2 - y3) + x2*(y3 - y1) + x3*(y1 - y2))
    cx = ((x1*x1 + y1*y1)*(y2 - y3) + (x2*x2 + y2*y2)*(y3 - y1) + (x3*x3 + y3*y3)*(y1 - y2)) / D
    cy = ((x1*x1 + y1*y1)*(x3 - x2) + (x2*x2 + y2*y2)*(x1 - x3) + (x3*x3 + y3*y3)*(x2 - x1)) / D
    r = math.hypot(x1 - cx, y1 - cy)

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
        dir = "CCW"
    else:
        span = -(cw12 + cw23)
        dir = "CW"
        
    print(f"Arc 2: cx={cx}, cy={cy}, r={r}")
    print(f"ang1={ang1}, ang2={ang2}, ang3={ang3}")
    print(f"span={span}, dir={dir}")
    if (y2 < y1):
        print("Middle point is HIGHER (smaller Y), so it bows UPWARDS.")

test_arc2()
