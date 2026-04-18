#include "current_source_ltspice_dialog.h"
#include "voltage_source_custom_waveform_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QFileDialog>
#include <QCompleter>
#include <QStringListModel>
#include <QKeyEvent>
#include <QJSEngine>
#include <QInputDialog>
#include <QAbstractItemView>
#include <QSyntaxHighlighter>
#include <QTextDocument>
#include "../analysis/net_manager.h"
#include "../editor/schematic_commands.h"

namespace {
QString normalizeBehavioralExpr(QString expr) {
    expr = expr.trimmed();
    if (expr.isEmpty()) return "I=0";
    if (!expr.startsWith("I=", Qt::CaseInsensitive)) expr.prepend("I=");
    return expr;
}

QString validateBehavioralExpr(const QString& expr) {
    const QString v = expr.trimmed();
    if (v.isEmpty()) return "Expression is empty. Using I=0.";
    int depth = 0;
    for (const QChar& c : v) {
        if (c == '(') depth++;
        else if (c == ')') depth--;
        if (depth < 0) break;
    }
    if (depth != 0) return "Unbalanced parentheses.";
    if (!v.startsWith("I=", Qt::CaseInsensitive)) return "Missing 'I=' prefix. It will be added.";
    return QString();
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

CurrentSourceLTSpiceDialog::CurrentSourceLTSpiceDialog(CurrentSourceItem* item, QUndoStack* undoStack, QGraphicsScene* scene, const QString& projectDir, QWidget* parent)
    : QDialog(parent), m_item(item), m_undoStack(undoStack), m_scene(scene), m_projectDir(projectDir) {
    
    setWindowTitle("Independent Current Source - " + item->reference());
    setupUi();
    loadFromItem();
}

void CurrentSourceLTSpiceDialog::setupUi() {
    auto* mainLayout = new QHBoxLayout(this);

    auto* leftCol = new QVBoxLayout();
    auto* groupFunctions = new QGroupBox("Functions");
    auto* functionsLayout = new QVBoxLayout(groupFunctions);

    m_noneRadio = new QRadioButton("(none)");
    m_pulseRadio = new QRadioButton("PULSE(I1 I2 Tdelay Trise Tfall Ton Period Ncycles)");
    m_sineRadio = new QRadioButton("SINE(Ioffset Iamp Freq Td Theta Phi Ncycles)");
    m_expRadio = new QRadioButton("EXP(I1 I2 Td1 Tau1 Td2 Tau2)");
    m_sffmRadio = new QRadioButton("SFFM(Ioff Iamp Fcar MDI Fsig)");
    m_pwlRadio = new QRadioButton("PWL(t1 i1 t2 i2...)");
    m_behavioralRadio = new QRadioButton("Behavioral (BI: I=expression)");
    m_customRadio = new QRadioButton("CUSTOM (Draw)");
    m_pwlFileRadio = new QRadioButton("PWL FILE:");

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
    auto* browseBtn = new QPushButton("Browse");
    pwlFileLayout->addWidget(browseBtn);
    functionsLayout->addLayout(pwlFileLayout);
    
    connect(browseBtn, &QPushButton::clicked, this, &CurrentSourceLTSpiceDialog::onPwlBrowse);

    m_paramStack = new QStackedWidget();
    
    m_paramStack->addWidget(new QWidget());

    auto* pulsePage = new QWidget();
    auto* pulseLayout = new QGridLayout(pulsePage);
    pulseLayout->addWidget(new QLabel("Iinitial[A]:"), 0, 0); m_pulseI1 = new QLineEdit(); pulseLayout->addWidget(m_pulseI1, 0, 1);
    pulseLayout->addWidget(new QLabel("Ion[A]:"), 1, 0);     m_pulseI2 = new QLineEdit(); pulseLayout->addWidget(m_pulseI2, 1, 1);
    pulseLayout->addWidget(new QLabel("Tdelay[s]:"), 2, 0); m_pulseTd = new QLineEdit(); pulseLayout->addWidget(m_pulseTd, 2, 1);
    pulseLayout->addWidget(new QLabel("Trise[s]:"), 3, 0);  m_pulseTr = new QLineEdit(); pulseLayout->addWidget(m_pulseTr, 3, 1);
    pulseLayout->addWidget(new QLabel("Tfall[s]:"), 4, 0);  m_pulseTf = new QLineEdit(); pulseLayout->addWidget(m_pulseTf, 4, 1);
    pulseLayout->addWidget(new QLabel("Ton[s]:"), 5, 0);    m_pulseTon = new QLineEdit(); pulseLayout->addWidget(m_pulseTon, 5, 1);
    pulseLayout->addWidget(new QLabel("Tperiod[s]:"), 6, 0); m_pulseTperiod = new QLineEdit(); pulseLayout->addWidget(m_pulseTperiod, 6, 1);
    pulseLayout->addWidget(new QLabel("Ncycles:"), 7, 0);   m_pulseNcycles = new QLineEdit(); pulseLayout->addWidget(m_pulseNcycles, 7, 1);
    m_paramStack->addWidget(pulsePage);

    auto* sinePage = new QWidget();
    auto* sineLayout = new QGridLayout(sinePage);
    sineLayout->addWidget(new QLabel("Ioffset[A]:"), 0, 0); m_sineIoffset = new QLineEdit(); sineLayout->addWidget(m_sineIoffset, 0, 1);
    sineLayout->addWidget(new QLabel("Iamp[A]:"), 1, 0);    m_sineIamp = new QLineEdit(); sineLayout->addWidget(m_sineIamp, 1, 1);
    sineLayout->addWidget(new QLabel("Freq[Hz]:"), 2, 0);   m_sineFreq = new QLineEdit(); sineLayout->addWidget(m_sineFreq, 2, 1);
    sineLayout->addWidget(new QLabel("Td[s]:"), 3, 0);     m_sineTd = new QLineEdit(); sineLayout->addWidget(m_sineTd, 3, 1);
    sineLayout->addWidget(new QLabel("Theta[1/s]:"), 4, 0); m_sineTheta = new QLineEdit(); sineLayout->addWidget(m_sineTheta, 4, 1);
    sineLayout->addWidget(new QLabel("Phi[deg]:"), 5, 0);   m_sinePhi = new QLineEdit(); sineLayout->addWidget(m_sinePhi, 5, 1);
    sineLayout->addWidget(new QLabel("Ncycles:"), 6, 0);    m_sineNcycles = new QLineEdit(); sineLayout->addWidget(m_sineNcycles, 6, 1);
    m_paramStack->addWidget(sinePage);

    auto* expPage = new QWidget();
    auto* expLayout = new QGridLayout(expPage);
    expLayout->addWidget(new QLabel("I1[A]:"), 0, 0);   m_expI1 = new QLineEdit(); expLayout->addWidget(m_expI1, 0, 1);
    expLayout->addWidget(new QLabel("I2[A]:"), 1, 0);   m_expI2 = new QLineEdit(); expLayout->addWidget(m_expI2, 1, 1);
    expLayout->addWidget(new QLabel("Td1[s]:"), 2, 0);  m_expTd1 = new QLineEdit(); expLayout->addWidget(m_expTd1, 2, 1);
    expLayout->addWidget(new QLabel("Tau1[s]:"), 3, 0); m_expTau1 = new QLineEdit(); expLayout->addWidget(m_expTau1, 3, 1);
    expLayout->addWidget(new QLabel("Td2[s]:"), 4, 0);  m_expTd2 = new QLineEdit(); expLayout->addWidget(m_expTd2, 4, 1);
    expLayout->addWidget(new QLabel("Tau2[s]:"), 5, 0); m_expTau2 = new QLineEdit(); expLayout->addWidget(m_expTau2, 5, 1);
    m_paramStack->addWidget(expPage);

    auto* sffmPage = new QWidget();
    auto* sffmLayout = new QGridLayout(sffmPage);
    sffmLayout->addWidget(new QLabel("Ioff[A]:"), 0, 0);  m_sffmIoff = new QLineEdit(); sffmLayout->addWidget(m_sffmIoff, 0, 1);
    sffmLayout->addWidget(new QLabel("Iamp[A]:"), 1, 0);  m_sffmIamp = new QLineEdit(); sffmLayout->addWidget(m_sffmIamp, 1, 1);
    sffmLayout->addWidget(new QLabel("Fcar[Hz]:"), 2, 0); m_sffmFcar = new QLineEdit(); sffmLayout->addWidget(m_sffmFcar, 2, 1);
    sffmLayout->addWidget(new QLabel("MDI:"), 3, 0);      m_sffmMdi = new QLineEdit(); sffmLayout->addWidget(m_sffmMdi, 3, 1);
    sffmLayout->addWidget(new QLabel("Fsig[Hz]:"), 4, 0); m_sffmFsig = new QLineEdit(); sffmLayout->addWidget(m_sffmFsig, 4, 1);
    m_paramStack->addWidget(sffmPage);

    auto* pwlPage = new QWidget();
    auto* pwlLayout = new QVBoxLayout(pwlPage);
    pwlLayout->addWidget(new QLabel("Values(t1 i1 t2 i2...):"));
    auto* pwlRow = new QHBoxLayout();
    m_pwlPoints = new QLineEdit();
    m_pwlDrawBtn = new QPushButton("Draw...");
    pwlRow->addWidget(m_pwlPoints);
    pwlRow->addWidget(m_pwlDrawBtn);
    pwlLayout->addLayout(pwlRow);
    m_pwlRepeat = new QCheckBox("Repeat (PWL r=0)");
    pwlLayout->addWidget(m_pwlRepeat);
    m_paramStack->addWidget(pwlPage);

    auto* behavioralPage = new QWidget();
    auto* behavioralLayout = new QVBoxLayout(behavioralPage);
    behavioralLayout->addWidget(new QLabel("Expression (use I=...):"));
    m_behavioralExpr = new QPlainTextEdit();
    m_behavioralExpr->setPlaceholderText("I=I(Vsrc)*2");
    m_behavioralExpr->setMinimumHeight(90);
    behavioralLayout->addWidget(m_behavioralExpr);
    new BehavioralHighlighter(m_behavioralExpr->document());

    m_behavioralStatus = new QLabel();
    m_behavioralStatus->setStyleSheet("color: #cc0000; font-size: 11px;");
    behavioralLayout->addWidget(m_behavioralStatus);

    auto* previewRow = new QHBoxLayout();
    previewRow->addWidget(new QLabel("Preview I:"));
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

    functionsLayout->addWidget(m_paramStack);
    
    m_functionVisible = new QCheckBox("Make this information visible on schematic:");
    functionsLayout->addWidget(m_functionVisible);
    
    leftCol->addWidget(groupFunctions);

    auto* rightCol = new QVBoxLayout();
    
    auto* groupDc = new QGroupBox("DC Value");
    auto* dcLayout = new QGridLayout(groupDc);
    dcLayout->addWidget(new QLabel("DC value:"), 0, 0);
    m_dcValue = new QLineEdit();
    dcLayout->addWidget(m_dcValue, 0, 1);
    m_dcVisible = new QCheckBox("Make this information visible on schematic:");
    dcLayout->addWidget(m_dcVisible, 1, 0, 1, 2);
    rightCol->addWidget(groupDc);

    auto* groupAc = new QGroupBox("Small signal AC analysis(.AC)");
    auto* acLayout = new QGridLayout(groupAc);
    acLayout->addWidget(new QLabel("AC Amplitude:"), 0, 0); m_acAmplitude = new QLineEdit(); acLayout->addWidget(m_acAmplitude, 0, 1);
    acLayout->addWidget(new QLabel("AC Phase:"), 1, 0);     m_acPhase = new QLineEdit(); acLayout->addWidget(m_acPhase, 1, 1);
    m_acVisible = new QCheckBox("Make this information visible on schematic:");
    acLayout->addWidget(m_acVisible, 2, 0, 1, 2);
    rightCol->addWidget(groupAc);

    auto* groupPara = new QGroupBox("Parasitic Properties");
    auto* paraLayout = new QGridLayout(groupPara);
    paraLayout->addWidget(new QLabel("Series Resistance[Ω]:"), 0, 0); m_seriesRes = new QLineEdit(); paraLayout->addWidget(m_seriesRes, 0, 1);
    paraLayout->addWidget(new QLabel("Parallel Capacitance[F]:"), 1, 0); m_parallelCap = new QLineEdit(); paraLayout->addWidget(m_parallelCap, 1, 1);
    m_parasiticVisible = new QCheckBox("Make this information visible on schematic:");
    paraLayout->addWidget(m_parasiticVisible, 2, 0, 1, 2);
    rightCol->addWidget(groupPara);

    auto* btnLayout = new QHBoxLayout();
    auto* cancelBtn = new QPushButton("Cancel");
    auto* okBtn = new QPushButton("OK");
    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(okBtn);
    rightCol->addLayout(btnLayout);

    mainLayout->addLayout(leftCol);
    mainLayout->addLayout(rightCol);

    connect(m_noneRadio, &QRadioButton::toggled, this, &CurrentSourceLTSpiceDialog::onFunctionChanged);
    connect(m_pulseRadio, &QRadioButton::toggled, this, &CurrentSourceLTSpiceDialog::onFunctionChanged);
    connect(m_sineRadio, &QRadioButton::toggled, this, &CurrentSourceLTSpiceDialog::onFunctionChanged);
    connect(m_expRadio, &QRadioButton::toggled, this, &CurrentSourceLTSpiceDialog::onFunctionChanged);
    connect(m_sffmRadio, &QRadioButton::toggled, this, &CurrentSourceLTSpiceDialog::onFunctionChanged);
    connect(m_pwlRadio, &QRadioButton::toggled, this, &CurrentSourceLTSpiceDialog::onFunctionChanged);
    connect(m_behavioralRadio, &QRadioButton::toggled, this, &CurrentSourceLTSpiceDialog::onFunctionChanged);
    connect(m_customRadio, &QRadioButton::toggled, this, &CurrentSourceLTSpiceDialog::onFunctionChanged);
    connect(m_pwlFileRadio, &QRadioButton::toggled, this, &CurrentSourceLTSpiceDialog::onFunctionChanged);

    connect(okBtn, &QPushButton::clicked, this, &CurrentSourceLTSpiceDialog::onAccepted);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_pwlDrawBtn, &QPushButton::clicked, this, &CurrentSourceLTSpiceDialog::onCustomDraw);

    m_behavioralCompleter = new QCompleter(this);
    m_behavioralCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    m_behavioralCompleter->setCompletionMode(QCompleter::PopupCompletion);
    m_behavioralCompleter->setWidget(m_behavioralExpr);
    m_behavioralExpr->installEventFilter(this);

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

        QString preview = "Preview: ";
        QString normalized = normalizeBehavioralExpr(expr);
        QString evalExpr = normalized;
        evalExpr.remove(0, 2);
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

    updateBehavioralUi();
}

void CurrentSourceLTSpiceDialog::onFunctionChanged() {
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
        onCustomDraw();
    }
    else if (m_pwlFileRadio->isChecked()) m_paramStack->setCurrentIndex(0);
}

void CurrentSourceLTSpiceDialog::onPwlBrowse() {
    QString fileName = QFileDialog::getOpenFileName(this, "Select PWL File", "", "Data Files (*.txt *.csv *.dat);;All Files (*)");
    if (!fileName.isEmpty()) {
        m_pwlFile->setText(fileName);
        m_pwlFileRadio->setChecked(true);
    }
}

void CurrentSourceLTSpiceDialog::onCustomDraw() {
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

void CurrentSourceLTSpiceDialog::loadFromItem() {
    m_noneRadio->setChecked(m_item->sourceType() == CurrentSourceItem::DC);
    m_pulseRadio->setChecked(m_item->sourceType() == CurrentSourceItem::Pulse);
    m_sineRadio->setChecked(m_item->sourceType() == CurrentSourceItem::Sine);
    m_expRadio->setChecked(m_item->sourceType() == CurrentSourceItem::EXP);
    m_sffmRadio->setChecked(m_item->sourceType() == CurrentSourceItem::SFFM);
    m_pwlRadio->setChecked(m_item->sourceType() == CurrentSourceItem::PWL);
    m_customRadio->setChecked(false);
    m_pwlFileRadio->setChecked(m_item->sourceType() == CurrentSourceItem::PWLFile);
    m_behavioralRadio->setChecked(m_item->sourceType() == CurrentSourceItem::Behavioral);
    
    onFunctionChanged();

    m_pulseI1->setText(m_item->pulseI1());
    m_pulseI2->setText(m_item->pulseI2());
    m_pulseTd->setText(m_item->pulseDelay());
    m_pulseTr->setText(m_item->pulseRise());
    m_pulseTf->setText(m_item->pulseFall());
    m_pulseTon->setText(m_item->pulseWidth());
    m_pulseTperiod->setText(m_item->pulsePeriod());
    m_pulseNcycles->setText(m_item->pulseNcycles());

    m_sineIoffset->setText(m_item->sineOffset());
    m_sineIamp->setText(m_item->sineAmplitude());
    m_sineFreq->setText(m_item->sineFrequency());
    m_sineTd->setText(m_item->sineDelay());
    m_sineTheta->setText(m_item->sineTheta());
    m_sinePhi->setText(m_item->sinePhi());
    m_sineNcycles->setText(m_item->sineNcycles());

    m_expI1->setText(m_item->expI1());
    m_expI2->setText(m_item->expI2());
    m_expTd1->setText(m_item->expTd1());
    m_expTau1->setText(m_item->expTau1());
    m_expTd2->setText(m_item->expTd2());
    m_expTau2->setText(m_item->expTau2());

    m_sffmIoff->setText(m_item->sffmOff());
    m_sffmIamp->setText(m_item->sffmAmplit());
    m_sffmFcar->setText(m_item->sffmCarrier());
    m_sffmMdi->setText(m_item->sffmModIndex());
    m_sffmFsig->setText(m_item->sffmSignalFreq());

    m_pwlPoints->setText(m_item->pwlPoints());
    m_pwlFile->setText(m_item->pwlFile());
    m_pwlRepeat->setChecked(m_item->pwlRepeat());

    m_behavioralExpr->setPlainText(m_item->value());

    m_dcValue->setText(m_item->dcCurrent());
    m_acAmplitude->setText(m_item->acAmplitude());
    m_acPhase->setText(m_item->acPhase());
    m_seriesRes->setText(m_item->seriesResistance());
    m_parallelCap->setText(m_item->parallelCapacitance());

    m_functionVisible->setChecked(m_item->isFunctionVisible());
    m_dcVisible->setChecked(m_item->isDcVisible());
    m_acVisible->setChecked(m_item->isAcVisible());
    m_parasiticVisible->setChecked(m_item->isParasiticVisible());
}

void CurrentSourceLTSpiceDialog::onAccepted() {
    saveToItem();
    accept();
}

void CurrentSourceLTSpiceDialog::saveToItem() {
    if (m_undoStack) {
        m_undoStack->beginMacro("Update Current Source (LTspice Dialog)");
    }

    CurrentSourceItem::SourceType type = CurrentSourceItem::DC;
    if (m_pulseRadio->isChecked()) type = CurrentSourceItem::Pulse;
    else if (m_sineRadio->isChecked()) type = CurrentSourceItem::Sine;
    else if (m_expRadio->isChecked()) type = CurrentSourceItem::EXP;
    else if (m_sffmRadio->isChecked()) type = CurrentSourceItem::SFFM;
    else if (m_pwlRadio->isChecked()) type = CurrentSourceItem::PWL;
    else if (m_customRadio->isChecked()) type = CurrentSourceItem::PWL;
    else if (m_pwlFileRadio->isChecked()) type = CurrentSourceItem::PWLFile;
    else if (m_behavioralRadio->isChecked()) type = CurrentSourceItem::Behavioral;

    m_item->setSourceType(type);
    
    m_item->setPulseI1(m_pulseI1->text());
    m_item->setPulseI2(m_pulseI2->text());
    m_item->setPulseDelay(m_pulseTd->text());
    m_item->setPulseRise(m_pulseTr->text());
    m_item->setPulseFall(m_pulseTf->text());
    m_item->setPulseWidth(m_pulseTon->text());
    m_item->setPulsePeriod(m_pulseTperiod->text());
    m_item->setPulseNcycles(m_pulseNcycles->text());

    m_item->setSineOffset(m_sineIoffset->text());
    m_item->setSineAmplitude(m_sineIamp->text());
    m_item->setSineFrequency(m_sineFreq->text());
    m_item->setSineDelay(m_sineTd->text());
    m_item->setSineTheta(m_sineTheta->text());
    m_item->setSinePhi(m_sinePhi->text());
    m_item->setSineNcycles(m_sineNcycles->text());

    m_item->setExpI1(m_expI1->text());
    m_item->setExpI2(m_expI2->text());
    m_item->setExpTd1(m_expTd1->text());
    m_item->setExpTau1(m_expTau1->text());
    m_item->setExpTd2(m_expTd2->text());
    m_item->setExpTau2(m_expTau2->text());

    m_item->setSffmOff(m_sffmIoff->text());
    m_item->setSffmAmplit(m_sffmIamp->text());
    m_item->setSffmCarrier(m_sffmFcar->text());
    m_item->setSffmModIndex(m_sffmMdi->text());
    m_item->setSffmSignalFreq(m_sffmFsig->text());

    m_item->setPwlPoints(m_pwlPoints->text());
    m_item->setPwlFile(m_pwlFile->text());
    m_item->setPwlRepeat(m_pwlRepeat->isChecked());

    if (type == CurrentSourceItem::Behavioral) {
        const QString expr = normalizeBehavioralExpr(m_behavioralExpr->toPlainText());
        m_item->setValue(expr);
    }

    m_item->setDcCurrent(m_dcValue->text());
    m_item->setAcAmplitude(m_acAmplitude->text());
    m_item->setAcPhase(m_acPhase->text());
    m_item->setSeriesResistance(m_seriesRes->text());
    m_item->setParallelCapacitance(m_parallelCap->text());

    m_item->setFunctionVisible(m_functionVisible->isChecked());
    m_item->setDcVisible(m_dcVisible->isChecked());
    m_item->setAcVisible(m_acVisible->isChecked());
    m_item->setParasiticVisible(m_parasiticVisible->isChecked());

    if (m_undoStack) {
        m_undoStack->endMacro();
    }
    
    m_item->update();
}

bool CurrentSourceLTSpiceDialog::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_behavioralExpr) {
        if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Space && (keyEvent->modifiers() & Qt::ControlModifier)) {
                return true;
            }
        }
    }
    return QDialog::eventFilter(obj, event);
}
