#include "ai_datasheet_import_dialog.h"
#include "../../python/python_manager.h"
#include "../models/symbol_primitive.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QDebug>
#include <QDir>
#include <QCoreApplication>

using namespace Flux::Model;

AIDatasheetImportDialog::AIDatasheetImportDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("AI Symbol Pin Generator"));
    resize(600, 500);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    QLabel* infoLabel = new QLabel(tr("Paste text from a datasheet (pin table, descriptions, etc.) below:"), this);
    mainLayout->addWidget(infoLabel);

    m_inputEdit = new QPlainTextEdit(this);
    m_inputEdit->setPlaceholderText(tr("Example:\n1 VCC Power Input\n2 GND Ground\n3-7 D0-D4 Data Lines..."));
    mainLayout->addWidget(m_inputEdit);

    m_statusLabel = new QLabel(tr("Ready."), this);
    mainLayout->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 0); // Indeterminate
    m_progressBar->hide();
    mainLayout->addWidget(m_progressBar);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_generateBtn = new QPushButton(tr("Generate Pins"), this);
    m_generateBtn->setStyleSheet("background-color: #3b82f6; color: white; font-weight: bold; padding: 8px;");
    
    QPushButton* cancelBtn = new QPushButton(tr("Cancel"), this);
    
    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(m_generateBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_generateBtn, &QPushButton::clicked, this, &AIDatasheetImportDialog::onGenerateClicked);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

AIDatasheetImportDialog::~AIDatasheetImportDialog() {
    if (m_process) {
        m_process->kill();
        m_process->deleteLater();
    }
}

void AIDatasheetImportDialog::onGenerateClicked() {
    QString rawText = m_inputEdit->toPlainText().trimmed();
    if (rawText.isEmpty()) {
        QMessageBox::warning(this, tr("Empty Input"), tr("Please paste some text first."));
        return;
    }

    m_generateBtn->setEnabled(false);
    m_inputEdit->setEnabled(false);
    m_progressBar->show();
    m_statusLabel->setText(tr("Extracting pins via Gemini AI..."));
    m_responseBuffer.clear();

    if (m_process) {
        m_process->kill();
        m_process->deleteLater();
    }

    m_process = new QProcess(this);
    m_process->setProcessEnvironment(PythonManager::getConfiguredEnvironment());
    
    connect(m_process, &QProcess::readyReadStandardOutput, this, &AIDatasheetImportDialog::onProcessReadyRead);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &AIDatasheetImportDialog::onProcessFinished);

    QString scriptPath = QDir(PythonManager::getScriptsDir()).absoluteFilePath("gemini_query.py");
    QString pythonExec = PythonManager::getPythonExecutable();

    QString prompt = QString(
        "You are an EDA expert. Extract pin information from the following datasheet text. "
        "Return ONLY a JSON array. Each object in the array MUST have: "
        "\"num\": integer (pin number), "
        "\"name\": string (pin name). "
        "Ignore headers and non-pin text. If one line describes multiple pins (e.g. 4-7 D0-D4), expand them. "
        "Return ONLY the valid JSON array without any markdown wrappers.\n\n"
        "TABLE TEXT:\n%1"
    ).arg(rawText);

    QStringList args;
    args << scriptPath << prompt << "--mode" << "symbol";

    m_process->start(pythonExec, args);
}

void AIDatasheetImportDialog::onProcessReadyRead() {
    m_responseBuffer += QString::fromUtf8(m_process->readAllStandardOutput());
}

void AIDatasheetImportDialog::onProcessFinished(int exitCode) {
    m_progressBar->hide();
    m_generateBtn->setEnabled(true);
    m_inputEdit->setEnabled(true);

    if (exitCode != 0) {
        m_statusLabel->setText(tr("Error: AI process failed."));
        QMessageBox::critical(this, tr("AI Error"), tr("The Gemini process failed with code %1.").arg(exitCode));
        return;
    }

    // Attempt to extract JSON if there's any commentary
    QString jsonStr = m_responseBuffer.trimmed();
    int start = jsonStr.indexOf('[');
    int end = jsonStr.lastIndexOf(']');
    if (start != -1 && end != -1 && end > start) {
        jsonStr = jsonStr.mid(start, end - start + 1);
    }

    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    if (doc.isNull() || !doc.isArray()) {
        m_statusLabel->setText(tr("Error: Failed to parse AI response."));
        qDebug() << "Failed JSON:" << jsonStr;
        QMessageBox::critical(this, tr("Parse Error"), tr("Failed to parse AI output as a JSON array."));
        return;
    }

    QJsonArray arr = doc.array();
    m_generatedPins.clear();
    
    // Default positioning: stack them vertically
    QPointF currentPos(0, 0);

    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject obj = arr[i].toObject();
        int num = obj["num"].toInt();
        QString name = obj["name"].toString();
        
        if (num > 0 && !name.isEmpty()) {
            SymbolPrimitive pin = SymbolPrimitive::createPin(currentPos, num, name, "Right");
            m_generatedPins.append(pin);
            currentPos.ry() += 100; // Space by 100 units
        }
    }

    if (m_generatedPins.isEmpty()) {
        m_statusLabel->setText(tr("AI found 0 pins."));
        QMessageBox::information(this, tr("No Pins Found"), tr("Gemini couldn't find any pins in the provided text."));
    } else {
        m_statusLabel->setText(tr("Successfully extracted %1 pins.").arg(m_generatedPins.size()));
        accept();
    }
}
