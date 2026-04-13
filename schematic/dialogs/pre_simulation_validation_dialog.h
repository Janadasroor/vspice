#ifndef PRE_SIMULATION_VALIDATION_DIALOG_H
#define PRE_SIMULATION_VALIDATION_DIALOG_H

#include <QDialog>
#include <QStringList>
#include <QList>

class QTableWidget;
class QPushButton;
class QLabel;

struct ValidationIssue {
    enum Severity {
        Info,
        Warning,
        Error
    };

    Severity severity;
    QString message;
    QString category; // e.g., "ERC", "Preflight", "Configuration"
};

class PreSimulationValidationDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreSimulationValidationDialog(QWidget* parent = nullptr);
    
    void addIssue(const ValidationIssue& issue);
    void addIssues(const QList<ValidationIssue>& issues);
    
    // Returns true if user chose to proceed, false if they aborted
    bool shouldProceed() const { return m_proceed; }
    
    // Returns true if there are any errors (not just warnings)
    bool hasErrors() const;
    
    // Returns true if there are any warnings
    bool hasWarnings() const;

private:
    void setupUi();
    void populateIssues();
    void updateButtonStates();
    void copyIssuesToClipboard() const;

    QTableWidget* m_table;
    QPushButton* m_btnProceed;
    QPushButton* m_btnAbort;
    QPushButton* m_btnCopy;
    QLabel* m_summaryLabel;
    
    QList<ValidationIssue> m_issues;
    bool m_proceed = false;
};

#endif // PRE_SIMULATION_VALIDATION_DIALOG_H
