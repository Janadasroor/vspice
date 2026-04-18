#include "logic_analyzer_window.h"
#if __has_include("../../core/remote_display_server.h") && __has_include(<QtWebSockets/QWebSocketServer>)
#include "remote_display_server.h"
#define VIOSPICE_HAS_REMOTE_DISPLAY 1
#endif
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QInputDialog>
#include <QCloseEvent>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QUuid>
#include <algorithm>

LogicAnalyzerWindow::LogicAnalyzerWindow(const QString& title, QWidget* parent)
    : QMainWindow(parent) {
    
    setWindowTitle(title);
    resize(900, 400);
    setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);

    QWidget* central = new QWidget();
    QHBoxLayout* mainLayout = new QHBoxLayout(central);
    mainLayout->setContentsMargins(2, 2, 2, 2);

    m_la = new LogicAnalyzerWidget(this);
    mainLayout->addWidget(m_la, 1);

    // Sidebar
    QWidget* sidebar = new QWidget();
    sidebar->setFixedWidth(200);
    QVBoxLayout* sideLayout = new QVBoxLayout(sidebar);

    QGroupBox* ctrlBox = new QGroupBox("DISPLAY CONTROLS");
    QFormLayout* fl = new QFormLayout(ctrlBox);

    m_timeDivSpin = new QDoubleSpinBox();
    m_timeDivSpin->setRange(0.001, 1000.0);
    m_timeDivSpin->setValue(1.0);
    m_timeDivSpin->setSuffix(" ms/div");
    connect(m_timeDivSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), m_la, &LogicAnalyzerWidget::setTimePerDiv);
    connect(m_la, &LogicAnalyzerWidget::timePerDivChanged, m_timeDivSpin, &QDoubleSpinBox::setValue);
    fl->addRow("Timebase:", m_timeDivSpin);

    m_hPosSpin = new QDoubleSpinBox();
    m_hPosSpin->setRange(-10000.0, 10000.0);
    m_hPosSpin->setValue(0.0);
    m_hPosSpin->setSuffix(" ms");
    connect(m_hPosSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), m_la, &LogicAnalyzerWidget::setHorizontalOffset);
    connect(m_la, &LogicAnalyzerWidget::horizontalOffsetChanged, m_hPosSpin, &QDoubleSpinBox::setValue);
    fl->addRow("Position:", m_hPosSpin);

    QDoubleSpinBox* thresholdSpin = new QDoubleSpinBox();
    thresholdSpin->setRange(-10.0, 10.0);
    thresholdSpin->setValue(2.5);
    thresholdSpin->setSuffix(" V");
    thresholdSpin->setSingleStep(0.1);
    connect(thresholdSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), m_la, &LogicAnalyzerWidget::setThreshold);
    fl->addRow("Threshold:", thresholdSpin);

    sideLayout->addWidget(ctrlBox);

    QGroupBox* zoomBox = new QGroupBox("ZOOM");
    QVBoxLayout* zl = new QVBoxLayout(zoomBox);
    QPushButton* zoomIn = new QPushButton("Zoom In (+)");
    QPushButton* zoomOut = new QPushButton("Zoom Out (-)");
    QPushButton* zoomFit = new QPushButton("Zoom to Fit");
    zl->addWidget(zoomIn);
    zl->addWidget(zoomOut);
    zl->addWidget(zoomFit);
    sideLayout->addWidget(zoomBox);

    connect(zoomIn, &QPushButton::clicked, m_la, &LogicAnalyzerWidget::zoomInX);
    connect(zoomOut, &QPushButton::clicked, m_la, &LogicAnalyzerWidget::zoomOutX);
    connect(zoomFit, &QPushButton::clicked, m_la, &LogicAnalyzerWidget::autoScale);

    sideLayout->addWidget(zoomBox);

    QGroupBox* decodeBox = new QGroupBox("DECODERS");
    QVBoxLayout* dl = new QVBoxLayout(decodeBox);
    
    QPushButton* addUartBtn = new QPushButton("+ Add UART");
    dl->addWidget(addUartBtn);
    
    QPushButton* clearDecBtn = new QPushButton("Clear Decoders");
    dl->addWidget(clearDecBtn);
    
    sideLayout->addWidget(decodeBox);

    connect(addUartBtn, &QPushButton::clicked, this, [this]() {
        bool ok;
        QString chan = QInputDialog::getItem(this, "Add UART Decoder", "Select RX Channel:", m_targetNets, 0, false, &ok);
        if (ok && !chan.isEmpty()) {
            int baud = QInputDialog::getInt(this, "UART Baud Rate", "Baud Rate (bps):", 9600, 300, 115200, 1, &ok);
            if (ok) {
                LogicAnalyzerWidget::DecoderConfig cfg;
                cfg.protocol = LogicAnalyzerWidget::Protocol::UART;
                cfg.channelA = chan;
                cfg.baudRate = baud;
                m_la->addDecoder(cfg);
            }
        }
    });
    
    connect(clearDecBtn, &QPushButton::clicked, m_la, &LogicAnalyzerWidget::clearDecoders);

    sideLayout->addStretch();
    
    QPushButton* clearBtn = new QPushButton("Clear Capture");
    connect(clearBtn, &QPushButton::clicked, m_la, &LogicAnalyzerWidget::clear);
    sideLayout->addWidget(clearBtn);

    mainLayout->addWidget(sidebar);
    setCentralWidget(central);
}

void LogicAnalyzerWindow::setChannels(const QStringList& nets) {
    m_targetNets = nets;
    m_la->clear();
}

void LogicAnalyzerWindow::updateData(const SimResults& results) {
    if (!m_la) return;
    
    QJsonObject remoteData;
    QJsonArray remoteTraces;
    
    m_la->beginBatchUpdate();
    for (const auto& wave : results.waveforms) {
        QString waveName = QString::fromStdString(wave.name);
        
        bool found = false;
        for (const auto& target : m_targetNets) {
            if (waveName == "V(" + target + ")" || waveName == target) {
                found = true;
                break;
            }
        }

        if (found) {
            QVector<QPointF> pts;
            pts.reserve((int)wave.xData.size());
            
            QJsonArray xArray;
            QJsonArray yArray;
            
            size_t total = wave.xData.size();
            size_t step = (total > 500) ? total / 500 : 1;

            for (size_t i = 0; i < total; i += step) {
                double x = wave.xData[i];
                double y = wave.yData[i];
                pts.append(QPointF(x, y));
                xArray.append(x);
                yArray.append(y);
            }
            m_la->setChannelData(waveName, pts);
            
            QJsonObject traceObj;
            traceObj["name"] = waveName;
            traceObj["x"] = xArray;
            traceObj["y"] = yArray;
            remoteTraces.append(traceObj);
        }
    }
    m_la->endBatchUpdate();
    
    // Broadcast to remote clients
    remoteData["traces"] = remoteTraces;
    remoteData["timebase"] = m_timeDivSpin->value();
    
    static qint64 lastBroadcast = 0;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastBroadcast > 50) { 
        QUuid uid = QUuid::fromString(m_id);
        if (uid.isNull()) uid = QUuid::createUuidV5(QUuid(), m_id.toUtf8());
        
#ifdef VIOSPICE_HAS_REMOTE_DISPLAY
        RemoteDisplayServer::instance().broadcastUpdate("logicanalyzer", uid, remoteData);
#endif
        lastBroadcast = now;
    }
}

void LogicAnalyzerWindow::clear() {
    m_la->clear();
}

void LogicAnalyzerWindow::closeEvent(QCloseEvent* event) {
    Q_EMIT windowClosing(m_id);
    QMainWindow::closeEvent(event);
}
