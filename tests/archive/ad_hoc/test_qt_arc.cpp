#include <QApplication>
#include <QPainter>
#include <QPixmap>
#include <iostream>
#include <cmath>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QPixmap pix(200, 200);
    pix.fill(Qt::white);
    QPainter p(&pix);
    p.translate(100, 100);
    p.setPen(QPen(Qt::red, 2));

    double cx = -5.08, cy = 13.71;
    double r = 5.303;

    double ang1 = -16.8;    // Qt mathematical angle mapping
    double ang2 = -90.0;
    double ang3 = -163.2;

    if (ang1 < 0) ang1 += 360.0;
    if (ang2 < 0) ang2 += 360.0;
    if (ang3 < 0) ang3 += 360.0;

    double span = -146.6;

    QRectF rect(cx - r, cy - r, 2*r, 2*r);
    p.drawArc(rect, int(ang1 * 16.0), int(span * 16.0));
    
    // Test the second arc
    double cx2 = 5.08, cy2 = 16.51;
    double r2 = 5.236;
    double ang1_2 = -(-16.0); // 16.0
    double ang2_2 = -(-90.0); // 90.0
    double ang3_2 = -( -164.0); // 164.0

    if (ang1_2 < 0) ang1_2 += 360.0;
    if (ang2_2 < 0) ang2_2 += 360.0;
    if (ang3_2 < 0) ang3_2 += 360.0;
    
    double span2 = 148.0;
    
    QRectF rect2(cx2 - r2, cy2 - r2, 2*r2, 2*r2);
    p.setPen(QPen(Qt::blue, 2));
    p.drawArc(rect2, int(ang1_2 * 16.0), int(span2 * 16.0));

    p.end();
    pix.save("qt_arc_test.png");
    std::cout << "Done! Bounding Boxes:\n";
    std::cout << "1: " << rect.x() << " " << rect.y() << " " << rect.width() << " " << rect.height() << "\n";
    std::cout << "2: " << rect2.x() << " " << rect2.y() << " " << rect2.width() << " " << rect2.height() << "\n";
    return 0;
}
