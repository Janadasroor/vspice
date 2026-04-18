#include "voltage_source_ltspice_dialog.h"
#include "voltage_source_custom_waveform_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QFileDialog>
#include <QCompleter>
#include <QComboBox>
#include <QStringListModel>
#include <QKeyEvent>
#include <QJSEngine>
#include <QInputDialog>
#include <QAbstractItemView>
#include <QSyntaxHighlighter>
#include <QTextDocument>
#include <QFileInfo>
#include <QMessageBox>
#include <QSaveFile>
#include <QtEndian>
#include <cmath>
#include "../analysis/net_manager.h"
#include "../editor/schematic_commands.h"

namespace {
QString normalizeBehavioralExpr(QString expr) {
    expr = expr.trimmed();
    if (expr.isEmpty()) return "V=0";
    if (!expr.startsWith("V=", Qt::CaseInsensitive)) expr.prepend("V=");
    return expr;
}

QString validateBehavioralExpr(const QString& expr) {
    const QString v = expr.trimmed();
    if (v.isEmpty()) return "Expression is empty. Using V=0.";
    int depth = 0;
    for (const QChar& c : v) {
        if (c == '(') depth++;
        else if (c == ')') depth--;
        if (depth < 0) break;
    }
    if (depth != 0) return "Unbalanced parentheses.";
    if (!v.startsWith("V=", Qt::CaseInsensitive)) return "Missing 'V=' prefix. It will be added.";
    return QString();
}

struct DecodedWavData {
    int sampleRate = 0;
    int channelCount = 0;
    QVector<QVector<double>> channels;
};

quint32 readLe32(const char* data) {
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(data));
}

quint16 readLe16(const char* data) {
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(data));
}

bool decodeWavFile(const QString& path, DecodedWavData* out, QString* error) {
    if (!out) return false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }

    const QByteArray bytes = file.readAll();
    if (bytes.size() < 44 || bytes.mid(0, 4) != "RIFF" || bytes.mid(8, 4) != "WAVE") {
        if (error) *error = "Not a valid RIFF/WAVE file.";
        return false;
    }

    quint16 audioFormat = 0;
    quint16 channelCount = 0;
    quint16 bitsPerSample = 0;
    quint16 blockAlign = 0;
    quint32 sampleRate = 0;
    QByteArray pcmData;

    int offset = 12;
    while (offset + 8 <= bytes.size()) {
        const QByteArray chunkId = bytes.mid(offset, 4);
        const quint32 chunkSize = readLe32(bytes.constData() + offset + 4);
        offset += 8;
        if (offset + static_cast<int>(chunkSize) > bytes.size()) break;

        if (chunkId == "fmt ") {
            if (chunkSize < 16) {
                if (error) *error = "Invalid WAV fmt chunk.";
                return false;
            }
            const char* fmt = bytes.constData() + offset;
            audioFormat = readLe16(fmt + 0);
            channelCount = readLe16(fmt + 2);
            sampleRate = readLe32(fmt + 4);
            blockAlign = readLe16(fmt + 12);
            bitsPerSample = readLe16(fmt + 14);
        } else if (chunkId == "data") {
            pcmData = bytes.mid(offset, chunkSize);
        }

        offset += static_cast<int>(chunkSize);
        if (chunkSize & 1) ++offset;
    }

    if (sampleRate == 0 || channelCount == 0 || bitsPerSample == 0 || blockAlign == 0 || pcmData.isEmpty()) {
        if (error) *error = "Missing WAV format or data chunk.";
        return false;
    }

    if (audioFormat != 1 && audioFormat != 3) {
        if (error) *error = QString("Unsupported WAV encoding format %1. Only PCM and IEEE float are supported.").arg(audioFormat);
        return false;
    }

    const int bytesPerSample = bitsPerSample / 8;
    if (bytesPerSample <= 0 || blockAlign < bytesPerSample * channelCount) {
        if (error) *error = "Invalid WAV sample format.";
        return false;
    }

    const int frameCount = pcmData.size() / blockAlign;
    if (frameCount <= 0) {
        if (error) *error = "WAV file contains no audio frames.";
        return false;
    }

    out->sampleRate = static_cast<int>(sampleRate);
    out->channelCount = static_cast<int>(channelCount);
    out->channels = QVector<QVector<double>>(out->channelCount);
    for (auto& channel : out->channels) {
        channel.reserve(frameCount);
    }

    for (int frame = 0; frame < frameCount; ++frame) {
        const char* frameData = pcmData.constData() + frame * blockAlign;
        for (int channel = 0; channel < out->channelCount; ++channel) {
            const char* sampleData = frameData + channel * bytesPerSample;
            double sample = 0.0;

            if (audioFormat == 3 && bitsPerSample == 32) {
                float value = 0.0f;
                memcpy(&value, sampleData, sizeof(float));
                sample = static_cast<double>(value);
            } else if (audioFormat == 3 && bitsPerSample == 64) {
                double value = 0.0;
                memcpy(&value, sampleData, sizeof(double));
                sample = value;
            } else if (bitsPerSample == 8) {
                const quint8 value = static_cast<quint8>(sampleData[0]);
                sample = (static_cast<double>(value) - 128.0) / 128.0;
            } else if (bitsPerSample == 16) {
                const qint16 value = static_cast<qint16>(readLe16(sampleData));
                sample = static_cast<double>(value) / 32768.0;
            } else if (bitsPerSample == 24) {
                qint32 value = (static_cast<quint8>(sampleData[0])) |
                               (static_cast<quint8>(sampleData[1]) << 8) |
                               (static_cast<quint8>(sampleData[2]) << 16);
                if (value & 0x00800000) value |= ~0x00FFFFFF;
                sample = static_cast<double>(value) / 8388608.0;
            } else if (bitsPerSample == 32) {
                const qint32 value = static_cast<qint32>(readLe32(sampleData));
                sample = static_cast<double>(value) / 2147483648.0;
            } else {
                if (error) *error = QString("Unsupported WAV bit depth %1.").arg(bitsPerSample);
                return false;
            }

            out->channels[channel].append(sample);
        }
    }

    return true;
}

bool writePwlFromWav(const QString& wavPath,
                     const QString& projectDir,
                     const QString& reference,
                     int channelIndex,
                     double amplitudeScale,
                     double dcOffset,
                     QString* outPath,
                     QString* error) {
    DecodedWavData wav;
    if (!decodeWavFile(wavPath, &wav, error)) {
        return false;
    }
    if (channelIndex < 0 || channelIndex >= wav.channels.size()) {
        if (error) *error = "Selected WAV channel is out of range.";
        return false;
    }

    QFileInfo wavInfo(wavPath);
    const QString targetDir = !projectDir.isEmpty() ? projectDir : wavInfo.absolutePath();
    const QString baseName = !reference.isEmpty() ? reference : wavInfo.completeBaseName();
    QDir().mkpath(targetDir);
    const QString pwlPath = QDir(targetDir).filePath(baseName + "_audio.pwl");

    QSaveFile outFile(pwlPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = outFile.errorString();
        return false;
    }

    QTextStream out(&outFile);
    constexpr int kMaxPwlPoints = 100000;
    const QVector<double>& samples = wav.channels[channelIndex];
    const int stride = std::max(1, static_cast<int>(std::ceil(static_cast<double>(samples.size()) / kMaxPwlPoints)));
    const double dt = 1.0 / static_cast<double>(wav.sampleRate);

    for (int i = 0; i < samples.size(); i += stride) {
        const double t = static_cast<double>(i) * dt;
        const double value = samples[i] * amplitudeScale + dcOffset;
        out << QString::number(t, 'g', 12) << ' ' << QString::number(value, 'g', 12) << '\n';
    }

    if (!samples.isEmpty() && (samples.size() - 1) % stride != 0) {
        const int lastIndex = samples.size() - 1;
        const double t = static_cast<double>(lastIndex) * dt;
        const double value = samples[lastIndex] * amplitudeScale + dcOffset;
        out << QString::number(t, 'g', 12) << ' ' << QString::number(value, 'g', 12) << '\n';
    }

    if (!outFile.commit()) {
        if (error) *error = outFile.errorString();
        return false;
    }

    if (outPath) *outPath = pwlPath;
    return true;
}
}

class BehavioralHighlighter : public QSyntaxHighlighter {
public:
    explicit BehavioralHighlighter(QTextDocument* doc) : QSyntaxHighlighter(doc) {
        m_numberFormat.setForeground(QColor("#1f6feb"));
        m_funcFormat.setForeground(QColor("#7e57c2"));
        m_funcFormat.setFontWeight(QFont::DemiBold);
        m_varFormat.setForeground(QColor("#d97706"));
        m_varFormat.setFontWeight(QFont::DemiBold);
        m_opFormat.setForeground(QColor("#555555"));

        m_funcPatterns = {
            QRegularExpression("\\b(if|table|abs|sin|cos|tan|sqrt|exp|log|min|max|pow|limit)\\s*\\(", QRegularExpression::CaseInsensitiveOption)
        };
        m_varPatterns = {
            QRegularExpression("\\bV\\s*\\(", QRegularExpression::CaseInsensitiveOption),
            QRegularExpression("\\bI\\s*\\(", QRegularExpression::CaseInsensitiveOption),
            QRegularExpression("\\btime\\b", QRegularExpression::CaseInsensitiveOption)
        };
        m_numberPattern = QRegularExpression("[-+]?(?:\\d*\\.\\d+|\\d+)(?:[eE][-+]?\\d+)?");
        m_opPattern = QRegularExpression("[\\+\\-\\*/\\^=<>]");
    }

protected:
    void highlightBlock(const QString& text) override {
        auto apply = [&](const QRegularExpression& re, const QTextCharFormat& fmt) {
            auto it = re.globalMatch(text);
            while (it.hasNext()) {
                auto m = it.next();
                setFormat(m.capturedStart(), m.capturedLength(), fmt);
            }
        };

        for (const auto& re : m_funcPatterns) apply(re, m_funcFormat);
        for (const auto& re : m_varPatterns) apply(re, m_varFormat);
        apply(m_numberPattern, m_numberFormat);
        apply(m_opPattern, m_opFormat);
    }

private:
    QTextCharFormat m_numberFormat;
    QTextCharFormat m_funcFormat;
    QTextCharFormat m_varFormat;
    QTextCharFormat m_opFormat;
    QList<QRegularExpression> m_funcPatterns;
    QList<QRegularExpression> m_varPatterns;
    QRegularExpression m_numberPattern;
    QRegularExpression m_opPattern;
};

VoltageSourceLTSpiceDialog::VoltageSourceLTSpiceDialog(VoltageSourceItem* item, QUndoStack* undoStack, QGraphicsScene* scene, const QString& projectDir, QWidget* parent)
    : QDialog(parent), m_item(item), m_undoStack(undoStack), m_scene(scene), m_projectDir(projectDir) {
    
    setWindowTitle("Independent Voltage Source - " + item->reference());
    setupUi();
    loadFromItem();
}

void VoltageSourceLTSpiceDialog::setupUi() {
    auto* mainLayout = new QHBoxLayout(this);

    // --- Left Column: Functions ---
    auto* leftCol = new QVBoxLayout();
    auto* groupFunctions = new QGroupBox("Functions");
    auto* functionsLayout = new QVBoxLayout(groupFunctions);

    m_noneRadio = new QRadioButton("(none)");
    m_pulseRadio = new QRadioButton("PULSE(V1 V2 Tdelay Trise Tfall Ton Period Ncycles)");
    m_sineRadio = new QRadioButton("SINE(Voffset Vamp Freq Td Theta Phi Ncycles)");
    m_expRadio = new QRadioButton("EXP(V1 V2 Td1 Tau1 Td2 Tau2)");
    m_sffmRadio = new QRadioButton("SFFM(Voff Vamp Fcar MDI Fsig)");
    m_pwlRadio = new QRadioButton("PWL(t1 v1 t2 v2...)");
    m_behavioralRadio = new QRadioButton("Behavioral (BV: V=expression)");
    m_customRadio = new QRadioButton("CUSTOM (Draw)");
    m_pwlFileRadio = new QRadioButton("PWL FILE:");
    m_waveFileRadio = new QRadioButton("WAVEFILE (Audio .wav):");

    functionsLayout->addWidget(m_noneRadio);
    functionsLayout->addWidget(m_pulseRadio);
    functionsLayout->addWidget(m_sineRadio);
    functionsLayout->addWidget(m_expRadio);
    functionsLayout->addWidget(m_sffmRadio);
    functionsLayout->addWidget(m_pwlRadio);
    functionsLayout->addWidget(m_behavioralRadio);
    functionsLayout->addWidget(m_customRadio);

    auto* pwlFileLayout = new QHBoxLayout();
    pwlFileLayout->addWidget(m_pwlFileRadio);
    m_pwlFile = new QLineEdit();
    pwlFileLayout->addWidget(m_pwlFile);
    auto* pwlBrowseBtn = new QPushButton("Browse");
    pwlFileLayout->addWidget(pwlBrowseBtn);
    functionsLayout->addLayout(pwlFileLayout);

    connect(pwlBrowseBtn, &QPushButton::clicked, this, &VoltageSourceLTSpiceDialog::onPwlBrowse);

    auto* waveFileRadioLayout = new QHBoxLayout();
    waveFileRadioLayout->addWidget(m_waveFileRadio);
    m_waveFile = new QLineEdit();
    waveFileRadioLayout->addWidget(m_waveFile);
    auto* waveBrowseBtn = new QPushButton("Browse");
    waveFileRadioLayout->addWidget(waveBrowseBtn);
    functionsLayout->addLayout(waveFileRadioLayout);

    connect(waveBrowseBtn, &QPushButton::clicked, this, &VoltageSourceLTSpiceDialog::onWaveBrowse);

    // Function Parameters (Stacked)
    m_paramStack = new QStackedWidget();
    
    // 0: None
    m_paramStack->addWidget(new QWidget());

    // 1: Pulse
    auto* pulsePage = new QWidget();
    auto* pulseLayout = new QGridLayout(pulsePage);
    pulseLayout->addWidget(new QLabel("Vinitial[V]:"), 0, 0); m_pulseV1 = new QLineEdit(); pulseLayout->addWidget(m_pulseV1, 0, 1);
    pulseLayout->addWidget(new QLabel("Von[V]:"), 1, 0);     m_pulseV2 = new QLineEdit(); pulseLayout->addWidget(m_pulseV2, 1, 1);
    pulseLayout->addWidget(new QLabel("Tdelay[s]:"), 2, 0); m_pulseTd = new QLineEdit(); pulseLayout->addWidget(m_pulseTd, 2, 1);
    pulseLayout->addWidget(new QLabel("Trise[s]:"), 3, 0);  m_pulseTr = new QLineEdit(); pulseLayout->addWidget(m_pulseTr, 3, 1);
    pulseLayout->addWidget(new QLabel("Tfall[s]:"), 4, 0);  m_pulseTf = new QLineEdit(); pulseLayout->addWidget(m_pulseTf, 4, 1);
    pulseLayout->addWidget(new QLabel("Ton[s]:"), 5, 0);    m_pulseTon = new QLineEdit(); pulseLayout->addWidget(m_pulseTon, 5, 1);
    pulseLayout->addWidget(new QLabel("Tperiod[s]:"), 6, 0); m_pulseTperiod = new QLineEdit(); pulseLayout->addWidget(m_pulseTperiod, 6, 1);
    pulseLayout->addWidget(new QLabel("Ncycles:"), 7, 0);   m_pulseNcycles = new QLineEdit(); pulseLayout->addWidget(m_pulseNcycles, 7, 1);
    m_paramStack->addWidget(pulsePage);

    // 2: Sine
    auto* sinePage = new QWidget();
    auto* sineLayout = new QGridLayout(sinePage);
    sineLayout->addWidget(new QLabel("Voffset[V]:"), 0, 0); m_sineVoffset = new QLineEdit(); sineLayout->addWidget(m_sineVoffset, 0, 1);
    sineLayout->addWidget(new QLabel("Vamp[V]:"), 1, 0);    m_sineVamp = new QLineEdit(); sineLayout->addWidget(m_sineVamp, 1, 1);
    sineLayout->addWidget(new QLabel("Freq[Hz]:"), 2, 0);   m_sineFreq = new QLineEdit(); sineLayout->addWidget(m_sineFreq, 2, 1);
    sineLayout->addWidget(new QLabel("Td[s]:"), 3, 0);     m_sineTd = new QLineEdit(); sineLayout->addWidget(m_sineTd, 3, 1);
    sineLayout->addWidget(new QLabel("Theta[1/s]:"), 4, 0); m_sineTheta = new QLineEdit(); sineLayout->addWidget(m_sineTheta, 4, 1);
    sineLayout->addWidget(new QLabel("Phi[deg]:"), 5, 0);   m_sinePhi = new QLineEdit(); sineLayout->addWidget(m_sinePhi, 5, 1);
    sineLayout->addWidget(new QLabel("Ncycles:"), 6, 0);    m_sineNcycles = new QLineEdit(); sineLayout->addWidget(m_sineNcycles, 6, 1);
    m_paramStack->addWidget(sinePage);

    // 3: EXP
    auto* expPage = new QWidget();
    auto* expLayout = new QGridLayout(expPage);
    expLayout->addWidget(new QLabel("V1[V]:"), 0, 0);   m_expV1 = new QLineEdit(); expLayout->addWidget(m_expV1, 0, 1);
    expLayout->addWidget(new QLabel("V2[V]:"), 1, 0);   m_expV2 = new QLineEdit(); expLayout->addWidget(m_expV2, 1, 1);
    expLayout->addWidget(new QLabel("Td1[s]:"), 2, 0);  m_expTd1 = new QLineEdit(); expLayout->addWidget(m_expTd1, 2, 1);
    expLayout->addWidget(new QLabel("Tau1[s]:"), 3, 0); m_expTau1 = new QLineEdit(); expLayout->addWidget(m_expTau1, 3, 1);
    expLayout->addWidget(new QLabel("Td2[s]:"), 4, 0);  m_expTd2 = new QLineEdit(); expLayout->addWidget(m_expTd2, 4, 1);
    expLayout->addWidget(new QLabel("Tau2[s]:"), 5, 0); m_expTau2 = new QLineEdit(); expLayout->addWidget(m_expTau2, 5, 1);
    m_paramStack->addWidget(expPage);

    // 4: SFFM
    auto* sffmPage = new QWidget();
    auto* sffmLayout = new QGridLayout(sffmPage);
    sffmLayout->addWidget(new QLabel("Voff[V]:"), 0, 0);  m_sffmVoff = new QLineEdit(); sffmLayout->addWidget(m_sffmVoff, 0, 1);
    sffmLayout->addWidget(new QLabel("Vamp[V]:"), 1, 0);  m_sffmVamp = new QLineEdit(); sffmLayout->addWidget(m_sffmVamp, 1, 1);
    sffmLayout->addWidget(new QLabel("Fcar[Hz]:"), 2, 0); m_sffmFcar = new QLineEdit(); sffmLayout->addWidget(m_sffmFcar, 2, 1);
    sffmLayout->addWidget(new QLabel("MDI:"), 3, 0);      m_sffmMdi = new QLineEdit(); sffmLayout->addWidget(m_sffmMdi, 3, 1);
    sffmLayout->addWidget(new QLabel("Fsig[Hz]:"), 4, 0); m_sffmFsig = new QLineEdit(); sffmLayout->addWidget(m_sffmFsig, 4, 1);
    m_paramStack->addWidget(sffmPage);

    // 5: PWL
    auto* pwlPage = new QWidget();
    auto* pwlLayout = new QVBoxLayout(pwlPage);
    pwlLayout->addWidget(new QLabel("Values(t1 v1 t2 v2...):"));
    auto* pwlRow = new QHBoxLayout();
    m_pwlPoints = new QLineEdit();
    m_pwlDrawBtn = new QPushButton("Draw...");
    pwlRow->addWidget(m_pwlPoints);
    pwlRow->addWidget(m_pwlDrawBtn);
    pwlLayout->addLayout(pwlRow);
    m_pwlRepeat = new QCheckBox("Repeat (PWL r=0)");
    pwlLayout->addWidget(m_pwlRepeat);
    m_paramStack->addWidget(pwlPage);

    // 6: Behavioral (BV)
    auto* behavioralPage = new QWidget();
    auto* behavioralLayout = new QVBoxLayout(behavioralPage);
    behavioralLayout->addWidget(new QLabel("Expression (use V=...):"));
    m_behavioralExpr = new QPlainTextEdit();
    m_behavioralExpr->setPlaceholderText("V=V(in)*2");
    m_behavioralExpr->setMinimumHeight(90);
    behavioralLayout->addWidget(m_behavioralExpr);
    new BehavioralHighlighter(m_behavioralExpr->document());

    auto* presetRow = new QHBoxLayout();
    auto* preset1 = new QPushButton("V=V(in)");
    auto* preset2 = new QPushButton("V=V(in)*2");
    auto* preset3 = new QPushButton("V=if(V(in)>0,1,0)");
    auto* preset4 = new QPushButton("V=table(time, 0 0 1m 1)");
    presetRow->addWidget(preset1);
    presetRow->addWidget(preset2);
    presetRow->addWidget(preset3);
    presetRow->addWidget(preset4);
    behavioralLayout->addLayout(presetRow);

    m_behavioralStatus = new QLabel();
    m_behavioralStatus->setStyleSheet("color: #cc0000; font-size: 11px;");
    behavioralLayout->addWidget(m_behavioralStatus);

    auto* helperRow = new QHBoxLayout();
    m_insertNodeBtn = new QPushButton("Insert Node...");
    helperRow->addWidget(m_insertNodeBtn);
    helperRow->addStretch();
    behavioralLayout->addLayout(helperRow);

    auto* previewRow = new QHBoxLayout();
    previewRow->addWidget(new QLabel("Preview V:"));
    m_previewValue = new QDoubleSpinBox();
    m_previewValue->setRange(-1e6, 1e6);
    m_previewValue->setDecimals(4);
    m_previewValue->setValue(1.0);
    previewRow->addWidget(m_previewValue);
    previewRow->addWidget(new QLabel("time:"));
    m_previewTime = new QDoubleSpinBox();
    m_previewTime->setRange(-1e6, 1e6);
    m_previewTime->setDecimals(6);
    m_previewTime->setValue(0.0);
    previewRow->addWidget(m_previewTime);
    behavioralLayout->addLayout(previewRow);

    m_behavioralPreview = new QLabel();
    m_behavioralPreview->setStyleSheet("color: #333333; font-size: 11px;");
    behavioralLayout->addWidget(m_behavioralPreview);

    auto* help = new QLabel("Tips: use V(node), I(Vsrc), time, if(), table(), abs(), sin().");
    help->setStyleSheet("color: #666666; font-size: 11px;");
    behavioralLayout->addWidget(help);

    m_paramStack->addWidget(behavioralPage);

    // 7: WAVEFILE
    auto* waveFilePage = new QWidget();
    auto* waveFileLayout = new QVBoxLayout(waveFilePage);

    auto* waveFilePathLayout = new QHBoxLayout();
    waveFilePathLayout->addWidget(new QLabel("WAV File:"));
    m_waveFile = new QLineEdit();
    m_waveFile->setPlaceholderText("Path to .wav file...");
    waveFilePathLayout->addWidget(m_waveFile);
    auto* waveFileBrowseBtn = new QPushButton("Browse...");
    waveFilePathLayout->addWidget(waveFileBrowseBtn);
    waveFileLayout->addLayout(waveFilePathLayout);

    auto* waveChanLayout = new QHBoxLayout();
    waveChanLayout->addWidget(new QLabel("Channel:"));
    m_waveChan = new QLineEdit("0");
    m_waveChan->setMaximumWidth(80);
    waveChanLayout->addWidget(m_waveChan);
    waveChanLayout->addSpacing(10);
    waveChanLayout->addWidget(new QLabel("Peak Scale:"));
    auto* m_waveScale = new QDoubleSpinBox();
    m_waveScale->setRange(0.001, 10000.0);
    m_waveScale->setDecimals(4);
    m_waveScale->setValue(1.0);
    m_waveScale->setMaximumWidth(120);
    m_waveScale->setToolTip("Multiply all WAV samples by this factor.\n1.0 = original amplitude, 2.0 = 6dB gain, 0.5 = -6dB.");
    waveChanLayout->addWidget(m_waveScale);
    m_waveScaleSpin = m_waveScale;
    waveChanLayout->addStretch();
    waveFileLayout->addLayout(waveChanLayout);

    m_waveFileInfo = new QLabel();
    m_waveFileInfo->setWordWrap(true);
    m_waveFileInfo->setStyleSheet("color: #555555; font-size: 11px; background: #f8f8f8; border: 1px solid #e0e0e0; border-radius: 4px; padding: 8px;");
    m_waveFileInfo->setTextFormat(Qt::RichText);
    m_waveFileInfo->setMinimumHeight(80);
    m_waveFileInfo->setText("<b>No WAV file selected.</b><br><br>"
                            "Select a WAV file to see its properties here.<br>"
                            "Supported: 8/16/24/32-bit PCM, 32/64-bit IEEE float.");
    waveFileLayout->addWidget(m_waveFileInfo);

    auto* waveInfoNote = new QLabel("<b>Note:</b> WAV samples are normalized to ±1.0 V. Use <b>Peak Scale</b> to adjust amplitude (e.g. 2.0 = 6dB gain).");
    waveInfoNote->setWordWrap(true);
    waveInfoNote->setStyleSheet("color: #888888; font-size: 10px;");
    waveFileLayout->addWidget(waveInfoNote);

    waveFileLayout->addStretch();

    connect(waveFileBrowseBtn, &QPushButton::clicked, this, &VoltageSourceLTSpiceDialog::onWaveBrowse);
    connect(m_waveFile, &QLineEdit::textChanged, this, &VoltageSourceLTSpiceDialog::onWaveFileChanged);

    m_paramStack->addWidget(waveFilePage);

    functionsLayout->addWidget(m_paramStack);
    
    m_functionVisible = new QCheckBox("Make this information visible on schematic:");
    functionsLayout->addWidget(m_functionVisible);
    
    leftCol->addWidget(groupFunctions);

    // --- Right Column: DC, AC, Parasitics ---
    auto* rightCol = new QVBoxLayout();
    
    // DC Value
    auto* groupDc = new QGroupBox("DC Value");
    auto* dcLayout = new QGridLayout(groupDc);
    dcLayout->addWidget(new QLabel("DC value:"), 0, 0);
    m_dcValue = new QLineEdit();
    dcLayout->addWidget(m_dcValue, 0, 1);
    m_dcVisible = new QCheckBox("Make this information visible on schematic:");
    dcLayout->addWidget(m_dcVisible, 1, 0, 1, 2);
    rightCol->addWidget(groupDc);

    // AC Analysis
    auto* groupAc = new QGroupBox("Small signal AC analysis(.AC)");
    auto* acLayout = new QGridLayout(groupAc);
    acLayout->addWidget(new QLabel("AC Amplitude:"), 0, 0); m_acAmplitude = new QLineEdit(); acLayout->addWidget(m_acAmplitude, 0, 1);
    acLayout->addWidget(new QLabel("AC Phase:"), 1, 0);     m_acPhase = new QLineEdit(); acLayout->addWidget(m_acPhase, 1, 1);
    m_acVisible = new QCheckBox("Make this information visible on schematic:");
    acLayout->addWidget(m_acVisible, 2, 0, 1, 2);
    rightCol->addWidget(groupAc);

    // Parasitics
    auto* groupPara = new QGroupBox("Parasitic Properties");
    auto* paraLayout = new QGridLayout(groupPara);
    paraLayout->addWidget(new QLabel("Series Resistance[Ω]:"), 0, 0); m_seriesRes = new QLineEdit(); paraLayout->addWidget(m_seriesRes, 0, 1);
    paraLayout->addWidget(new QLabel("Parallel Capacitance[F]:"), 1, 0); m_parallelCap = new QLineEdit(); paraLayout->addWidget(m_parallelCap, 1, 1);
    m_parasiticVisible = new QCheckBox("Make this information visible on schematic:");
    paraLayout->addWidget(m_parasiticVisible, 2, 0, 1, 2);
    rightCol->addWidget(groupPara);

    // Buttons
    auto* btnLayout = new QHBoxLayout();
    auto* cancelBtn = new QPushButton("Cancel");
    auto* okBtn = new QPushButton("OK");
    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(okBtn);
    rightCol->addLayout(btnLayout);

    mainLayout->addLayout(leftCol);
    mainLayout->addLayout(rightCol);

    connect(m_noneRadio, &QRadioButton::toggled, this, &VoltageSourceLTSpiceDialog::onFunctionChanged);
    connect(m_pulseRadio, &QRadioButton::toggled, this, &VoltageSourceLTSpiceDialog::onFunctionChanged);
    connect(m_sineRadio, &QRadioButton::toggled, this, &VoltageSourceLTSpiceDialog::onFunctionChanged);
    connect(m_expRadio, &QRadioButton::toggled, this, &VoltageSourceLTSpiceDialog::onFunctionChanged);
    connect(m_sffmRadio, &QRadioButton::toggled, this, &VoltageSourceLTSpiceDialog::onFunctionChanged);
    connect(m_pwlRadio, &QRadioButton::toggled, this, &VoltageSourceLTSpiceDialog::onFunctionChanged);
    connect(m_behavioralRadio, &QRadioButton::toggled, this, &VoltageSourceLTSpiceDialog::onFunctionChanged);
    connect(m_customRadio, &QRadioButton::toggled, this, &VoltageSourceLTSpiceDialog::onFunctionChanged);
    connect(m_pwlFileRadio, &QRadioButton::toggled, this, &VoltageSourceLTSpiceDialog::onFunctionChanged);
    connect(m_waveFileRadio, &QRadioButton::toggled, this, &VoltageSourceLTSpiceDialog::onFunctionChanged);

    connect(okBtn, &QPushButton::clicked, this, &VoltageSourceLTSpiceDialog::onAccepted);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_pwlDrawBtn, &QPushButton::clicked, this, &VoltageSourceLTSpiceDialog::onCustomDraw);

    m_behavioralCompleter = new QCompleter(this);
    m_behavioralCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    m_behavioralCompleter->setCompletionMode(QCompleter::PopupCompletion);
    m_behavioralCompleter->setWidget(m_behavioralExpr);
    m_behavioralExpr->installEventFilter(this);

    connect(m_behavioralCompleter, qOverload<const QString &>(&QCompleter::activated), this, [this](const QString& completion) {
        if (!m_behavioralExpr) return;
        QTextCursor tc = m_behavioralExpr->textCursor();
        tc.select(QTextCursor::WordUnderCursor);
        tc.removeSelectedText();
        tc.insertText(completion);
        m_behavioralExpr->setTextCursor(tc);
    });

    auto updateCompleter = [this]() {
        QStringList words = {
            "V(", "I(", "time", "if(", "table(", "abs(", "sin(", "cos(", "tan(", "sqrt(", "exp(", "log(",
            "min(", "max(", "pow(", "limit(", "u", "n", "p", "k", "Meg"
        };
        if (m_scene) {
            NetManager nm;
            nm.updateNets(m_scene);
            auto nets = nm.netNames();
            for (const QString& n : nets) {
                if (!n.trimmed().isEmpty()) words << QString("V(%1)").arg(n);
            }
        }
        words.removeDuplicates();
        words.sort();
        auto* model = new QStringListModel(words, m_behavioralCompleter);
        m_behavioralCompleter->setModel(model);
    };
    updateCompleter();

    auto updateBehavioralUi = [this]() {
        const QString expr = m_behavioralExpr->toPlainText();
        const QString msg = validateBehavioralExpr(expr);
        if (msg.isEmpty()) {
            m_behavioralStatus->setText("Expression looks good.");
            m_behavioralStatus->setStyleSheet("color: #008800; font-size: 11px;");
        } else {
            m_behavioralStatus->setText(msg);
            if (msg.contains("Unbalanced")) {
                m_behavioralStatus->setStyleSheet("color: #cc0000; font-size: 11px;");
            } else {
                m_behavioralStatus->setStyleSheet("color: #996600; font-size: 11px;");
            }
        }

        int errorPos = -1;
        QVector<int> stack;
        for (int i = 0; i < expr.size(); ++i) {
            const QChar c = expr.at(i);
            if (c == '(') stack.push_back(i);
            else if (c == ')') {
                if (stack.isEmpty()) { errorPos = i; break; }
                stack.removeLast();
            }
        }
        if (errorPos < 0 && !stack.isEmpty()) errorPos = stack.first();

        QList<QTextEdit::ExtraSelection> extras;
        if (errorPos >= 0) {
            QTextEdit::ExtraSelection sel;
            QTextCursor cursor(m_behavioralExpr->document());
            cursor.setPosition(errorPos);
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
            sel.cursor = cursor;
            QTextCharFormat fmt;
            fmt.setUnderlineColor(QColor("#cc0000"));
            fmt.setUnderlineStyle(QTextCharFormat::SingleUnderline);
            sel.format = fmt;
            extras << sel;
            m_behavioralExpr->setToolTip(QString("Unbalanced parenthesis at position %1").arg(errorPos + 1));
        } else {
            m_behavioralExpr->setToolTip("");
        }
        m_behavioralExpr->setExtraSelections(extras);

        QString preview = "Preview: ";
        QString normalized = normalizeBehavioralExpr(expr);
        QString evalExpr = normalized;
        evalExpr.remove(0, 2); // strip V=
        if (evalExpr.contains("if(", Qt::CaseInsensitive) || evalExpr.contains("table(", Qt::CaseInsensitive)) {
            m_behavioralPreview->setText(preview + "not available for if()/table().");
            return;
        }

        QRegularExpression reV("V\\s*\\([^\\)]*\\)", QRegularExpression::CaseInsensitiveOption);
        QRegularExpression reI("I\\s*\\([^\\)]*\\)", QRegularExpression::CaseInsensitiveOption);
        evalExpr = evalExpr.replace(reV, QString::number(m_previewValue->value(), 'g', 8));
        evalExpr = evalExpr.replace(reI, QString::number(m_previewValue->value(), 'g', 8));
        evalExpr.replace(QRegularExpression("\\btime\\b", QRegularExpression::CaseInsensitiveOption),
                         QString::number(m_previewTime->value(), 'g', 8));

        evalExpr.replace("abs(", "Math.abs(");
        evalExpr.replace("sin(", "Math.sin(");
        evalExpr.replace("cos(", "Math.cos(");
        evalExpr.replace("tan(", "Math.tan(");
        evalExpr.replace("sqrt(", "Math.sqrt(");
        evalExpr.replace("exp(", "Math.exp(");
        evalExpr.replace("log(", "Math.log(");
        evalExpr.replace("min(", "Math.min(");
        evalExpr.replace("max(", "Math.max(");
        evalExpr.replace("pow(", "Math.pow(");

        QJSEngine engine;
        QJSValue result = engine.evaluate(evalExpr);
        if (result.isError()) {
            m_behavioralPreview->setText(preview + "error evaluating expression.");
        } else {
            m_behavioralPreview->setText(preview + result.toString());
        }
    };

    connect(m_behavioralExpr, &QPlainTextEdit::textChanged, this, updateBehavioralUi);
    connect(m_previewValue, qOverload<double>(&QDoubleSpinBox::valueChanged), this, updateBehavioralUi);
    connect(m_previewTime, qOverload<double>(&QDoubleSpinBox::valueChanged), this, updateBehavioralUi);

    connect(m_insertNodeBtn, &QPushButton::clicked, this, [this]() {
        if (!m_scene) return;
        NetManager nm;
        nm.updateNets(m_scene);
        QStringList nets = nm.netNames();
        nets.removeAll("0");
        nets.removeAll("GND");
        nets.sort();
        bool ok = false;
        QString picked = QInputDialog::getItem(this, "Insert Node", "Node:", nets, 0, false, &ok);
        if (!ok || picked.isEmpty()) return;
        QTextCursor cursor = m_behavioralExpr->textCursor();
        cursor.insertText(QString("V(%1)").arg(picked));
        m_behavioralExpr->setTextCursor(cursor);
    });

    connect(preset1, &QPushButton::clicked, this, [this]() { m_behavioralExpr->setPlainText("V=V(in)"); });
    connect(preset2, &QPushButton::clicked, this, [this]() { m_behavioralExpr->setPlainText("V=V(in)*2"); });
    connect(preset3, &QPushButton::clicked, this, [this]() { m_behavioralExpr->setPlainText("V=if(V(in)>0,1,0)"); });
    connect(preset4, &QPushButton::clicked, this, [this]() { m_behavioralExpr->setPlainText("V=table(time, 0 0 1m 1)"); });

    updateBehavioralUi();
}

void VoltageSourceLTSpiceDialog::onFunctionChanged() {
    // Only proceed if this is the radio button being CHECKED
    QRadioButton* rb = qobject_cast<QRadioButton*>(sender());
    if (rb && !rb->isChecked()) return;

    if (m_noneRadio->isChecked()) m_paramStack->setCurrentIndex(0);
    else if (m_pulseRadio->isChecked()) m_paramStack->setCurrentIndex(1);
    else if (m_sineRadio->isChecked()) m_paramStack->setCurrentIndex(2);
    else if (m_expRadio->isChecked()) m_paramStack->setCurrentIndex(3);
    else if (m_sffmRadio->isChecked()) m_paramStack->setCurrentIndex(4);
    else if (m_pwlRadio->isChecked()) m_paramStack->setCurrentIndex(5);
    else if (m_behavioralRadio->isChecked()) m_paramStack->setCurrentIndex(6);
    else if (m_customRadio->isChecked()) {
        m_paramStack->setCurrentIndex(5);
        // Only open if it's the currently focused window or explicitly clicked
        // We use the guard inside onCustomDraw, but we can also check if we are 
        // in the middle of a setup/load pass.
        onCustomDraw();
    }
    else if (m_pwlFileRadio->isChecked()) m_paramStack->setCurrentIndex(0);
    else if (m_waveFileRadio->isChecked()) m_paramStack->setCurrentIndex(7);
}

void VoltageSourceLTSpiceDialog::onPwlBrowse() {
    QString fileName = QFileDialog::getOpenFileName(this, "Select PWL File", "", "Data Files (*.txt *.csv *.dat);;All Files (*)");
    if (!fileName.isEmpty()) {
        m_pwlFile->setText(fileName);
        m_pwlFileRadio->setChecked(true);
    }
}

void VoltageSourceLTSpiceDialog::onWaveBrowse() {
    const QString wavPath = QFileDialog::getOpenFileName(this, "Select WAV Audio File", m_projectDir, "WAV Audio Files (*.wav);;All Files (*)");
    if (wavPath.isEmpty()) {
        return;
    }

    DecodedWavData wavInfo;
    QString error;
    if (!decodeWavFile(wavPath, &wavInfo, &error)) {
        QMessageBox::warning(this, "WAV File Error", "Could not read WAV file.\n\n" + error);
        return;
    }

    m_waveFile->setText(wavPath);
    m_waveFileRadio->setChecked(true);
}

void VoltageSourceLTSpiceDialog::onWaveFileChanged() {
    updateWaveFileInfo();
}

void VoltageSourceLTSpiceDialog::updateWaveFileInfo() {
    QString path = m_waveFile->text().trimmed();
    double scale = m_waveScaleSpin ? m_waveScaleSpin->value() : 1.0;
    if (path.isEmpty()) {
        m_waveFileInfo->setText("<b>No WAV file selected.</b><br><br>"
                                "Select a WAV file to see its properties here.<br>"
                                "Supported: 8/16/24/32-bit PCM, 32/64-bit IEEE float.");
        return;
    }

    DecodedWavData wavInfo;
    QString error;
    if (!decodeWavFile(path, &wavInfo, &error)) {
        m_waveFileInfo->setText(QString("<b style='color: #cc0000;'>⚠ Error reading file</b><br><br>%1").arg(error));
        return;
    }

    double duration = wavInfo.channels.isEmpty() ? 0.0 : static_cast<double>(wavInfo.channels.first().size()) / wavInfo.sampleRate;

    QString channels;
    if (wavInfo.channelCount == 1) channels = "Mono";
    else if (wavInfo.channelCount == 2) channels = "Stereo";
    else channels = QString("%1 channels").arg(wavInfo.channelCount);

    QString channelNames;
    if (wavInfo.channelCount >= 1) {
        QStringList names;
        if (wavInfo.channelCount == 2) {
            names << "0: Left" << "1: Right";
        } else {
            for (int i = 0; i < wavInfo.channelCount; ++i) {
                names << QString("%1: Ch%2").arg(i).arg(i + 1);
            }
        }
        channelNames = "<br><b>Channels:</b> " + names.join(", ");
    }

    // Compute RMS and peak for selected channel
    int chanIdx = 0;
    bool ok = false;
    int inputChan = m_waveChan->text().toInt(&ok);
    if (ok && inputChan >= 0 && inputChan < wavInfo.channelCount) {
        chanIdx = inputChan;
    }

    QString scaleNote;
    if (std::abs(scale - 1.0) > 1e-6) {
        double db = 20.0 * std::log10(scale);
        scaleNote = QString("<br><b>⚡ Scale:</b> %1× (%2 dB)").arg(scale, 0, 'f', 3).arg(db, 0, 'f', 1);
    }

    if (!wavInfo.channels.isEmpty() && chanIdx < wavInfo.channels.size()) {
        const auto& samples = wavInfo.channels[chanIdx];
        double rms = 0.0;
        double peak = 0.0;
        for (double s : samples) {
            rms += s * s;
            double absS = std::abs(s);
            if (absS > peak) peak = absS;
        }
        if (!samples.isEmpty()) rms = std::sqrt(rms / samples.size());

        double effRms = rms * scale;
        double effPeak = peak * scale;
        channelNames += QString("<br><b>RMS (ch %1):</b> %2 V").arg(chanIdx).arg(effRms, 0, 'f', 6);
        channelNames += QString("<br><b>Peak (ch %1):</b> %2 V").arg(chanIdx).arg(effPeak, 0, 'f', 6);
    }

    m_waveFileInfo->setText(
        QString("<b style='color: #008800;'>✓ WAV file loaded</b><br><br>"
                "<b>File:</b> %1<br>"
                "<b>Sample Rate:</b> %2 Hz<br>"
                "<b>Format:</b> %3<br>"
                "<b>Duration:</b> %4 sec (%5 samples)")
            .arg(QFileInfo(path).fileName())
            .arg(wavInfo.sampleRate)
            .arg(channels)
            .arg(duration, 0, 'f', 3)
            .arg(wavInfo.channels.isEmpty() ? 0 : wavInfo.channels.first().size())
        + channelNames + scaleNote
    );
}

void VoltageSourceLTSpiceDialog::onWavBrowse() {
    const QString wavPath = QFileDialog::getOpenFileName(this, "Import WAV Audio", m_projectDir, "WAV Audio (*.wav)");
    if (wavPath.isEmpty()) {
        return;
    }

    DecodedWavData wavInfo;
    QString error;
    if (!decodeWavFile(wavPath, &wavInfo, &error)) {
        QMessageBox::warning(this, "WAV Import Failed", "Could not read WAV file.\n\n" + error);
        return;
    }

    QDialog options(this);
    options.setWindowTitle("WAV Import Options");
    QFormLayout* form = new QFormLayout(&options);

    QComboBox* channelCombo = new QComboBox(&options);
    for (int i = 0; i < wavInfo.channelCount; ++i) {
        channelCombo->addItem(QString("Channel %1").arg(i + 1), i);
    }
    form->addRow("Channel:", channelCombo);

    QDoubleSpinBox* scaleSpin = new QDoubleSpinBox(&options);
    scaleSpin->setRange(0.000001, 1000000.0);
    scaleSpin->setDecimals(6);
    scaleSpin->setValue(1.0);
    scaleSpin->setSuffix(" V");
    form->addRow("Peak Scale:", scaleSpin);

    QDoubleSpinBox* offsetSpin = new QDoubleSpinBox(&options);
    offsetSpin->setRange(-1000000.0, 1000000.0);
    offsetSpin->setDecimals(6);
    offsetSpin->setValue(0.0);
    offsetSpin->setSuffix(" V");
    form->addRow("DC Offset:", offsetSpin);

    QLabel* info = new QLabel(QString("Sample rate: %1 Hz\nFrames: %2\nChannels: %3")
                              .arg(wavInfo.sampleRate)
                              .arg(wavInfo.channels.isEmpty() ? 0 : wavInfo.channels.first().size())
                              .arg(wavInfo.channelCount), &options);
    form->addRow("", info);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &options);
    form->addRow("", buttons);
    connect(buttons, &QDialogButtonBox::accepted, &options, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &options, &QDialog::reject);
    if (options.exec() != QDialog::Accepted) {
        return;
    }

    QString pwlPath;
    if (!writePwlFromWav(wavPath,
                         m_projectDir,
                         m_item ? m_item->reference() : QString(),
                         channelCombo->currentData().toInt(),
                         scaleSpin->value(),
                         offsetSpin->value(),
                         &pwlPath,
                         &error)) {
        QMessageBox::warning(this, "WAV Import Failed", "Could not convert WAV file to a PWL source.\n\n" + error);
        return;
    }

    m_pwlFile->setText(pwlPath);
    m_pwlFileRadio->setChecked(true);
    m_paramStack->setCurrentIndex(0);
    QMessageBox::information(this, "WAV Imported",
                             QString("Audio was converted to a PWL file for ngspice.\n\nOutput: %1").arg(pwlPath));
}

void VoltageSourceLTSpiceDialog::onCustomDraw() {
    static bool isDrawing = false;
    if (isDrawing) return;
    isDrawing = true;

    auto result = VoltageSourceCustomWaveformDialog::execCustomDraw(
        this, m_projectDir, m_item ? (m_item->reference() + ".pwl") : "waveform.pwl"
    );

    if (result.accepted) {
        if (m_pwlRepeat) m_pwlRepeat->setChecked(result.repeat);

        if (result.saveToFile && !result.filePath.isEmpty()) {
            m_pwlFile->setText(result.filePath);
            m_pwlFileRadio->blockSignals(true);
            m_pwlFileRadio->setChecked(true);
            m_pwlFileRadio->blockSignals(false);
            m_paramStack->setCurrentIndex(0);
        } else if (!result.points.isEmpty()) {
            m_pwlPoints->setText(result.points);
            m_pwlRadio->blockSignals(true);
            m_pwlRadio->setChecked(true);
            m_pwlRadio->blockSignals(false);
            m_paramStack->setCurrentIndex(5);
        }
    }
    
    isDrawing = false;
}

void VoltageSourceLTSpiceDialog::loadFromItem() {
    // Functions
    m_noneRadio->setChecked(m_item->sourceType() == VoltageSourceItem::DC);
    m_pulseRadio->setChecked(m_item->sourceType() == VoltageSourceItem::Pulse);
    m_sineRadio->setChecked(m_item->sourceType() == VoltageSourceItem::Sine);
    m_expRadio->setChecked(m_item->sourceType() == VoltageSourceItem::EXP);
    m_sffmRadio->setChecked(m_item->sourceType() == VoltageSourceItem::SFFM);
    m_pwlRadio->setChecked(m_item->sourceType() == VoltageSourceItem::PWL);
    m_customRadio->setChecked(false);
    m_pwlFileRadio->setChecked(m_item->sourceType() == VoltageSourceItem::PWLFile);
    m_waveFileRadio->setChecked(m_item->sourceType() == VoltageSourceItem::WaveFile);
    m_behavioralRadio->setChecked(m_item->sourceType() == VoltageSourceItem::Behavioral);
    
    onFunctionChanged();

    // Pulse
    m_pulseV1->setText(m_item->pulseV1());
    m_pulseV2->setText(m_item->pulseV2());
    m_pulseTd->setText(m_item->pulseDelay());
    m_pulseTr->setText(m_item->pulseRise());
    m_pulseTf->setText(m_item->pulseFall());
    m_pulseTon->setText(m_item->pulseWidth());
    m_pulseTperiod->setText(m_item->pulsePeriod());
    m_pulseNcycles->setText(m_item->pulseNcycles());

    // Sine
    m_sineVoffset->setText(m_item->sineOffset());
    m_sineVamp->setText(m_item->sineAmplitude());
    m_sineFreq->setText(m_item->sineFrequency());
    m_sineTd->setText(m_item->sineDelay());
    m_sineTheta->setText(m_item->sineTheta());
    m_sinePhi->setText(m_item->sinePhi());
    m_sineNcycles->setText(m_item->sineNcycles());

    // EXP
    m_expV1->setText(m_item->expV1());
    m_expV2->setText(m_item->expV2());
    m_expTd1->setText(m_item->expTd1());
    m_expTau1->setText(m_item->expTau1());
    m_expTd2->setText(m_item->expTd2());
    m_expTau2->setText(m_item->expTau2());

    // SFFM
    m_sffmVoff->setText(m_item->sffmOff());
    m_sffmVamp->setText(m_item->sffmAmplit());
    m_sffmFcar->setText(m_item->sffmCarrier());
    m_sffmMdi->setText(m_item->sffmModIndex());
    m_sffmFsig->setText(m_item->sffmSignalFreq());

    // PWL
    m_pwlPoints->setText(m_item->pwlPoints());
    m_pwlFile->setText(m_item->pwlFile());
    m_pwlRepeat->setChecked(m_item->pwlRepeat());

    // WAVEFILE
    m_waveFile->setText(m_item->waveFile());
    m_waveChan->setText(QString::number(m_item->waveChan()));
    m_waveScaleSpin->setValue(m_item->waveScale());
    updateWaveFileInfo();

    // Behavioral
    m_behavioralExpr->setPlainText(m_item->value());
    const QString msg = validateBehavioralExpr(m_behavioralExpr->toPlainText());
    if (msg.isEmpty()) {
        m_behavioralStatus->setText("Expression looks good.");
        m_behavioralStatus->setStyleSheet("color: #008800; font-size: 11px;");
    } else {
        m_behavioralStatus->setText(msg);
        if (msg.contains("Unbalanced")) {
            m_behavioralStatus->setStyleSheet("color: #cc0000; font-size: 11px;");
        } else {
            m_behavioralStatus->setStyleSheet("color: #996600; font-size: 11px;");
        }
    }

    // Right Col
    m_dcValue->setText(m_item->dcVoltage());
    m_acAmplitude->setText(m_item->acAmplitude());
    m_acPhase->setText(m_item->acPhase());
    m_seriesRes->setText(m_item->seriesResistance());
    m_parallelCap->setText(m_item->parallelCapacitance());

    // Visibility
    m_functionVisible->setChecked(m_item->isFunctionVisible());
    m_dcVisible->setChecked(m_item->isDcVisible());
    m_acVisible->setChecked(m_item->isAcVisible());
    m_parasiticVisible->setChecked(m_item->isParasiticVisible());
}

void VoltageSourceLTSpiceDialog::onAccepted() {
    saveToItem();
    accept();
}

void VoltageSourceLTSpiceDialog::saveToItem() {
    if (m_undoStack) {
        m_undoStack->beginMacro("Update Voltage Source (LTspice Dialog)");
    }

    // Capture state
    VoltageSourceItem::SourceType type = VoltageSourceItem::DC;
    if (m_pulseRadio->isChecked()) type = VoltageSourceItem::Pulse;
    else if (m_sineRadio->isChecked()) type = VoltageSourceItem::Sine;
    else if (m_expRadio->isChecked()) type = VoltageSourceItem::EXP;
    else if (m_sffmRadio->isChecked()) type = VoltageSourceItem::SFFM;
    else if (m_pwlRadio->isChecked()) type = VoltageSourceItem::PWL;
    else if (m_customRadio->isChecked()) type = VoltageSourceItem::PWL;
    else if (m_pwlFileRadio->isChecked()) type = VoltageSourceItem::PWLFile;
    else if (m_waveFileRadio->isChecked()) type = VoltageSourceItem::WaveFile;
    else if (m_behavioralRadio->isChecked()) type = VoltageSourceItem::Behavioral;

    m_item->setSourceType(type);
    
    // Pulse
    m_item->setPulseV1(m_pulseV1->text());
    m_item->setPulseV2(m_pulseV2->text());
    m_item->setPulseDelay(m_pulseTd->text());
    m_item->setPulseRise(m_pulseTr->text());
    m_item->setPulseFall(m_pulseTf->text());
    m_item->setPulseWidth(m_pulseTon->text());
    m_item->setPulsePeriod(m_pulseTperiod->text());
    m_item->setPulseNcycles(m_pulseNcycles->text());

    // Sine
    m_item->setSineOffset(m_sineVoffset->text());
    m_item->setSineAmplitude(m_sineVamp->text());
    m_item->setSineFrequency(m_sineFreq->text());
    m_item->setSineDelay(m_sineTd->text());
    m_item->setSineTheta(m_sineTheta->text());
    m_item->setSinePhi(m_sinePhi->text());
    m_item->setSineNcycles(m_sineNcycles->text());

    // EXP
    m_item->setExpV1(m_expV1->text());
    m_item->setExpV2(m_expV2->text());
    m_item->setExpTd1(m_expTd1->text());
    m_item->setExpTau1(m_expTau1->text());
    m_item->setExpTd2(m_expTd2->text());
    m_item->setExpTau2(m_expTau2->text());

    // SFFM
    m_item->setSffmOff(m_sffmVoff->text());
    m_item->setSffmAmplit(m_sffmVamp->text());
    m_item->setSffmCarrier(m_sffmFcar->text());
    m_item->setSffmModIndex(m_sffmMdi->text());
    m_item->setSffmSignalFreq(m_sffmFsig->text());

    // PWL
    m_item->setPwlPoints(m_pwlPoints->text());
    m_item->setPwlFile(m_pwlFile->text());
    m_item->setPwlRepeat(m_pwlRepeat->isChecked());

    // WAVEFILE
    if (type == VoltageSourceItem::WaveFile) {
        m_item->setWaveFile(m_waveFile->text());
        bool ok = false;
        int chan = m_waveChan->text().toInt(&ok);
        if (!ok || chan < 0) chan = 0;
        m_item->setWaveChan(chan);
        m_item->setWaveScale(m_waveScaleSpin->value());
    }

    // Behavioral
    if (type == VoltageSourceItem::Behavioral) {
        const QString expr = normalizeBehavioralExpr(m_behavioralExpr->toPlainText());
        m_item->setValue(expr);
    }

    // Right Col
    m_item->setDcVoltage(m_dcValue->text());
    m_item->setAcAmplitude(m_acAmplitude->text());
    m_item->setAcPhase(m_acPhase->text());
    m_item->setSeriesResistance(m_seriesRes->text());
    m_item->setParallelCapacitance(m_parallelCap->text());

    // Visibility
    m_item->setFunctionVisible(m_functionVisible->isChecked());
    m_item->setDcVisible(m_dcVisible->isChecked());
    m_item->setAcVisible(m_acVisible->isChecked());
    m_item->setParasiticVisible(m_parasiticVisible->isChecked());

    if (m_undoStack) {
        m_undoStack->endMacro();
    }
    
    m_item->update();
}

bool VoltageSourceLTSpiceDialog::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_behavioralExpr && m_behavioralCompleter) {
        if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (m_behavioralCompleter->popup()->isVisible()) {
                switch (keyEvent->key()) {
                    case Qt::Key_Enter:
                    case Qt::Key_Return:
                    case Qt::Key_Escape:
                    case Qt::Key_Tab:
                    case Qt::Key_Backtab:
                        keyEvent->ignore();
                        return true;
                }
            }

            const bool ctrlSpace = (keyEvent->modifiers() & Qt::ControlModifier) && keyEvent->key() == Qt::Key_Space;
            if (ctrlSpace) {
                QTextCursor tc = m_behavioralExpr->textCursor();
                tc.select(QTextCursor::WordUnderCursor);
                QString prefix = tc.selectedText();
                if (prefix.isEmpty()) prefix = "V(";
                m_behavioralCompleter->setCompletionPrefix(prefix);
                m_behavioralCompleter->complete(m_behavioralExpr->cursorRect());
                return true;
            }
        }
    }

    return QDialog::eventFilter(obj, event);
}
