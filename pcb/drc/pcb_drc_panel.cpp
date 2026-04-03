#include "pcb_drc_panel.h"
#include <QHBoxLayout>

PCBDRCPanel::PCBDRCPanel(QWidget* parent)
    : QWidget(parent)
    , m_scene(nullptr)
{
    setupUI();

    // Connect DRC signals
    connect(&m_drc, &PCBDRC::checkStarted, this, &PCBDRCPanel::onCheckStarted);
    connect(&m_drc, &PCBDRC::checkProgress, this, &PCBDRCPanel::onCheckProgress);
    connect(&m_drc, &PCBDRC::checkCompleted, this, &PCBDRCPanel::onCheckCompleted);
    connect(&m_drc, &PCBDRC::violationFound, this, [this](const DRCViolation& v) {
        addViolationToList(v);
    });
}

void PCBDRCPanel::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Header
    QWidget* header = new QWidget(this);
    header->setFixedHeight(50);
    header->setStyleSheet("background-color: #1a1a1a; border-bottom: 1px solid #333333;");
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(15, 0, 15, 0);
    
    QLabel* titleLabel = new QLabel("DESIGN RULE CHECK");
    titleLabel->setStyleSheet("color: #a1a1aa; font-weight: 800; font-size: 10px; letter-spacing: 1.2px;");
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    
    layout->addWidget(header);

    // Status Area
    QWidget* statusArea = new QWidget(this);
    statusArea->setStyleSheet("background-color: #161618; border-bottom: 1px solid #27272a;");
    QVBoxLayout* statusLayout = new QVBoxLayout(statusArea);
    statusLayout->setContentsMargins(12, 12, 12, 12);
    statusLayout->setSpacing(8);

    m_statusLabel = new QLabel("Ready to run DRC");
    m_statusLabel->setStyleSheet("color: #d4d4d8; font-size: 11px; font-weight: 600;");
    statusLayout->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar();
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(false);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(4);
    m_progressBar->setStyleSheet(R"(
        QProgressBar {
            background: #27272a;
            border: none;
            border-radius: 2px;
        }
        QProgressBar::chunk {
            background-color: #6366f1;
            border-radius: 2px;
        }
    )");
    statusLayout->addWidget(m_progressBar);

    m_summaryLabel = new QLabel();
    m_summaryLabel->setVisible(false);
    statusLayout->addWidget(m_summaryLabel);

    layout->addWidget(statusArea);

    // Violation list
    m_violationList = new QListWidget();
    m_violationList->setFrameShape(QFrame::NoFrame);
    m_violationList->setStyleSheet(R"(
        QListWidget {
            background: #121212;
            border: none;
            outline: none;
        }
        QListWidget::item {
            padding: 12px;
            border-bottom: 1px solid #1a1a1a;
            color: #e4e4e7;
            font-size: 11px;
        }
        QListWidget::item:selected {
            background-color: rgba(99, 102, 241, 0.15);
            color: #ffffff;
            border-left: 2px solid #6366f1;
        }
        QListWidget::item:hover:!selected {
            background: rgba(255, 255, 255, 0.02);
        }
    )");
    connect(m_violationList, &QListWidget::itemClicked, this, &PCBDRCPanel::onViolationSelected);
    layout->addWidget(m_violationList, 1);

    // Footer with buttons
    QWidget* footer = new QWidget(this);
    footer->setStyleSheet("background: #1a1a1a; border-top: 1px solid #333333;");
    QHBoxLayout* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(12, 12, 12, 12);
    footerLayout->setSpacing(8);

    m_runButton = new QPushButton("Run Check");
    m_runButton->setStyleSheet(R"(
        QPushButton {
            background-color: #6366f1;
            border: none;
            border-radius: 4px;
            padding: 6px 16px;
            color: white;
            font-weight: 700;
            font-size: 11px;
        }
        QPushButton:hover { background-color: #818cf8; }
        QPushButton:pressed { background-color: #4f46e5; }
        QPushButton:disabled { background-color: #3f3f46; color: #71717a; }
    )");
    connect(m_runButton, &QPushButton::clicked, this, &PCBDRCPanel::runCheck);
    footerLayout->addWidget(m_runButton);

    m_clearButton = new QPushButton("Clear");
    m_clearButton->setStyleSheet(R"(
        QPushButton {
            background: #27272a;
            border: 1px solid #3f3f46;
            border-radius: 4px;
            padding: 6px 16px;
            color: #d4d4d8;
            font-size: 11px;
            font-weight: 600;
        }
        QPushButton:hover { background: #3f3f46; border-color: #52525b; }
    )");
    connect(m_clearButton, &QPushButton::clicked, this, &PCBDRCPanel::clearResults);
    footerLayout->addWidget(m_clearButton);

    footerLayout->addStretch();
    layout->addWidget(footer);
}

void PCBDRCPanel::runCheck() {
    if (!m_scene) {
        m_statusLabel->setText("⚠️ No scene to check");
        return;
    }

    m_violationList->clear();
    m_drc.runFullCheck(m_scene);
}

void PCBDRCPanel::clearResults() {
    m_violationList->clear();
    m_drc.clearViolations();
    m_statusLabel->setText("Results cleared");
    m_summaryLabel->setVisible(false);
    m_progressBar->setVisible(false);
}

void PCBDRCPanel::onCheckStarted() {
    m_runButton->setEnabled(false);
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
    m_violationList->clear();
    m_statusLabel->setText("Running DRC...");
}

void PCBDRCPanel::onCheckProgress(int percent, const QString& message) {
    m_progressBar->setValue(percent);
    m_statusLabel->setText(message);
}

void PCBDRCPanel::onCheckCompleted(int errorCount, int warningCount) {
    m_runButton->setEnabled(true);
    m_progressBar->setVisible(false);

    // Update summary
    QString summaryText;
    if (errorCount == 0 && warningCount == 0) {
        summaryText = "✅ <b>No DRC violations found!</b><br>"
                      "<span style='color: #22c55e;'>Your design passes all checks.</span>";
        m_summaryLabel->setStyleSheet(R"(
            QLabel {
                background-color: rgba(34, 197, 94, 30);
                border: 1px solid rgba(34, 197, 94, 100);
                border-radius: 0px;
                padding: 12px;
                font-size: 12px;
                color: #e4e4e7;
            }
        )");
    } else {
        QString errorStyle = errorCount > 0 ? "color: #ef4444; font-weight: bold;" : "color: #22c55e;";
        QString warnStyle = warningCount > 0 ? "color: #f59e0b;" : "color: #22c55e;";
        
        summaryText = QString(
            "<b>DRC Complete</b><br>"
            "<span style='%1'>%2 Error%3</span> • "
            "<span style='%4'>%5 Warning%6</span>"
        ).arg(errorStyle)
         .arg(errorCount).arg(errorCount == 1 ? "" : "s")
         .arg(warnStyle)
         .arg(warningCount).arg(warningCount == 1 ? "" : "s");

        QString bgColor = errorCount > 0 ? "rgba(239, 68, 68, 30)" : "rgba(245, 158, 11, 30)";
        QString borderColor = errorCount > 0 ? "rgba(239, 68, 68, 100)" : "rgba(245, 158, 11, 100)";

        m_summaryLabel->setStyleSheet(QString(R"(
            QLabel {
                background-color: %1;
                border: 1px solid %2;
                border-radius: 0px;
                padding: 12px;
                font-size: 12px;
                color: #e4e4e7;
            }
        )").arg(bgColor, borderColor));
    }

    m_summaryLabel->setText(summaryText);
    m_summaryLabel->setVisible(true);

    m_statusLabel->setText(QString("Check complete • %1 violation%2")
        .arg(errorCount + warningCount)
        .arg((errorCount + warningCount) == 1 ? "" : "s"));

    emit checkCompleted(errorCount, warningCount);
}

void PCBDRCPanel::addViolationToList(const DRCViolation& violation) {
    QListWidgetItem* item = new QListWidgetItem();

    QString icon;
    switch (violation.severity()) {
        case DRCViolation::Error: icon = "🔴"; break;
        case DRCViolation::Warning: icon = "🟡"; break;
        case DRCViolation::Info: icon = "🔵"; break;
    }

    QString text = QString("%1 [%2] %3")
        .arg(icon)
        .arg(violation.typeString())
        .arg(violation.message());

    item->setText(text);
    item->setData(Qt::UserRole, QVariant::fromValue(violation.location()));
    item->setToolTip(QString("Location: (%1, %2)\nClick to navigate")
        .arg(violation.location().x(), 0, 'f', 2)
        .arg(violation.location().y(), 0, 'f', 2));

    m_violationList->addItem(item);
}

void PCBDRCPanel::onViolationSelected(QListWidgetItem* item) {
    QPointF location = item->data(Qt::UserRole).toPointF();
    emit violationSelected(location);
}
