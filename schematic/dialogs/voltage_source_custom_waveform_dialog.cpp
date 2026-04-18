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
    : QDialog(parent), m_drawWidget(nullptr), m_formulaEdit(nullptr), m_periodEdit(nullptr), m_amplitudeEdit(nullptr),
      m_offsetEdit(nullptr), m_samplesSpin(nullptr), m_repeatCheck(nullptr),
      m_saveToFileCheck(nullptr), m_resampleCheck(nullptr), m_filePathEdit(nullptr), m_browseBtn(nullptr),
      m_clearBtn(nullptr), m_repeatEnabled(false), m_saveToFileEnabled(false) {
    
    setWindowTitle("Custom Waveform Editor");
    
    // Add maximize/minimize buttons to existing dialog flags
    setWindowFlags(windowFlags() | Qt::WindowMinMaxButtonsHint);
    
    // Set a safe default size that fits on practically any monitor
    resize(750, 550);
    
    setupUi();
}

void VoltageSourceCustomWaveformDialog::setDefaultSavePath(const QString& dirPath, const QString& baseName) {
    m_defaultDir = dirPath;
    m_defaultBaseName = baseName;
}

VoltageSourceCustomWaveformDialog::Result VoltageSourceCustomWaveformDialog::execCustomDraw(QWidget* parent, const QString& projectDir, const QString& defaultName) {
    VoltageSourceCustomWaveformDialog dlg(parent);
    dlg.setDefaultSavePath(projectDir, defaultName);
    
    Result res;
    if (dlg.exec() == QDialog::Accepted) {
        res.accepted = true;
        res.points = dlg.pwlPoints();
        res.repeat = dlg.repeatEnabled();
        res.filePath = dlg.pwlFilePath();
        res.saveToFile = dlg.saveToFileEnabled();
    }
    return res;
}

void VoltageSourceCustomWaveformDialog::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);

    auto* header = new QLabel("Draw one waveform period (left to right). Hold Ctrl to append.");
    header->setStyleSheet("color: #e5e7eb; font-weight: 600;");
    mainLayout->addWidget(header);

    createGeneratorsRow(mainLayout);
    createTransformationsRow(mainLayout);
    createOptionsRow(mainLayout);

    m_drawWidget = new WaveformDrawWidget();
    mainLayout->addWidget(m_drawWidget, 1);

    createFormulaBar(mainLayout);
    createParameterGrid(mainLayout);
    createActionButtons(mainLayout);
}

void VoltageSourceCustomWaveformDialog::createGeneratorsRow(QVBoxLayout* layout) {
    auto* row = new QHBoxLayout();
    
    auto* sineBtn = new QPushButton("Sine");
    auto* squareBtn = new QPushButton("Square");
    auto* triBtn = new QPushButton("Triangle");
    auto* sawBtn = new QPushButton("Sawtooth");
    auto* bitsBtn = new QPushButton("Bitstream");
    auto* pulseBtn = new QPushButton("Pulse");
    
    row->addWidget(sineBtn);
    row->addWidget(squareBtn);
    row->addWidget(triBtn);
    row->addWidget(sawBtn);
    row->addWidget(bitsBtn);
    row->addWidget(pulseBtn);
    row->addStretch();
    
    layout->addLayout(row);

    connect(sineBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onApplySine);
    connect(squareBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onApplySquare);
    connect(triBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onApplyTriangle);
    connect(sawBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onApplySawtooth);
    connect(bitsBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onApplyBitstream);
    connect(pulseBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onApplyPulse);
}

void VoltageSourceCustomWaveformDialog::createTransformationsRow(QVBoxLayout* layout) {
    auto* row = new QHBoxLayout();
    
    auto* smoothBtn = new QPushButton("Smooth");
    auto* noiseBtn = new QPushButton("Noise");
    auto* invertBtn = new QPushButton("Invert");
    auto* mirrorVBtn = new QPushButton("Mirror V");
    auto* reverseBtn = new QPushButton("Reverse");
    auto* scaleBtn = new QPushButton("Scale T/V");
    auto* shiftBtn = new QPushButton("Shift T");
    
    row->addWidget(smoothBtn);
    row->addWidget(noiseBtn);
    row->addWidget(invertBtn);
    row->addWidget(mirrorVBtn);
    row->addWidget(reverseBtn);
    row->addWidget(scaleBtn);
    row->addWidget(shiftBtn);
    row->addStretch();
    
    layout->addLayout(row);

    connect(smoothBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onApplySmooth);
    connect(noiseBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onApplyNoise);
    connect(invertBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onApplyInvert);
    connect(mirrorVBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onApplyMirrorV);
    connect(reverseBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onApplyReverse);
    connect(scaleBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onApplyScaleTime);
    connect(shiftBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onApplyShiftTime);
}

void VoltageSourceCustomWaveformDialog::createOptionsRow(QVBoxLayout* layout) {
    auto* row = new QHBoxLayout();
    
    auto* snapCheck = new QCheckBox("Snap");
    auto* stepCheck = new QCheckBox("Step Mode");
    auto* polylineCheck = new QCheckBox("Polyline");
    m_resampleCheck = new QCheckBox("Resample");
    m_resampleCheck->setChecked(false); 
    
    row->addWidget(snapCheck);
    row->addWidget(stepCheck);
    row->addWidget(polylineCheck);
    row->addWidget(m_resampleCheck);
    row->addStretch();
    
    layout->addLayout(row);

    connect(snapCheck, &QCheckBox::toggled, m_drawWidget, &WaveformDrawWidget::setSnapToGrid);
    connect(stepCheck, &QCheckBox::toggled, m_drawWidget, &WaveformDrawWidget::setStepMode);
    connect(polylineCheck, &QCheckBox::toggled, m_drawWidget, &WaveformDrawWidget::setPolylineMode);
    connect(m_resampleCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_samplesSpin) m_samplesSpin->setEnabled(checked);
    });
}

void VoltageSourceCustomWaveformDialog::createFormulaBar(QVBoxLayout* layout) {
    auto* row = new QHBoxLayout();
    row->addWidget(new QLabel("Equation f(x):"));
    
    m_formulaEdit = new QLineEdit();
    m_formulaEdit->setPlaceholderText("e.g. sin(2*pi*x) + 0.5*sin(6*pi*x)");
    row->addWidget(m_formulaEdit, 1);
    
    auto* applyBtn = new QPushButton("Apply");
    row->addWidget(applyBtn);
    
    layout->addLayout(row);

    connect(applyBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onApplyFormula);
    connect(m_formulaEdit, &QLineEdit::returnPressed, this, &VoltageSourceCustomWaveformDialog::onApplyFormula);
}

void VoltageSourceCustomWaveformDialog::createParameterGrid(QVBoxLayout* layout) {
    auto* grid = new QGridLayout();
    
    grid->addWidget(new QLabel("Period [s]:"), 0, 0);
    m_periodEdit = new QLineEdit("1m");
    grid->addWidget(m_periodEdit, 0, 1);
    
    grid->addWidget(new QLabel("Amplitude [V]:"), 0, 2);
    m_amplitudeEdit = new QLineEdit("1");
    grid->addWidget(m_amplitudeEdit, 0, 3);
    
    grid->addWidget(new QLabel("Offset [V]:"), 0, 4);
    m_offsetEdit = new QLineEdit("0");
    grid->addWidget(m_offsetEdit, 0, 5);
    
    grid->addWidget(new QLabel("Samples:"), 0, 6);
    m_samplesSpin = new QSpinBox();
    m_samplesSpin->setRange(8, 2048);
    m_samplesSpin->setValue(64);
    m_samplesSpin->setEnabled(false); // Default resample is off
    grid->addWidget(m_samplesSpin, 0, 7);
    
    m_repeatCheck = new QCheckBox("Repeat (PWL r=0)");
    grid->addWidget(m_repeatCheck, 1, 0, 1, 3);

    m_saveToFileCheck = new QCheckBox("Save PWL to file");
    grid->addWidget(m_saveToFileCheck, 1, 3, 1, 2);
    
    m_filePathEdit = new QLineEdit();
    m_filePathEdit->setPlaceholderText("waveform.pwl");
    m_filePathEdit->setEnabled(false);
    grid->addWidget(m_filePathEdit, 1, 5, 1, 2);
    
    m_browseBtn = new QPushButton("Browse");
    m_browseBtn->setEnabled(false);
    grid->addWidget(m_browseBtn, 1, 7, 1, 1);
    
    layout->addLayout(grid);

    connect(m_saveToFileCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_filePathEdit->setEnabled(checked);
        m_browseBtn->setEnabled(checked);
    });
    
    connect(m_browseBtn, &QPushButton::clicked, this, [this]() {
        QString suggested = m_defaultDir.isEmpty() ? QString() : (m_defaultDir + "/" + (m_defaultBaseName.isEmpty() ? "waveform.pwl" : m_defaultBaseName));
        QString path = QFileDialog::getSaveFileName(this, "Save PWL File", suggested, "PWL Files (*.pwl *.txt *.csv *.dat);;All Files (*)");
        if (!path.isEmpty()) m_filePathEdit->setText(path);
    });
}

void VoltageSourceCustomWaveformDialog::createActionButtons(QVBoxLayout* layout) {
    auto* row = new QHBoxLayout();
    
    m_clearBtn = new QPushButton("Clear");
    auto* cancelBtn = new QPushButton("Cancel");
    auto* okBtn = new QPushButton("OK");
    
    row->addWidget(m_clearBtn);
    row->addStretch();
    row->addWidget(cancelBtn);
    row->addWidget(okBtn);
    
    layout->addLayout(row);

    connect(m_clearBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onClear);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(okBtn, &QPushButton::clicked, this, &VoltageSourceCustomWaveformDialog::onAccepted);
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

void VoltageSourceCustomWaveformDialog::onApplyBitstream() {
    bool ok;
    QString bits = QInputDialog::getText(this, "Logic Bitstream", "Enter bits (e.g. 101101):", QLineEdit::Normal, "", &ok);
    if (ok && !bits.isEmpty()) {
        m_drawWidget->setPoints(WaveformEngine::generateBitstream(bits));
        m_drawWidget->setStepMode(true);
        // Update UI checkbox to match
        for (auto* obj : children()) {
            if (auto* cb = qobject_cast<QCheckBox*>(obj)) {
                if (cb->text() == "Step Mode") cb->setChecked(true);
            }
        }
    }
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
    if (!m_drawWidget) return "0 0 1 0";

    WaveformEngine::ExportParams p;
    p.period = qMax(1e-12, parseOrDefault(m_periodEdit->text(), 1.0));
    p.amplitude = parseOrDefault(m_amplitudeEdit->text(), 1.0);
    p.offset = parseOrDefault(m_offsetEdit->text(), 0.0);
    p.isStepMode = m_drawWidget->isStepMode();
    p.resample = m_resampleCheck && m_resampleCheck->isChecked();
    p.sampleCount = m_samplesSpin->value();

    return WaveformEngine::convertToPwl(m_drawWidget->points(), p);
}
