#include "simulation_log_dialog.h"
#include "theme_manager.h"
#include <QVBoxLayout>
#include <QPushButton>
#include <QFont>

SimulationLogDialog::SimulationLogDialog(const QString& logText, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Ngspice Detailed Simulation Log");
    resize(800, 600);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);
    
    QTextEdit* logEdit = new QTextEdit(this);
    logEdit->setReadOnly(true);
    logEdit->setPlainText(logText);
    logEdit->setFont(QFont("Monospace", 10));
    
    layout->addWidget(logEdit);
    
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    QPushButton* closeBtn = new QPushButton("Close", this);
    closeBtn->setMinimumWidth(100);
    closeBtn->setMinimumHeight(30);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(closeBtn);
    layout->addLayout(btnLayout);

    PCBTheme* theme = ThemeManager::theme();
    if (theme) {
        theme->applyToWidget(this);
        logEdit->setStyleSheet(QString(
            "QTextEdit { background-color: %1; color: %2; border: 1px solid %3; padding: 10px; }"
        ).arg(theme->panelBackground().name(), 
              theme->textColor().name(), 
              theme->panelBorder().name()));
              
        closeBtn->setStyleSheet(QString(
            "QPushButton { background-color: %1; color: white; border-radius: 4px; font-weight: bold; }"
            "QPushButton:hover { background-color: %2; }"
        ).arg(theme->accentColor().name(), theme->accentColor().lighter(110).name()));
    }
}
