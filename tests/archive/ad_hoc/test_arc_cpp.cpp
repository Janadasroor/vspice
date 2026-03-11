#include <iostream>
#include <cmath>

int main() {
    double KICAD_SCALE = 10.0;
    double x1 = 0 * KICAD_SCALE;
    double y1 = -(-1.524) * KICAD_SCALE;
    double x2 = -0.508 * KICAD_SCALE;
    double y2 = -(-1.902) * KICAD_SCALE;
    double x3 = -1.016 * KICAD_SCALE;
    double y3 = -(-1.524) * KICAD_SCALE;

    double D = 2 * (x1*(y2 - y3) + x2*(y3 - y1) + x3*(y1 - y2));
    double cx = ((x1*x1 + y1*y1)*(y2 - y3) + (x2*x2 + y2*y2)*(y3 - y1) + (x3*x3 + y3*y3)*(y1 - y2)) / D;
    double cy = ((x1*x1 + y1*y1)*(x3 - x2) + (x2*x2 + y2*y2)*(x1 - x3) + (x3*x3 + y3*y3)*(x2 - x1)) / D;

    double ang1 = std::atan2(y1 - cy, x1 - cx) * 180.0 / M_PI;
    double ang2 = std::atan2(y2 - cy, x2 - cx) * 180.0 / M_PI;
    double ang3 = std::atan2(y3 - cy, x3 - cx) * 180.0 / M_PI;

    ang1 = -ang1;
    ang2 = -ang2;
    ang3 = -ang3;

    if (ang1 < 0) ang1 += 360.0;
    if (ang2 < 0) ang2 += 360.0;
    if (ang3 < 0) ang3 += 360.0;

    double ccw12 = ang2 - ang1; if (ccw12 < 0) ccw12 += 360.0;
    double ccw23 = ang3 - ang2; if (ccw23 < 0) ccw23 += 360.0;
    double cw12  = ang1 - ang2; if (cw12 < 0) cw12 += 360.0;
    double cw23  = ang2 - ang3; if (cw23 < 0) cw23 += 360.0;

    double span = 0;
    if (ccw12 + ccw23 < 360.0) {
        span = ccw12 + ccw23;
    } else {
        span = -(cw12 + cw23);
    }
    
    int startIdx = (int)(ang1 * 16.0);
    int spanIdx = (int)(span * 16.0);
    
    std::cout << "CCW12: " << ccw12 << ", CCW23: " << ccw23 << "\n";
    std::cout << "CW12: " << cw12 << ", CW23: " << cw23 << "\n";
    std::cout << "Start (deg): " << ang1 << ", Span (deg): " << span << "\n";
    std::cout << "Start (1/16): " << startIdx << ", Span (1/16): " << spanIdx << "\n";
    return 0;
}
