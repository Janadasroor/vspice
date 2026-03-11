#include <iostream>
#include <cmath>
using namespace std;

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
    double r = std::hypot(x1 - cx, y1 - cy);

    cout << "cx: " << cx << ", cy: " << cy << ", r: " << r << "\n";
    cout << "rect.y: " << cy - r << "\n";
    
    // Test the second arc
    x1 = 0 * KICAD_SCALE;
    y1 = -(-1.524) * KICAD_SCALE;
    x2 = 0.508 * KICAD_SCALE;
    y2 = -(-1.1274) * KICAD_SCALE;
    x3 = 1.016 * KICAD_SCALE;
    y3 = -(-1.524) * KICAD_SCALE;

    D = 2 * (x1*(y2 - y3) + x2*(y3 - y1) + x3*(y1 - y2));
    cx = ((x1*x1 + y1*y1)*(y2 - y3) + (x2*x2 + y2*y2)*(y3 - y1) + (x3*x3 + y3*y3)*(y1 - y2)) / D;
    cy = ((x1*x1 + y1*y1)*(x3 - x2) + (x2*x2 + y2*y2)*(x1 - x3) + (x3*x3 + y3*y3)*(x2 - x1)) / D;
    r = std::hypot(x1 - cx, y1 - cy);

    cout << "Arc 2:\ncx: " << cx << ", cy: " << cy << ", r: " << r << "\n";
    cout << "rect.y: " << cy - r << "\n";
    
    return 0;
}
