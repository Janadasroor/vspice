#include "simulation_debugger_dialog.h"
#include "theme_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QIcon>

SimulationDebuggerDialog::SimulationDebuggerDialog(const QStringList& diagnostics, QWidget* parent)
    : QDialog(parent)
{
    setupUi();
    populateDiagnostics(diagnostics);
}

void SimulationDebuggerDialog::setupUi() {
    setWindowTitle("Simulation Debugger");
    setMinimumSize(600, 400);
    
    // Dark theme styling
    QString style = "QDialog { background-color: #1e293b; color: #f8fafc; } "
                    "QLabel { color: #f8fafc; font-family: 'Inter'; font-size: 13px; } "
                    "QTableWidget { background-color: #0f172a; color: #cbd5e1; border: 1px solid #334155; gridline-color: #334155; font-family: 'Inter'; } "
                    "QHeaderView::section { background-color: #1e293b; color: #94a3b8; padding: 4px; border: 1px solid #334155; font-size: 11px; font-weight: bold; } "
                    "QPushButton { background-color: #334155; color: #f8fafc; border-radius: 4px; padding: 6px 16px; font-weight: bold; } "
                    "QPushButton:hover { background-color: #475569; } "
                    "QPushButton#btnRun { background-color: #059669; } "
                    "QPushButton#btnRun:hover { background-color: #10b981; } "
                    "QPushButton#btnAbort { background-color: #b91c1c; } "
                    "QPushButton#btnAbort:hover { background-color: #dc2626; } ";
    setStyleSheet(style);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(15);

    auto* headerLabel = new QLabel("Potential issues detected in simulation netlist:");
    headerLabel->setStyleSheet("font-weight: bold; font-size: 15px;");
    layout->addWidget(headerLabel);

    m_table = new QTableWidget();
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({"Saliency", "Message"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->setStyleSheet("QTableWidget::item { padding: 8px; } ");
    layout->addWidget(m_table);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    m_btnAbort = new QPushButton("Abort");
    m_btnAbort->setObjectName("btnAbort");
    connect(m_btnAbort, &QPushButton::clicked, this, [this]() {
        m_runAnyway = false;
        reject();
    });
    btnLayout->addWidget(m_btnAbort);

    m_btnRun = new QPushButton("Run Anyway");
    m_btnRun->setObjectName("btnRun");
    connect(m_btnRun, &QPushButton::clicked, this, [this]() {
        m_runAnyway = true;
        accept();
    });
    btnLayout->addWidget(m_btnRun);

    layout->addLayout(btnLayout);
}

void SimulationDebuggerDialog::populateDiagnostics(const QStringList& diagnostics) {
    m_table->setRowCount(diagnostics.size());
    bool hasError = false;

    for (int i = 0; i < diagnostics.size(); ++i) {
        QString msg = diagnostics[i];
        QString level = "INFO";
        QColor color = QColor("#94a3b8");

        if (msg.contains("[error]", Qt::CaseInsensitive)) {
            level = "ERROR";
            color = QColor("#ef4444");
            hasError = true;
        } else if (msg.contains("[warn]", Qt::CaseInsensitive)) {
            level = "WARNING";
            color = QColor("#f59e0b");
        }

        auto* levelItem = new QTableWidgetItem(level);
        levelItem->setForeground(color);
        levelItem->setFont(QFont("Inter", 10, QFont::Bold));
        m_table->setItem(i, 0, levelItem);

        auto* msgItem = new QTableWidgetItem(msg);
        m_table->setItem(i, 1, msgItem);
    }

    if (hasError) {
        m_btnRun->setEnabled(false);
        m_btnRun->setToolTip("Resolve errors before running simulation.");
    }
}
