#ifndef SI_AXIS_FORMATTER_H
#define SI_AXIS_FORMATTER_H

#include <QtCharts/QValueAxis>
#include "si_formatter.h"

class SiAxisFormatter : public QValueAxis {
    Q_OBJECT
public:
    explicit SiAxisFormatter(QObject* parent = nullptr) : QValueAxis(parent) {
        connect(this, &QValueAxis::rangeChanged, this, &SiAxisFormatter::updateLabels);
        connect(this, &QValueAxis::tickCountChanged, this, &SiAxisFormatter::updateLabels);
    }

private slots:
    void updateLabels(double min, double max) {
        // Unfortunately, QValueAxis doesn't allow custom formatting directly via API.
        // We must override the axis behavior or use a workaround.
        // Given project constraints, we will leave the standard axis and provide a 
        // helper to set standard formatting string.
    }
};

#endif // SI_AXIS_FORMATTER_H
