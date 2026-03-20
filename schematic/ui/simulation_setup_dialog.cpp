#include "simulation_setup_dialog.h"
#include "../../core/theme_manager.h"
#include "../../simulator/core/sim_value_parser.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QRegularExpression>

QJsonObject SimulationSetupDialog::Config::toJson() const {
    QJsonObject obj;
    obj["version"] = 1;
    obj["analysisType"] = static_cast<int>(type);
    obj["stop"] = stop;
    obj["step"] = step;
    obj["start"] = start;
    obj["fStart"] = fStart;
    obj["fStop"] = fStop;
    obj["pts"] = pts;
    obj["commandText"] = commandText;
    return obj;
}

SimulationSetupDialog::Config SimulationSetupDialog::Config::fromJson(const QJsonObject& obj) {
    Config cfg;
    const int typeVal = obj.value("analysisType").toInt(static_cast<int>(cfg.type));
    switch (typeVal) {
        case static_cast<int>(SimAnalysisType::Transient): cfg.type = SimAnalysisType::Transient; break;
        case static_cast<int>(SimAnalysisType::OP): cfg.type = SimAnalysisType::OP; break;
        case static_cast<int>(SimAnalysisType::AC): cfg.type = SimAnalysisType::AC; break;
        default: cfg.type = SimAnalysisType::Transient; break;
    }
    cfg.stop = obj.value("stop").toDouble(cfg.stop);
    cfg.step = obj.value("step").toDouble(cfg.step);
    cfg.start = obj.value("start").toDouble(cfg.start);
    cfg.fStart = obj.value("fStart").toDouble(cfg.fStart);
    cfg.fStop = obj.value("fStop").toDouble(cfg.fStop);
    cfg.pts = obj.value("pts").toInt(cfg.pts);
    cfg.commandText = obj.value("commandText").toString();
    return cfg;
}

SimulationSetupDialog::SimulationSetupDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Simulation Setup");
    setMinimumWidth(350);
    setupUI();
}

void SimulationSetupDialog::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    
    auto* form = new QFormLayout();
    
    m_typeCombo = new QComboBox();
    m_typeCombo->addItems({"Transient (Time)", "DC Operating Point", "AC Sweep (Frequency)", "Interactive (Live)"});
    form->addRow("Analysis Type:", m_typeCombo);

    m_param1 = new QLineEdit("10u");
    m_param2 = new QLineEdit("10m");
    m_param3 = new QLineEdit("0");

    form->addRow("Step Size:", m_param1);
    form->addRow("Stop Time:", m_param2);
    form->addRow("Start Time:", m_param3);

    m_commandLine = new QLineEdit();
    m_commandLine->setStyleSheet(
        "QLineEdit { background: white; color: #3b82f6; border: 1px solid #3b82f6; "
        "font-family: 'Courier New'; font-weight: bold; padding: 4px; }");
    m_commandLine->setPlaceholderText(".tran <tstep> <tstop> [tstart]");
    form->addRow("Command:", m_commandLine);

    mainLayout->addLayout(form);

    auto* btnLayout = new QHBoxLayout();
    auto* okBtn = new QPushButton("Apply");
    auto* cancelBtn = new QPushButton("Cancel");
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(okBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SimulationSetupDialog::onAnalysisChanged);
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SimulationSetupDialog::updateCommandDisplay);
    connect(m_param1, &QLineEdit::textChanged, this, &SimulationSetupDialog::updateCommandDisplay);
    connect(m_param2, &QLineEdit::textChanged, this, &SimulationSetupDialog::updateCommandDisplay);
    connect(m_param3, &QLineEdit::textChanged, this, &SimulationSetupDialog::updateCommandDisplay);
    connect(m_commandLine, &QLineEdit::editingFinished, this, [this]() {
        parseCommandText(m_commandLine->text());
    });
    
    updateCommandDisplay();
}

void SimulationSetupDialog::onAnalysisChanged(int index) {
    auto* form = qobject_cast<QFormLayout*>(layout()->itemAt(0)->layout());
    if (!form) return;

    auto setLabel = [&](QWidget* field, const QString& text) {
        if (auto* lbl = qobject_cast<QLabel*>(form->labelForField(field))) {
            lbl->setText(text);
            lbl->show();
        }
        field->show();
    };

    auto hideField = [&](QWidget* field) {
        if (auto* lbl = form->labelForField(field)) lbl->hide();
        field->hide();
    };

    if (index == 0) { // Transient
        setLabel(m_param1, "Step Size:");
        setLabel(m_param2, "Stop Time:");
        setLabel(m_param3, "Start Time:");
        m_param1->setText("10u"); m_param2->setText("10m"); m_param3->setText("0");
    } else if (index == 1) { // DC
        hideField(m_param1);
        hideField(m_param2);
        hideField(m_param3);
    } else if (index == 2) { // AC
        setLabel(m_param1, "Start Freq:");
        setLabel(m_param2, "Stop Freq:");
        setLabel(m_param3, "Points:");
        m_param1->setText("10"); m_param2->setText("1meg"); m_param3->setText("100");
    } else if (index == 3) { // Real-time
        setLabel(m_param1, "Update Interval (ms):");
        setLabel(m_param2, "Simulated Time Step:");
        hideField(m_param3);
        m_param1->setText("50"); m_param2->setText("1m");
    }
    
    updateCommandDisplay();
}

void SimulationSetupDialog::updateCommandDisplay() {
    if (!m_commandLine) return;
    
    int idx = m_typeCombo->currentIndex();
    QString cmd;
    
    if (idx == 0) { // Transient
        cmd = QString(".tran %1 %2 %3")
            .arg(m_param1->text())
            .arg(m_param2->text())
            .arg(m_param3->text());
    } else if (idx == 1) { // DC OP
        cmd = ".op";
    } else if (idx == 2) { // AC Sweep
        cmd = QString(".ac dec %1 %2 %3")
            .arg(m_param3->text())
            .arg(m_param1->text())
            .arg(m_param2->text());
    } else { // Real-time
        cmd = QString(".tran %1 %2 0")
            .arg(m_param2->text())
            .arg(m_param1->text());
    }
    
    m_commandLine->setText(cmd);
}

void SimulationSetupDialog::parseCommandText(const QString& command) {
    QString cmd = command.trimmed().toLower();
    
    m_typeCombo->blockSignals(true);
    m_param1->blockSignals(true);
    m_param2->blockSignals(true);
    m_param3->blockSignals(true);
    
    if (cmd.startsWith(".tran")) {
        m_typeCombo->setCurrentIndex(0);
        QStringList parts = cmd.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() >= 3) {
            m_param1->setText(parts[1]);
            m_param2->setText(parts[2]);
            if (parts.size() >= 4) m_param3->setText(parts[3]);
        }
    } else if (cmd.startsWith(".op")) {
        m_typeCombo->setCurrentIndex(1);
    } else if (cmd.startsWith(".ac")) {
        m_typeCombo->setCurrentIndex(2);
        QStringList parts = cmd.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() >= 5) {
            m_param3->setText(parts[2]);
            m_param1->setText(parts[3]);
            m_param2->setText(parts[4]);
        }
    }
    
    m_typeCombo->blockSignals(false);
    m_param1->blockSignals(false);
    m_param2->blockSignals(false);
    m_param3->blockSignals(false);
    
    updateCommandDisplay();
}

SimulationSetupDialog::Config SimulationSetupDialog::getConfig() const {
    Config cfg;
    int idx = m_typeCombo->currentIndex();
    if (idx == 0) cfg.type = SimAnalysisType::Transient;
    else if (idx == 1) cfg.type = SimAnalysisType::OP;
    else if (idx == 2) cfg.type = SimAnalysisType::AC;
    else cfg.type = SimAnalysisType::RealTime;
    
    if (cfg.type == SimAnalysisType::Transient) {
        double parsed = 0.0;
        if (SimValueParser::parseSpiceNumber(m_param1->text().trimmed(), parsed)) cfg.step = parsed;
        if (SimValueParser::parseSpiceNumber(m_param2->text().trimmed(), parsed)) cfg.stop = parsed;
        if (SimValueParser::parseSpiceNumber(m_param3->text().trimmed(), parsed)) cfg.start = parsed;
    } else if (cfg.type == SimAnalysisType::AC) {
        double parsed = 0.0;
        if (SimValueParser::parseSpiceNumber(m_param1->text().trimmed(), parsed)) cfg.fStart = parsed;
        if (SimValueParser::parseSpiceNumber(m_param2->text().trimmed(), parsed)) cfg.fStop = parsed;
        cfg.pts = std::max(1, m_param3->text().trimmed().toInt());
    } else if (cfg.type == SimAnalysisType::RealTime) {
        cfg.rtIntervalMs = std::max(10, m_param1->text().trimmed().toInt());
        double parsed = 0.0;
        if (SimValueParser::parseSpiceNumber(m_param2->text().trimmed(), parsed)) cfg.rtStep = parsed;
    }

    cfg.commandText = m_commandLine->text();
    return cfg;
}

void SimulationSetupDialog::setConfig(const Config& cfg) {
    if (cfg.type == SimAnalysisType::Transient) m_typeCombo->setCurrentIndex(0);
    else if (cfg.type == SimAnalysisType::OP) m_typeCombo->setCurrentIndex(1);
    else if (cfg.type == SimAnalysisType::AC) m_typeCombo->setCurrentIndex(2);
    else m_typeCombo->setCurrentIndex(3);

    if (cfg.type == SimAnalysisType::Transient) {
        m_param1->setText(QString::number(cfg.step, 'g', 12));
        m_param2->setText(QString::number(cfg.stop, 'g', 12));
        m_param3->setText(QString::number(cfg.start, 'g', 12));
    } else if (cfg.type == SimAnalysisType::AC) {
        m_param1->setText(QString::number(cfg.fStart, 'g', 12));
        m_param2->setText(QString::number(cfg.fStop, 'g', 12));
        m_param3->setText(QString::number(cfg.pts));
    } else if (cfg.type == SimAnalysisType::RealTime) {
        m_param1->setText(QString::number(cfg.rtIntervalMs));
        m_param2->setText(QString::number(cfg.rtStep, 'g', 12));
    }
    
    if (!cfg.commandText.isEmpty()) {
        m_commandLine->setText(cfg.commandText);
    } else {
        updateCommandDisplay();
    }
}
