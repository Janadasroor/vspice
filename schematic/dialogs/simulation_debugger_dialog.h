#ifndef SIMULATION_DEBUGGER_DIALOG_H
#define SIMULATION_DEBUGGER_DIALOG_H

#include <QDialog>
#include <QStringList>

class QTableWidget;
class QPushButton;

class SimulationDebuggerDialog : public QDialog {
    Q_OBJECT

public:
    explicit SimulationDebuggerDialog(const QStringList& diagnostics, QWidget* parent = nullptr);

    bool shouldRunAnyway() const { return m_runAnyway; }

private:
    void setupUi();
    void populateDiagnostics(const QStringList& diagnostics);

    QTableWidget* m_table;
    QPushButton* m_btnRun;
    QPushButton* m_btnAbort;
    bool m_runAnyway = false;
};

#endif // SIMULATION_DEBUGGER_DIALOG_H
