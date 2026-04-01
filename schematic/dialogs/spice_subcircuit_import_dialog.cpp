#include "spice_subcircuit_import_dialog.h"

#include "../ui/spice_highlighter.h"
#include "../../simulator/bridge/model_library_manager.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace {

QStringList collapseContinuationLines(const QString& text) {
    QStringList collapsed;
    QString current;

    const QStringList lines = text.split('\n');
    for (const QString& rawLine : lines) {
        const QString trimmed = rawLine.trimmed();
        if (trimmed.startsWith('+')) {
            const QString continuation = trimmed.mid(1).trimmed();
            if (current.isEmpty()) {
                current = continuation;
            } else if (!continuation.isEmpty()) {
                if (!current.endsWith(' ')) current += ' ';
                current += continuation;
            }
            continue;
        }

        if (!current.isEmpty()) collapsed.append(current);
        current = rawLine;
    }

    if (!current.isEmpty()) collapsed.append(current);
    return collapsed;
}

QString sanitizeFileStem(QString text) {
    text = text.trimmed();
    text.replace(QRegularExpression("[^A-Za-z0-9_.-]+"), "_");
    text.replace(QRegularExpression("_+"), "_");
    text.remove(QRegularExpression("^_+|_+$"));
    return text.isEmpty() ? QString("subckt_model") : text;
}

}

SpiceSubcircuitImportDialog::SpiceSubcircuitImportDialog(const QString& projectDir,
                                                         const QString& currentFilePath,
                                                         QWidget* parent)
    : QDialog(parent)
    , m_projectDir(projectDir)
    , m_currentFilePath(currentFilePath)
    , m_textEdit(nullptr)
    , m_nameEdit(nullptr)
    , m_fileNameEdit(nullptr)
    , m_pathPreviewLabel(nullptr)
    , m_statusLabel(nullptr)
    , m_pinTable(nullptr)
    , m_insertIncludeCheck(nullptr)
    , m_openSymbolEditorCheck(nullptr)
    , m_autoPlaceAfterSaveCheck(nullptr)
    , m_buttonBox(nullptr)
    , m_highlighter(nullptr) {
    setupUi();
    updateFromText();
}

void SpiceSubcircuitImportDialog::setupUi() {
    setWindowTitle("Import SPICE Subcircuit");
    resize(860, 680);

    auto* mainLayout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        "Paste a reusable .subckt/.ends block. The dialog extracts the subcircuit name and pins, "
        "saves it into your project library, and can insert an .include directive into the schematic.",
        this);
    intro->setWordWrap(true);
    mainLayout->addWidget(intro);

    m_textEdit = new QPlainTextEdit(this);
    m_textEdit->setPlaceholderText(
        ".subckt opamp 1 2 3 4 5\n"
        "E1 3 0 1 2 100k\n"
        "R1 3 0 1k\n"
        ".ends opamp");
    QFont mono("Courier New");
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(10);
    m_textEdit->setFont(mono);
    m_highlighter = new SpiceHighlighter(m_textEdit->document());
    mainLayout->addWidget(m_textEdit, 1);

    auto* formLayout = new QGridLayout();
    formLayout->addWidget(new QLabel("Subcircuit Name", this), 0, 0);
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setReadOnly(true);
    formLayout->addWidget(m_nameEdit, 0, 1);

    formLayout->addWidget(new QLabel("Library File", this), 1, 0);
    m_fileNameEdit = new QLineEdit(this);
    m_fileNameEdit->setPlaceholderText("opamp.lib");
    formLayout->addWidget(m_fileNameEdit, 1, 1);

    formLayout->addWidget(new QLabel("Save Path", this), 2, 0);
    m_pathPreviewLabel = new QLabel(this);
    m_pathPreviewLabel->setWordWrap(true);
    m_pathPreviewLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    formLayout->addWidget(m_pathPreviewLabel, 2, 1);
    mainLayout->addLayout(formLayout);

    auto* pinsLabel = new QLabel("Pin Mapping Setup", this);
    mainLayout->addWidget(pinsLabel);

    m_pinTable = new QTableWidget(this);
    m_pinTable->setColumnCount(3);
    m_pinTable->setHorizontalHeaderLabels({"Subckt Pin", "Symbol Pin #", "Symbol Label"});
    m_pinTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_pinTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_pinTable->horizontalHeader()->setStretchLastSection(true);
    m_pinTable->verticalHeader()->setVisible(false);
    m_pinTable->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_pinTable->setMinimumHeight(160);
    mainLayout->addWidget(m_pinTable);

    m_insertIncludeCheck = new QCheckBox("Insert an .include directive onto the current schematic", this);
    m_insertIncludeCheck->setChecked(true);
    mainLayout->addWidget(m_insertIncludeCheck);

    m_openSymbolEditorCheck = new QCheckBox("Generate a mapped symbol template and open it in Symbol Editor", this);
    m_openSymbolEditorCheck->setChecked(true);
    mainLayout->addWidget(m_openSymbolEditorCheck);

    m_autoPlaceAfterSaveCheck = new QCheckBox("Place the generated symbol in the schematic after the first save", this);
    m_autoPlaceAfterSaveCheck->setChecked(true);
    mainLayout->addWidget(m_autoPlaceAfterSaveCheck);
    m_autoPlaceAfterSaveCheck->setEnabled(m_openSymbolEditorCheck->isChecked());

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextFormat(Qt::PlainText);
    mainLayout->addWidget(m_statusLabel);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(m_buttonBox);

    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &SpiceSubcircuitImportDialog::onAccepted);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_textEdit, &QPlainTextEdit::textChanged, this, &SpiceSubcircuitImportDialog::updateFromText);
    connect(m_fileNameEdit, &QLineEdit::textChanged, this, &SpiceSubcircuitImportDialog::refreshPathPreview);
    connect(m_openSymbolEditorCheck, &QCheckBox::toggled, m_autoPlaceAfterSaveCheck, &QWidget::setEnabled);
}

QString SpiceSubcircuitImportDialog::baseDirectory() const {
    return QDir::homePath() + "/ViospiceLib";
}

QString SpiceSubcircuitImportDialog::suggestedFileName(const QString& subcktName) const {
    return sanitizeFileStem(subcktName) + ".lib";
}

QString SpiceSubcircuitImportDialog::targetAbsolutePath() const {
    const QString baseDir = baseDirectory();
    const QString fileName = m_fileNameEdit->text().trimmed();
    return QDir(baseDir).filePath(QStringLiteral("sub/%1").arg(fileName));
}

void SpiceSubcircuitImportDialog::refreshPathPreview() {
    m_pathPreviewLabel->setText(targetAbsolutePath());
}

void SpiceSubcircuitImportDialog::updateFromText() {
    const QString text = m_textEdit->toPlainText();
    const QStringList lines = collapseContinuationLines(text);
    const QString previousSubcktName = m_result.subcktName;

    QStringList errors;
    QStringList warnings;
    QString subcktName;
    QStringList pins;
    QStringList subcktStack;
    int subcktCount = 0;

    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines.at(i).trimmed();
        const int lineNo = i + 1;
        if (line.isEmpty() || line.startsWith('*') || line.startsWith(';') || line.startsWith('#')) continue;
        if (!line.startsWith('.')) continue;

        const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.isEmpty()) continue;
        const QString card = parts.first().toLower();

        if (card == ".subckt") {
            ++subcktCount;
            if (parts.size() < 2) {
                errors << QString("Line %1: .subckt is missing a subcircuit name.").arg(lineNo);
                continue;
            }
            if (subcktName.isEmpty()) {
                subcktName = parts.at(1);
                for (int p = 2; p < parts.size(); ++p) pins.append(parts.at(p));
            }
            subcktStack.append(parts.at(1));
        } else if (card == ".ends") {
            if (subcktStack.isEmpty()) {
                errors << QString("Line %1: .ends has no matching .subckt.").arg(lineNo);
            } else {
                const QString openName = subcktStack.takeLast();
                if (parts.size() >= 2 && parts.at(1).compare(openName, Qt::CaseInsensitive) != 0) {
                    errors << QString("Line %1: .ends %2 does not match open .subckt %3.").arg(lineNo).arg(parts.at(1), openName);
                }
            }
        }
    }

    if (subcktCount == 0) {
        errors << "No .subckt definition found.";
    } else if (subcktCount > 1) {
        warnings << "Multiple .subckt definitions found; only the first one will be used for metadata.";
    }
    if (!subcktStack.isEmpty()) {
        errors << QString("Missing .ends for subcircuit %1.").arg(subcktStack.last());
    }

    m_nameEdit->setText(subcktName);
    if (!subcktName.isEmpty() && (m_fileNameEdit->text().trimmed().isEmpty() || m_fileNameEdit->text().trimmed() == suggestedFileName(previousSubcktName))) {
        m_fileNameEdit->setText(suggestedFileName(subcktName));
    }
    refreshPathPreview();

    const QList<Result::PinMapping> previousMappings = m_result.pinMappings;

    m_pinTable->setRowCount(pins.size());
    for (int row = 0; row < pins.size(); ++row) {
        const QString subcktPin = pins.at(row);
        QString symbolLabel = subcktPin;
        int symbolPinNumber = row + 1;

        if (row < previousMappings.size()) {
            const Result::PinMapping& prev = previousMappings.at(row);
            if (prev.subcktPin.compare(subcktPin, Qt::CaseInsensitive) == 0) {
                if (!prev.symbolPinName.trimmed().isEmpty()) symbolLabel = prev.symbolPinName.trimmed();
                if (prev.symbolPinNumber > 0) symbolPinNumber = prev.symbolPinNumber;
            }
        }

        auto* subcktPinItem = new QTableWidgetItem(subcktPin);
        subcktPinItem->setFlags(subcktPinItem->flags() & ~Qt::ItemIsEditable);
        auto* symbolPinNumberItem = new QTableWidgetItem(QString::number(symbolPinNumber));
        auto* symbolLabelItem = new QTableWidgetItem(symbolLabel);

        m_pinTable->setItem(row, 0, subcktPinItem);
        m_pinTable->setItem(row, 1, symbolPinNumberItem);
        m_pinTable->setItem(row, 2, symbolLabelItem);
    }

    QStringList messages;
    if (!errors.isEmpty()) {
        messages << "Errors:";
        messages << errors;
    }
    if (!warnings.isEmpty()) {
        if (!messages.isEmpty()) messages << QString();
        messages << "Warnings:";
        messages << warnings;
    }

    if (messages.isEmpty()) {
        m_statusLabel->setStyleSheet("color: #15803d;");
        m_statusLabel->setText(QString("Ready to save %1 with %2 pin(s).").arg(subcktName.isEmpty() ? QString("subcircuit") : subcktName).arg(pins.size()));
    } else if (!errors.isEmpty()) {
        m_statusLabel->setStyleSheet("color: #b91c1c;");
        m_statusLabel->setText(messages.join('\n'));
    } else {
        m_statusLabel->setStyleSheet("color: #b45309;");
        m_statusLabel->setText(messages.join('\n'));
    }

    m_result.subcktName = subcktName;
    m_result.pins = pins;
    m_result.netlistText = text.trimmed();
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(errors.isEmpty() && !subcktName.isEmpty());
}

void SpiceSubcircuitImportDialog::onAccepted() {
    const QString subcktName = m_nameEdit->text().trimmed();
    QString fileName = m_fileNameEdit->text().trimmed();
    if (subcktName.isEmpty()) {
        QMessageBox::warning(this, "Import SPICE Subcircuit", "A valid .subckt definition is required.");
        return;
    }
    if (fileName.isEmpty()) fileName = suggestedFileName(subcktName);
    if (!fileName.contains('.')) fileName += ".lib";

    const QString baseDir = baseDirectory();
    QDir dir(baseDir);
    if (!dir.mkpath("sub")) {
        QMessageBox::warning(this, "Import SPICE Subcircuit", "Failed to create the global sub library folder.");
        return;
    }

    const QString absolutePath = QDir(baseDir).filePath(QStringLiteral("sub/%1").arg(fileName));
    QFile file(absolutePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::warning(this, "Import SPICE Subcircuit", QString("Failed to write %1.").arg(absolutePath));
        return;
    }

    QString netlistText = m_textEdit->toPlainText().trimmed();
    if (!netlistText.endsWith('\n')) netlistText += '\n';
    file.write(netlistText.toUtf8());
    file.close();

    const QString relativeIncludePath = QDir::fromNativeSeparators(QDir(baseDir).relativeFilePath(absolutePath));
    ModelLibraryManager::instance().reload();

    QList<Result::PinMapping> pinMappings;
    QSet<int> usedSymbolPinNumbers;
    for (int row = 0; row < m_pinTable->rowCount(); ++row) {
        QTableWidgetItem* subcktPinItem = m_pinTable->item(row, 0);
        QTableWidgetItem* symbolPinNumberItem = m_pinTable->item(row, 1);
        QTableWidgetItem* symbolLabelItem = m_pinTable->item(row, 2);
        if (!subcktPinItem || !symbolPinNumberItem || !symbolLabelItem) continue;

        Result::PinMapping mapping;
        mapping.subcktPin = subcktPinItem->text().trimmed();
        mapping.symbolPinName = symbolLabelItem->text().trimmed();
        mapping.symbolPinNumber = symbolPinNumberItem->text().toInt();

        if (mapping.subcktPin.isEmpty()) continue;
        if (mapping.symbolPinName.isEmpty()) mapping.symbolPinName = mapping.subcktPin;
        if (mapping.symbolPinNumber <= 0) {
            QMessageBox::warning(this, "Import SPICE Subcircuit", QString("Symbol pin number for %1 must be greater than zero.").arg(mapping.subcktPin));
            return;
        }
        if (usedSymbolPinNumbers.contains(mapping.symbolPinNumber)) {
            QMessageBox::warning(this, "Import SPICE Subcircuit", QString("Duplicate symbol pin number %1 in mapping table.").arg(mapping.symbolPinNumber));
            return;
        }
        usedSymbolPinNumbers.insert(mapping.symbolPinNumber);
        pinMappings.append(mapping);
    }

    m_result.subcktName = subcktName;
    m_result.fileName = fileName;
    m_result.absolutePath = absolutePath;
    m_result.relativeIncludePath = relativeIncludePath;
    m_result.netlistText = netlistText.trimmed();
    m_result.pinMappings = pinMappings;
    m_result.insertIncludeDirective = m_insertIncludeCheck->isChecked();
    m_result.openSymbolEditor = m_openSymbolEditorCheck->isChecked();
    m_result.autoPlaceAfterSave = m_openSymbolEditorCheck->isChecked() && m_autoPlaceAfterSaveCheck->isChecked();

    accept();
}

void SpiceSubcircuitImportDialog::setPreloadedNetlist(const QString& netlist) {
    if (m_textEdit) {
        m_textEdit->setPlainText(netlist);
    }
}
