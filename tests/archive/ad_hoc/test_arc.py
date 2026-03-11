import math
import sys

def test_arc():
    KICAD_SCALE = 10.0
    x1 = 0 * KICAD_SCALE
    y1 = -(-1.524) * KICAD_SCALE
    x2 = -0.508 * KICAD_SCALE
    y2 = -(-1.902) * KICAD_SCALE
    x3 = -1.016 * KICAD_SCALE
    y3 = -(-1.524) * KICAD_SCALE

    y1_kicad = -1.524
    y2_kicad = -1.902
    print(f"y2_kicad = {y2_kicad}. It is LESS than y1_kicad={y1_kicad}.")
    print("In KICAD (Y points up), y=0 is origin, y=-1.524 is BELOW origin. y=-1.902 is FURTHER BELOW origin.")
    print("So the point goes from -1.524 DOWN to -1.902.")
    print("This means the arc dips DOWN. In a visual sense, it's a BOTTOM arc, like the letter U or v.")
test_arc()
