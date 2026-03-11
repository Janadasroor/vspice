import math

def test_arc():
    KICAD_SCALE = 10.0
    # True points on drawing area 
    # (Where Y increases downwards, typical Qt mapping setup up to now)
    x1 = 0 * KICAD_SCALE
    y1 = -(-1.524) * KICAD_SCALE
    x2 = -0.508 * KICAD_SCALE
    y2 = -(-1.902) * KICAD_SCALE
    x3 = -1.016 * KICAD_SCALE
    y3 = -(-1.524) * KICAD_SCALE

    D = 2 * (x1*(y2 - y3) + x2*(y3 - y1) + x3*(y1 - y2))
    cx = ((x1*x1 + y1*y1)*(y2 - y3) + (x2*x2 + y2*y2)*(y3 - y1) + (x3*x3 + y3*y3)*(y1 - y2)) / D
    cy = ((x1*x1 + y1*y1)*(x3 - x2) + (x2*x2 + y2*y2)*(x1 - x3) + (x3*x3 + y3*y3)*(x2 - x1)) / D
    r = math.hypot(x1 - cx, y1 - cy)

    ang1 = math.degrees(math.atan2(y1 - cy, x1 - cx))
    ang2 = math.degrees(math.atan2(y2 - cy, x2 - cx))
    ang3 = math.degrees(math.atan2(y3 - cy, x3 - cx))
    
    print(f"Original Visual Angles: A1={ang1:.1f}, A2={ang2:.1f}, A3={ang3:.1f}")
    
    ang1 = -ang1
    ang2 = -ang2
    ang3 = -ang3
    print(f"Inverted Angles (for Qt): A1={ang1:.1f}, A2={ang2:.1f}, A3={ang3:.1f}")

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
        
    print(f"Result Span: Start={ang1:.1f}, Span={span:.1f}, Direction={dir}")

test_arc()
