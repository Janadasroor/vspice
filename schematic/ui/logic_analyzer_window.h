#ifndef LOGIC_ANALYZER_WINDOW_H
#define LOGIC_ANALYZER_WINDOW_H

#include <QMainWindow>
#include <QStringList>
#include "virtual_instruments.h"
#include "../../simulator/core/sim_engine.h"

class QDoubleSpinBox;

class LogicAnalyzerWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit LogicAnalyzerWindow(const QString& title, QWidget* parent = nullptr);
    
    void setChannels(const QStringList& nets);
    void updateData(const SimResults& results);
    void clear();

    QString instrumentId() const { return m_id; }
    void setInstrumentId(const QString& id) { m_id = id; }

signals:
    void windowClosing(const QString& id);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    LogicAnalyzerWidget* m_la;
    QStringList m_targetNets;
    QString m_id;
    
    QDoubleSpinBox* m_timeDivSpin;
    QDoubleSpinBox* m_hPosSpin;
};

#endif // LOGIC_ANALYZER_WINDOW_H
