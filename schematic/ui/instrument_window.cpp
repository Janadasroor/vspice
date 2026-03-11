#include "instrument_window.h"
#include <QVBoxLayout>

#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <cmath>

InstrumentWindow::InstrumentWindow(const QString& title, QWidget* parent)
    : QMainWindow(parent) {
    
    setWindowTitle(title);
    resize(850, 500);
    setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);

    QWidget* central = new QWidget();
    QHBoxLayout* mainLayout = new QHBoxLayout(central);
    mainLayout->setContentsMargins(2, 2, 2, 2);

    m_scope = new OscilloscopeWidget(this);
    mainLayout->addWidget(m_scope, 1);

    // Sidebar Controls
    QWidget* sidebar = new QWidget();
    sidebar->setFixedWidth(200);
    QVBoxLayout* sideLayout = new QVBoxLayout(sidebar);
    
    // Horizontal Controls
    QGroupBox* hBox = new QGroupBox("HORIZONTAL");
    QFormLayout* hFL = new QFormLayout(hBox);
    QDoubleSpinBox* timeDiv = new QDoubleSpinBox();
    timeDiv->setRange(0.001, 1000.0);
    timeDiv->setValue(1.0);
    timeDiv->setSuffix(" ms");
    connect(timeDiv, QOverload<double>::of(&QDoubleSpinBox::valueChanged), m_scope, &OscilloscopeWidget::setTimePerDiv);
    connect(m_scope, &OscilloscopeWidget::timePerDivChanged, timeDiv, &QDoubleSpinBox::setValue);
    hFL->addRow("Time/Div:", timeDiv);

    QDoubleSpinBox* hPos = new QDoubleSpinBox();
    hPos->setRange(-1000.0, 1000.0);
    hPos->setValue(0.0);
    hPos->setSuffix(" ms");
    connect(hPos, QOverload<double>::of(&QDoubleSpinBox::valueChanged), m_scope, &OscilloscopeWidget::setHorizontalOffset);
    connect(m_scope, &OscilloscopeWidget::horizontalOffsetChanged, hPos, &QDoubleSpinBox::setValue);
    hFL->addRow("Position:", hPos);

    QCheckBox* fftCheck = new QCheckBox("FFT Spectrum Mode");
    connect(fftCheck, &QCheckBox::toggled, m_scope, &OscilloscopeWidget::setFftEnabled);
    hFL->addRow("", fftCheck);

    sideLayout->addWidget(hBox);

    // Vertical Controls (Selected Channel)
    QGroupBox* vBox = new QGroupBox("VERTICAL");
    QFormLayout* vFL = new QFormLayout(vBox);
    QComboBox* chSelect = new QComboBox();
    chSelect->addItem("None");
    connect(chSelect, &QComboBox::currentTextChanged, m_scope, &OscilloscopeWidget::setActiveChannel);
    vFL->addRow("Active CH:", chSelect);

    QDoubleSpinBox* voltDiv = new QDoubleSpinBox();
    voltDiv->setRange(0.001, 100.0);
    voltDiv->setValue(1.0);
    voltDiv->setSuffix(" V");
    connect(voltDiv, QOverload<double>::of(&QDoubleSpinBox::valueChanged), m_scope, &OscilloscopeWidget::setVoltsPerDiv);
    connect(m_scope, &OscilloscopeWidget::voltsPerDivChanged, voltDiv, &QDoubleSpinBox::setValue);
    vFL->addRow("Volts/Div:", voltDiv);

    QDoubleSpinBox* vPos = new QDoubleSpinBox();
    vPos->setRange(-100.0, 100.0);
    vPos->setValue(0.0);
    vPos->setSuffix(" V");
    connect(vPos, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this, chSelect](double v){
        if (chSelect->currentIndex() > 0) m_scope->setVerticalOffset(chSelect->currentText(), v);
    });
    vFL->addRow("Offset:", vPos);
    sideLayout->addWidget(vBox);

    // Trigger Controls
    QGroupBox* tBox = new QGroupBox("TRIGGER");
    QFormLayout* tFL = new QFormLayout(tBox);
    QComboBox* tMode = new QComboBox();
    tMode->addItems({"Auto", "Normal", "Single"});
    connect(tMode, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int idx){
        m_scope->setTriggerMode(static_cast<OscilloscopeWidget::TriggerMode>(idx));
    });
    tFL->addRow("Mode:", tMode);

    QDoubleSpinBox* tLevel = new QDoubleSpinBox();
    tLevel->setRange(-100.0, 100.0);
    tLevel->setValue(0.0);
    tLevel->setSuffix(" V");
    connect(tLevel, QOverload<double>::of(&QDoubleSpinBox::valueChanged), m_scope, &OscilloscopeWidget::setTriggerLevel);
    tFL->addRow("Level:", tLevel);
    
    sideLayout->addWidget(tBox);
    
    // Zoom Tools
    QGroupBox* zBox = new QGroupBox("ZOOM TOOLS");
    QVBoxLayout* zL = new QVBoxLayout(zBox);
    
    QHBoxLayout* hZoomBts = new QHBoxLayout();
    QPushButton* hIn = new QPushButton("H-Zoom +");
    hIn->setStyleSheet("padding: 5px;");
    QPushButton* hOut = new QPushButton("H-Zoom -");
    hOut->setStyleSheet("padding: 5px;");
    hZoomBts->addWidget(hIn);
    hZoomBts->addWidget(hOut);
    zL->addLayout(hZoomBts);
    
    QHBoxLayout* vZoomBts = new QHBoxLayout();
    QPushButton* vIn = new QPushButton("V-Zoom +");
    vIn->setStyleSheet("padding: 5px;");
    QPushButton* vOut = new QPushButton("V-Zoom -");
    vOut->setStyleSheet("padding: 5px;");
    vZoomBts->addWidget(vIn);
    vZoomBts->addWidget(vOut);
    zL->addLayout(vZoomBts);
    
    QPushButton* fitBtn = new QPushButton("Zoom to Fit");
    fitBtn->setStyleSheet("background-color: #1e3a8a; color: white; font-weight: bold; padding: 5px;");
    zL->addWidget(fitBtn);
    
    connect(hIn, &QPushButton::clicked, m_scope, &OscilloscopeWidget::zoomInX);
    connect(hOut, &QPushButton::clicked, m_scope, &OscilloscopeWidget::zoomOutX);
    connect(vIn, &QPushButton::clicked, m_scope, &OscilloscopeWidget::zoomInY);
    connect(vOut, &QPushButton::clicked, m_scope, &OscilloscopeWidget::zoomOutY);
    connect(fitBtn, &QPushButton::clicked, m_scope, &OscilloscopeWidget::autoScale);
    
    sideLayout->addWidget(zBox);

    // Measurements Section
    QGroupBox* mBox = new QGroupBox("MEASUREMENTS");
    QFormLayout* mFL = new QFormLayout(mBox);
    auto createMeasureLabel = [&]() {
        QLabel* l = new QLabel("---");
        l->setStyleSheet("font-family: 'Courier New'; font-weight: bold; color: #10b981;");
        l->setAlignment(Qt::AlignRight);
        return l;
    };
    m_minValLabel = createMeasureLabel();
    m_maxValLabel = createMeasureLabel();
    m_pkpkValLabel = createMeasureLabel();
    m_rmsValLabel = createMeasureLabel();
    m_avgValLabel = createMeasureLabel();
    mFL->addRow("Min:", m_minValLabel);
    mFL->addRow("Max:", m_maxValLabel);
    mFL->addRow("Pk-Pk:", m_pkpkValLabel);
    mFL->addRow("RMS:", m_rmsValLabel);
    mFL->addRow("Avg:", m_avgValLabel);
    sideLayout->addWidget(mBox);

    sideLayout->addStretch();

    mainLayout->addWidget(sidebar);
    setCentralWidget(central);
    
    // Store references for channel updates
    m_chSelect = chSelect;
    connect(chSelect, &QComboBox::currentTextChanged, this, &InstrumentWindow::updateMeasurementLabels);
}

void InstrumentWindow::setChannels(const QStringList& nets) {
    m_targetNets = nets;
    m_measurements.clear();
    if (m_chSelect) {
        m_chSelect->clear();
        m_chSelect->addItem("None");
        m_chSelect->addItems(nets);
    }
    updateMeasurementLabels();
}

void InstrumentWindow::updateData(const SimResults& results) {
    if (!m_scope) return;

    if (results.analysisType == SimAnalysisType::RealTime) {
        for (const auto& wave : results.waveforms) {
            QString name = QString::fromStdString(wave.name);
            bool match = false;
            for (const auto& target : m_targetNets) {
                if (name == "V(" + target + ")" || name == target) { match = true; break; }
            }
            if (match && !wave.xData.empty()) {
                m_scope->addData(name, wave.xData[0], wave.yData[0]);
                
                // Update min/max incrementally
                Measurements& m = m_measurements[name];
                if (!m.valid) { m.min = m.max = wave.yData[0]; m.valid = true; }
                else { m.min = std::min(m.min, wave.yData[0]); m.max = std::max(m.max, wave.yData[0]); }
            }
        }
        updateMeasurementLabels();
        return;
    }

    m_scope->clear();
    m_scope->beginBatchUpdate();

    auto strideFor = [](size_t n, int target) {
        if (target <= 0 || n <= static_cast<size_t>(target)) return 1;
        return static_cast<int>(std::ceil(static_cast<double>(n) / static_cast<double>(target)));
    };

    for (const auto& wave : results.waveforms) {
        QString waveName = QString::fromStdString(wave.name);
        
        // Only display if this net is connected to one of our instrument pins
        bool match = false;
        for (const auto& target : m_targetNets) {
            if (waveName == "V(" + target + ")" || waveName == target) {
                match = true;
                break;
            }
        }

        if (match) {
            // Update measurements
            if (!wave.yData.empty()) {
                auto [minIt, maxIt] = std::minmax_element(wave.yData.begin(), wave.yData.end());
                Measurements& m = m_measurements[waveName];
                m.min = *minIt;
                m.max = *maxIt;
                
                double sum = 0, sumSq = 0;
                for (double v : wave.yData) { sum += v; sumSq += v*v; }
                m.avg = sum / wave.yData.size();
                m.rms = std::sqrt(sumSq / wave.yData.size());
                
                m.valid = true;
            }

            const int stride = strideFor(wave.xData.size(), 3000);
            QVector<QPointF> points;
            points.reserve(static_cast<int>((wave.xData.size() + static_cast<size_t>(stride) - 1) / static_cast<size_t>(stride)));
            for (size_t i = 0; i < wave.xData.size(); i += static_cast<size_t>(stride)) {
                points.append(QPointF(wave.xData[i], wave.yData[i]));
            }
            m_scope->setChannelData(waveName, points);
        }
    }
    m_scope->endBatchUpdate();
    updateMeasurementLabels();
}

void InstrumentWindow::setTimeCursor(double t) {
    if (m_scope) m_scope->setTimeCursor(t);
}

void InstrumentWindow::updateMeasurementLabels() {
    if (!m_chSelect || !m_minValLabel) return;
    
    QString activeChannel = m_chSelect->currentText();
    if (activeChannel == "None" || !m_measurements.contains(activeChannel)) {
        m_minValLabel->setText("---");
        m_maxValLabel->setText("---");
        m_pkpkValLabel->setText("---");
        m_rmsValLabel->setText("---");
        m_avgValLabel->setText("---");
        return;
    }

    const Measurements& m = m_measurements[activeChannel];
    if (m.valid) {
        m_minValLabel->setText(OscilloscopeWidget::formatValue(m.min, "V"));
        m_maxValLabel->setText(OscilloscopeWidget::formatValue(m.max, "V"));
        m_pkpkValLabel->setText(OscilloscopeWidget::formatValue(m.max - m.min, "V"));
        m_rmsValLabel->setText(OscilloscopeWidget::formatValue(m.rms, "V"));
        m_avgValLabel->setText(OscilloscopeWidget::formatValue(m.avg, "V"));
    } else {
        m_minValLabel->setText("---");
        m_maxValLabel->setText("---");
        m_pkpkValLabel->setText("---");
        m_rmsValLabel->setText("---");
        m_avgValLabel->setText("---");
    }
}

void InstrumentWindow::clear() {
    if (m_scope) m_scope->clear();
    m_measurements.clear();
    updateMeasurementLabels();
}
