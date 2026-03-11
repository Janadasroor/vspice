#ifndef SIMULATION_SETUP_DIALOG_H
#define SIMULATION_SETUP_DIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QFormLayout>
#include <QJsonObject>
#include "../../simulator/core/sim_engine.h"

class SimulationSetupDialog : public QDialog {
    Q_OBJECT
public:
    explicit SimulationSetupDialog(QWidget* parent = nullptr);

    struct Config {
        SimAnalysisType type = SimAnalysisType::Transient;
        double stop = 10e-3;
        double step = 1e-6;
        double start = 0;
        double fStart = 10;
        double fStop = 1e6;
        int pts = 100;
        int rtIntervalMs = 50;
        double rtStep = 1e-3;
        bool native = true;

        QJsonObject toJson() const;
        static Config fromJson(const QJsonObject& obj);
    };

    Config getConfig() const;
    void setConfig(const Config& cfg);

private slots:
    void onAnalysisChanged(int index);

private:
    void setupUI();

    QComboBox* m_typeCombo;
    QLineEdit* m_param1;
    QLineEdit* m_param2;
    QLineEdit* m_param3;
    QCheckBox* m_nativeCheck;
};

#endif // SIMULATION_SETUP_DIALOG_H
