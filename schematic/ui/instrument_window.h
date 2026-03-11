#ifndef INSTRUMENT_WINDOW_H
#define INSTRUMENT_WINDOW_H

#include <QMainWindow>
#include <QStringList>
#include "virtual_instruments.h"
#include "../../simulator/core/sim_engine.h"

/**
 * @brief Dedicated floating window for a schematic instrument (Oscilloscope, etc.)
 */
class QComboBox;

class InstrumentWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit InstrumentWindow(const QString& title, QWidget* parent = nullptr);
    
    void setChannels(const QStringList& nets);
    void updateData(const SimResults& results);
    void setTimeCursor(double t);
    void clear();

    QString instrumentId() const { return m_id; }
    void setInstrumentId(const QString& id) { m_id = id; }

private:
    void updateMeasurementLabels();

    OscilloscopeWidget* m_scope;
    QStringList m_targetNets;
    QString m_id;
    QComboBox* m_chSelect;
    
    // Measurement displays
    class QLabel* m_minValLabel;
    class QLabel* m_maxValLabel;
    class QLabel* m_pkpkValLabel;
    class QLabel* m_rmsValLabel;
    class QLabel* m_avgValLabel;
    
    struct Measurements {
        double min = 0.0;
        double max = 0.0;
        double rms = 0.0;
        double avg = 0.0;
        bool valid = false;
    };
    QMap<QString, Measurements> m_measurements;
};

#endif // INSTRUMENT_WINDOW_H
