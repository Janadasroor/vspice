#include <QApplication>
#include <QWidget>
#include <QPainter>
#include <cmath>

class Widget : public QWidget {
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.translate(rect().center());
        
        // Let's reproduce the math
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

        double ang1 = std::atan2(cy - y1, x1 - cx) * 180.0 / M_PI;
        double ang2 = std::atan2(cy - y2, x2 - cx) * 180.0 / M_PI;
        double ang3 = std::atan2(cy - y3, x3 - cx) * 180.0 / M_PI;
        
        if (ang1 < 0) ang1 += 360.0;
        if (ang2 < 0) ang2 += 360.0;
        if (ang3 < 0) ang3 += 360.0;

        double ccw12 = ang2 - ang1; if (ccw12 < 0) ccw12 += 360.0;
        double ccw23 = ang3 - ang2; if (ccw23 < 0) ccw23 += 360.0;
        double cw12  = ang1 - ang2; if (cw12 < 0) cw12 += 360.0;
        double cw23  = ang2 - ang3; if (cw23 < 0) cw23 += 360.0;

        double span = 0;
        if (ccw12 + ccw23 < 360.0) span = ccw12 + ccw23;
        else span = -(cw12 + cw23);

        QRectF rect_arc(cx - r, cy - r, 2*r, 2*r);
        painter.setPen(QPen(Qt::red, 2));
        painter.drawArc(rect_arc, int(ang1 * 16), int(span * 16));
        
        painter.setPen(Qt::blue);
        painter.drawPoint(x1, y1);
        painter.drawPoint(x2, y2);
        painter.drawPoint(x3, y3);
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    Widget w;
    w.resize(200, 200);
    // w.show();
    QPixmap pix(200, 200);
    pix.fill(Qt::white);
    w.render(&pix);
    pix.save("test_arc.png");
    return 0; // app.exec();
}
