import sys
from PyQt5.QtWidgets import QApplication, QWidget
from PyQt5.QtGui import QPainter, QPen, QColor
from PyQt5.QtCore import Qt, QRectF
import math

class W(QWidget):
    def paintEvent(self, e):
        p = QPainter(self)
        p.translate(100, 100)
        
        KICAD_SCALE = 10.0
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

        ang1 = math.degrees(math.atan2(cy - y1, x1 - cx))
        ang2 = math.degrees(math.atan2(cy - y2, x2 - cx))
        ang3 = math.degrees(math.atan2(cy - y3, x3 - cx))
        
        if ang1 < 0: ang1 += 360.0
        if ang2 < 0: ang2 += 360.0
        if ang3 < 0: ang3 += 360.0

        ccw12 = ang2 - ang1
        if ccw12 < 0: ccw12 += 360.0
        ccw23 = ang3 - ang2
        if ccw23 < 0: ccw23 += 360.0
        cw12  = ang1 - ang2
        if cw12 < 0: cw12 += 360.0
        cw23  = ang2 - ang3
        if cw23 < 0: cw23 += 360.0

        span = 0
        if (ccw12 + ccw23) < 360.0:
            span = ccw12 + ccw23
        else:
            span = -(cw12 + cw23)

        rect = QRectF(cx - r, cy - r, 2*r, 2*r)
        p.setPen(QPen(Qt.red, 2))
        p.drawArc(rect, int(ang1 * 16), int(span * 16))
        
app = QApplication(sys.argv)
w = W()
w.resize(200, 200)
pix = w.grab()
pix.save("test_qt_arc.png")
