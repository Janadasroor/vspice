#include "voltage_source_custom_waveform_dialog.h"
#include "waveform_draw_widget.h"
#include "waveform_engine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QFileDialog>
#include <QInputDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QtMath>
#include <algorithm>
#include "../../simulator/core/sim_value_parser.h"

VoltageSourceCustomWaveformDialog::VoltageSourceCustomWaveformDialog(QWidget* parent)
    : QDialog(parent), m_drawWidget(nullptr), m_periodEdit(nullptr), m_amplitudeEdit(nullptr),
      m_offsetEdit(nullptr), m_samplesSpin(nullptr), m_repeatCheck(nullptr),
      m_saveToFileCheck(nullptr), m_filePathEdit(nullptr), m_browseBtn(nullptr),
      m_clearBtn(nullptr), m_repeatEnabled(false), m_saveToFileEnabled(false) {
    setWindowTitle("Custom Waveform (Draw)");
    setupUi();
}

void VoltageSourceCustomWaveformDialog::setDefaultSavePath(const QString& dirPath, const QString& baseName) {
    m_defaultDir = dirPath;
    m_defaultBaseName = baseName;
}

void VoltageSourceCustomWaveformDialog::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);

    auto* header = new QLabel("Draw one waveform period (left to right). Hold Ctrl to append.");
    header->setStyleSheet("color: #e5e7eb; font-weight: 600;");
    mainLayout->addWidget(header);

    // Toolbar for advanced tools
    auto* toolLayout = new QHBoxLayout();
    
    auto* sineBtn = new QPushButton("Sine");
    auto* squareBtn = new QPushButton("Square");
    auto* triBtn = new QPushButton("Triangle");
    auto* sawBtn = new QPushButton("Sawtooth");
    auto* pulseBtn = new QPushButton("Pulse");
    
    toolLayout->addWidget(sineBtn);
    toolLayout->addWidget(squareBtn);
    toolLayout->addWidget(triBtn);
    toolLayout->addWidget(sawBtn);
    toolLayout->addWidget(pulseBtn);
    toolLayout->addSpacing(10);
    
    auto* smoothBtn = new QPushButton("Smooth");
    auto* noiseBtn = new QPushButton("Noise");
    auto* invertBtn = new QPushButton("Invert");
    auto* mirrorVBtn = new QPushButton("Mirror V");
    auto* reverseBtn = new QPushButton("Reverse");
    auto* scaleBtn = new QPushButton("Scale T/V");
    auto* shiftBtn = new QPushButton("Shift T");
    
    toolLayout->addWidget(smoothBtn);
    toolLayout->addWidget(noiseBtn);
    toolLayout->addWidget(invertBtn);
    toolLayout->addWidget(mirrorVBtn);
    toolLayout->addWidget(reverseBtn);
    toolLayout->addWidget(scaleBtn);
    toolLayout->addWidget(shiftBtn);
    toolLayout->addSpacing(20);

    auto* snapCheck = new QCheckBox("Snap");
    auto* stepCheck = new QCheckBox("Step Mode");
    toolLayout->addWidget(snapCheck);
    toolLayout->addWidget(stepCheck);
    toolLayout->addStretch();
    
    mainLayout->addLayout(toolLayout);

    m_drawWidget = new WaveformDrawWidget();
    mainLayout->addWidget(m_drawWidget, 1);

    connect(snapCheck, &QCheckBox::toggled, m_drawWidget, &WaveformDrawWidget::setSnapToGrid);
    connect(stepCheck, &QCheckBox::toggled, m_drawWidget, &WaveformDrawWidget::setStepMode);

    auto* formulaLayout = new QHBoxLayout();
    formulaLayout->addWidget(new QLabel("Equation f(x):"));
    m_formulaEdit = new QLineEdit();
    m_formulaEdit->setPlaceholderText("e.g. sin(2*pi*x) + 0.5*sin(6*pi*x)");
    formulaLayout->addWidget(m_formulaEdit, 1);
    auto* applyFormulaBtn = new QPushButton("Apply");
    formulaLayout->addWidget(applyFormulaBtn);
    mainLayout->addLayout(formulaLayout);

    auto* controlLayout = new QGridLayout();
    controlLayout->addWidget(new QLabel("Period [s]:"), 0, 0);
    m_periodEdit = new QLineEdit("1m");
    controlLayout->addWidget(m_periodEdit, 0, 1);
    controlLayout->addWidget(new QLabel("Amplitude [V]:"), 0, 2);
    m_amplitudeEdit = new QLineEdit("1");
    controlLayout->addWidget(m_amplitudeEdit, 0, 3);
    controlLayout->addWidget(new QLabel("Offset [V]:"), 0, 4);
    m_offsetEdit = new QLineEdit("0");
    controlLayout->addWidget(m_offsetEdit, 0, 5);
    controlLayout->addWidget(new QLabel("Samples:"), 0, 6);
    m_samplesSpin = new QSpinBox();
    m_samplesSpin->setRange(8, 512);
    m_samplesSpin->setValue(64);
    controlLayout->addWidget(m_samplesSpin, 0, 7);
    m_repeatCheck = new QCheckBox("Repeat (PWL r=0)");
    controlLayout->addWidget(m_repeatCheck, 1, 0, 1, 3);

    m_saveToFileCheck = new QCheckBox("Save PWL to file");
    controlLayout->addWidget(m_saveToFileCheck, 1, 3, 1, 2);
    m_filePathEdit = new QLineEdit();
    m_filePathEdit->setPlaceholderText("waveform.pwl");
    m_filePathEdit->setEnabled(false);
    controlLayout->addWidget(m_filePathEdit, 1, 5, 1, 2);
    m_browseBtn = new QPushButton("Browse");
    m_browseBtn->setEnabled(false);
    controlLayout->addWidget(m_browseBtn, 1, 7, 1, 1);
    mainLayout->addLayout(controlLayout);

    auto* btnLayout = new QHBoxLayout();
    m_clearBtn = new QPushButton("Clear");
    auto* cancelBtn = new QPushButton("Cancel");
    auto* okBtn = new QPushButton("OK");
    btnLayout->addWidget(m_clearBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(okBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_clearBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onClear);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(okBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onAccepted);
    connect(m_saveToFileCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_filePathEdit->setEnabled(checked);
        m_browseBtn->setEnabled(checked);
    });
    connect(m_browseBtn, &QPushButton::clicked, this, [this]() {
        QString suggested = m_defaultDir.isEmpty() ? QString() : (m_defaultDir + "/" + (m_defaultBaseName.isEmpty() ? "waveform.pwl" : m_defaultBaseName));
        QString path = QFileDialog::getSaveFileName(this, "Save PWL File", suggested, "PWL Files (*.pwl *.txt *.csv *.dat);;All Files (*)");
        if (!path.isEmpty()) m_filePathEdit->setText(path);
    });

    connect(sineBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onApplySine);
    connect(squareBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onApplySquare);
    connect(triBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onApplyTriangle);
    connect(sawBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onApplySawtooth);
    connect(pulseBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onApplyPulse);
    connect(smoothBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onApplySmooth);
    connect(noiseBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onApplyNoise);
    connect(invertBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onApplyInvert);
    connect(mirrorVBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onApplyMirrorV);
    connect(reverseBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onApplyReverse);
    connect(scaleBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onApplyScaleTime);
    connect(shiftBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onApplyShiftTime);
    connect(applyFormulaBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onApplyFormula);
}

void VoltageSourceCustomWaveformDialog::onApplySine() {
    m_drawWidget->setPoints(WaveformEngine::generateSine());
}

void VoltageSourceCustomWaveformDialog::onApplySquare() {
    bool ok;
    double duty = QInputDialog::getDouble(this, "Square Wave", "Duty Cycle (0-100%):", 50.0, 0.0, 100.0, 1, &ok);
    if (ok) m_drawWidget->setPoints(WaveformEngine::generateSquare(duty / 100.0));
}

void VoltageSourceCustomWaveformDialog::onApplyTriangle() {
    m_drawWidget->setPoints(WaveformEngine::generateTriangle());
}

void VoltageSourceCustomWaveformDialog::onApplySawtooth() {
    m_drawWidget->setPoints(WaveformEngine::generateSawtooth());
}

void VoltageSourceCustomWaveformDialog::onApplyPulse() {
    bool ok;
    double v1 = QInputDialog::getDouble(this, "Pulse", "Initial Value (V1):", 0.0, -1000, 1000, 3, &ok); if (!ok) return;
    double v2 = QInputDialog::getDouble(this, "Pulse", "Pulsed Value (V2):", 5.0, -1000, 1000, 3, &ok); if (!ok) return;
    double td = QInputDialog::getDouble(this, "Pulse", "Delay (0.0-1.0):", 0.1, 0, 1, 3, &ok); if (!ok) return;
    double tw = QInputDialog::getDouble(this, "Pulse", "Width (0.0-1.0):", 0.5, 0, 1, 3, &ok); if (!ok) return;

    m_drawWidget->setPoints(WaveformEngine::generatePulse(v1, v2, td, tw));
    double maxV = qMax(qAbs(v1), qAbs(v2));
    if (maxV > 1.0) m_amplitudeEdit->setText(QString::number(maxV));
}

void VoltageSourceCustomWaveformDialog::onApplySmooth() {
    QVector<QPointF> pts = m_drawWidget->points();
    WaveformEngine::smooth(pts);
    m_drawWidget->setPoints(pts);
}

void VoltageSourceCustomWaveformDialog::onApplyNoise() {
    QVector<QPointF> pts = m_drawWidget->points();
    WaveformEngine::addNoise(pts);
    m_drawWidget->setPoints(pts);
}

void VoltageSourceCustomWaveformDialog::onApplyInvert() {
    QVector<QPointF> pts = m_drawWidget->points();
    for (auto& p : pts) p.setY(-p.y());
    m_drawWidget->setPoints(pts);
}

void VoltageSourceCustomWaveformDialog::onApplyMirrorV() {
    m_drawWidget->scaleValue(-1.0);
}

void VoltageSourceCustomWaveformDialog::onApplyReverse() {
    m_drawWidget->reverseTime();
}

void VoltageSourceCustomWaveformDialog::onApplyScaleTime() {
    bool ok;
    double f = QInputDialog::getDouble(this, "Scale", "Time Scale Factor:", 1.0, 0.01, 100, 3, &ok);
    if (ok) m_drawWidget->scaleTime(f);
    f = QInputDialog::getDouble(this, "Scale", "Value Scale Factor:", 1.0, -100, 100, 3, &ok);
    if (ok) m_drawWidget->scaleValue(f);
}

void VoltageSourceCustomWaveformDialog::onApplyShiftTime() {
    bool ok;
    double delta = QInputDialog::getDouble(this, "Shift", "Time Shift:", 0.0, -1, 1, 3, &ok);
    if (ok) m_drawWidget->shiftTime(delta);
}

void VoltageSourceCustomWaveformDialog::onApplyFormula() {
    bool ok;
    QVector<QPointF> pts = WaveformEngine::generateFromFormula(m_formulaEdit->text(), 200, &ok);
    if (ok) m_drawWidget->setPoints(pts);
    else QMessageBox::warning(this, "Formula Error", "Invalid mathematical expression.");
}

void VoltageSourceCustomWaveformDialog::onClear() {
    m_drawWidget->clearPoints();
}

void VoltageSourceCustomWaveformDialog::onAccepted() {
    m_pwlPoints = buildPwlPoints();
    m_repeatEnabled = m_repeatCheck->isChecked();
    m_saveToFileEnabled = m_saveToFileCheck->isChecked();

    if (m_saveToFileEnabled) {
        m_pwlFilePath = m_filePathEdit->text().trimmed();
        if (m_pwlFilePath.isEmpty() && !m_defaultDir.isEmpty()) {
            m_pwlFilePath = m_defaultDir + "/" + (m_defaultBaseName.isEmpty() ? "waveform.pwl" : m_defaultBaseName);
        }
        if (m_pwlFilePath.isEmpty()) {
            m_pwlFilePath = QFileDialog::getSaveFileName(this, "Save PWL", m_defaultDir, "PWL Files (*.pwl *.txt)");
        }
        if (m_pwlFilePath.isEmpty()) return;

        QFile file(m_pwlFilePath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            QStringList tokens = m_pwlPoints.split(' ', Qt::SkipEmptyParts);
            for (int i = 0; i + 1 < tokens.size(); i += 2) out << tokens[i] << " " << tokens[i + 1] << "\n";
        }
    }
    accept();
}

static double parseOrDefault(const QString& text, double fallback) {
    double v = 0.0;
    if (SimValueParser::parseSpiceNumber(text.trimmed(), v)) return v;
    return fallback;
}

QString VoltageSourceCustomWaveformDialog::buildPwlPoints() const {
    QVector<QPointF> points = m_drawWidget->points();
    if (points.isEmpty()) return "0 0 1 0";

    std::sort(points.begin(), points.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
    if (points.first().x() > 0.0) points.prepend({0.0, points.first().y()});
    if (points.last().x() < 1.0) points.append({1.0, points.last().y()});

    const int samples = m_samplesSpin->value();
    const double period = qMax(1e-12, parseOrDefault(m_periodEdit->text(), 1.0));
    const double amplitude = parseOrDefault(m_amplitudeEdit->text(), 1.0);
    const double offset = parseOrDefault(m_offsetEdit->text(), 0.0);

    QStringList tokens;
    for (int i = 0; i < samples; ++i) {
        double x = double(i) / (samples - 1);
        double y = 0;
        for (int j = 1; j < points.size(); ++j) {
            if (x <= points[j].x()) {
                const QPointF a = points[j - 1];
                const QPointF b = points[j];
                if (m_drawWidget->isStepMode()) y = a.y();
                else {
                    double t = (x - a.x()) / (b.x() - a.x());
                    y = a.y() + t * (b.y() - a.y());
                }
                break;
            }
        }
        tokens << QString::number(x * period, 'g', 12) << QString::number(offset + amplitude * y, 'g', 12);
    }
    return tokens.join(' ');
}
