#ifndef SIMULATION_LOG_DIALOG_H
#define SIMULATION_LOG_DIALOG_H

#include <QDialog>
#include <QTextEdit>

class SimulationLogDialog : public QDialog {
    Q_OBJECT
public:
    explicit SimulationLogDialog(const QString& logText, QWidget* parent = nullptr);
};

#endif // SIMULATION_LOG_DIALOG_H
