#include <QtCharts/QValueAxis>
#include <QString>
#include <cmath>

class SiAxis : public QValueAxis {
public:
    SiAxis(QObject* parent = nullptr) : QValueAxis(parent) {
        // We cannot override QValueAxis::labelFormat to return arbitrary strings (M, k, m, u),
        // because QValueAxis only supports printf-style formatting.
        // The standard QtCharts/QValueAxis implementation does NOT allow custom label text.
    }
};
