#include "spice_directive_dialog.h"
#include "../editor/schematic_commands.h"
#include "../ui/spice_highlighter.h"
#include "../items/power_item.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSplitter>
#include <QRegularExpression>
#include <QSet>
#include <QTextCursor>

namespace {

QSet<QString> schematicReferences(QGraphicsScene* scene) {
    QSet<QString> refs;
    if (!scene) return refs;

    for (QGraphicsItem* item : scene->items()) {
        auto* schematicItem = dynamic_cast<SchematicItem*>(item);
        if (!schematicItem) continue;
        if (schematicItem->itemType() == SchematicItem::SpiceDirectiveType) continue;

        const QString ref = schematicItem->reference().trimmed().toUpper();
        if (!ref.isEmpty()) refs.insert(ref);
    }

    return refs;
}

QSet<QString> schematicDirectiveModelNames(QGraphicsScene* scene, const SchematicSpiceDirectiveItem* self) {
    QSet<QString> names;
    if (!scene) return names;

    for (QGraphicsItem* item : scene->items()) {
        auto* directive = dynamic_cast<SchematicSpiceDirectiveItem*>(item);
        if (!directive || directive == self) continue;

        const QStringList lines = directive->text().split('\n');
        for (const QString& rawLine : lines) {
            const QString line = rawLine.trimmed();
            if (!line.startsWith(".model", Qt::CaseInsensitive)) continue;
            const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (parts.size() >= 2) names.insert(parts.at(1).toUpper());
        }
    }

    return names;
}

QSet<QString> powerRailNetNames(QGraphicsScene* scene) {
    QSet<QString> rails;
    if (!scene) return rails;

    for (QGraphicsItem* item : scene->items()) {
        auto* schematicItem = dynamic_cast<SchematicItem*>(item);
        if (!schematicItem) continue;
        if (schematicItem->itemType() != SchematicItem::PowerType) continue;

        const QString value = schematicItem->value().trimmed();
        if (!value.isEmpty()) rails.insert(value.toUpper());
        const QString ref = schematicItem->reference().trimmed();
        if (!ref.isEmpty()) rails.insert(ref.toUpper());
    }

    return rails;
}

bool isCommentLine(const QString& line) {
    return line.startsWith('*') || line.startsWith(';') || line.startsWith('#');
}

bool isDirectiveLine(const QString& line) {
    return line.startsWith('.');
}

QString firstToken(const QString& line) {
    return line.section(QRegularExpression("\\s+"), 0, 0).trimmed();
}

QStringList collapseSpiceContinuationLines(const QString& text) {
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

}

SpiceDirectiveDialog::SpiceDirectiveDialog(SchematicSpiceDirectiveItem* item, QUndoStack* undoStack, QGraphicsScene* scene, QWidget* parent)
    : QDialog(parent), m_item(item), m_undoStack(undoStack), m_scene(scene), m_commandEdit(nullptr), m_validationLabel(nullptr), m_previewEdit(nullptr), m_okButton(nullptr), m_cancelButton(nullptr), m_highlighter(nullptr)
{
    setupUi();
    loadFromItem();

    connect(m_okButton, &QPushButton::clicked, this, &SpiceDirectiveDialog::onAccepted);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_commandEdit, &QPlainTextEdit::textChanged, this, &SpiceDirectiveDialog::validateDirectiveText);
    connect(m_commandEdit, &QPlainTextEdit::textChanged, this, &SpiceDirectiveDialog::updatePreview);

    validateDirectiveText();
    updatePreview();
}

void SpiceDirectiveDialog::setupUi() {
    setWindowTitle("Edit SPICE Directive");
    resize(500, 300);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QLabel* infoLabel = new QLabel("Enter SPICE commands or LTspice-style netlist blocks:\n"
        ".tran .ac .op .dc .noise .four .tf .disto .sens\n"
        ".model .param .subckt .ends .include .lib .endl\n"
        ".func .global .ic .nodeset .options .temp .step .meas .print\n\n"
        "You can also paste component and subcircuit lines such as:\n"
        "Vcc vcc 0 DC 15\n"
        "X1 0 inv out vcc vee opamp\n"
        ".subckt opamp 1 2 3 4 5 ... .ends opamp", this);
    mainLayout->addWidget(infoLabel);

    QSplitter* splitter = new QSplitter(Qt::Vertical, this);

    m_commandEdit = new QPlainTextEdit(splitter);
    m_commandEdit->setPlaceholderText("Vcc vcc 0 DC 15\nVee vee 0 DC -15\nVin in 0 SIN(0 1 1k)\n\nR1 in inv 10k\nR2 out inv 100k\n\nX1 0 inv out vcc vee opamp\n.subckt opamp 1 2 3 4 5\nE1 3 0 1 2 100k\nR1 3 0 1k\n.ends opamp\n\n.tran 10u 10m");
    QFont font("Courier New");
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(10);
    m_commandEdit->setFont(font);
    m_highlighter = new SpiceHighlighter(m_commandEdit->document());

    m_previewEdit = new QTextBrowser(splitter);
    m_previewEdit->setOpenLinks(false);
    m_previewEdit->setReadOnly(true);
    m_previewEdit->setFont(font);
    m_previewEdit->setPlaceholderText("Directive preview will appear here.");

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    mainLayout->addWidget(splitter);

    m_validationLabel = new QLabel(this);
    m_validationLabel->setWordWrap(true);
    m_validationLabel->setTextFormat(Qt::PlainText);
    mainLayout->addWidget(m_validationLabel);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    m_cancelButton = new QPushButton("Cancel", this);
    m_okButton = new QPushButton("OK", this);
    m_okButton->setDefault(true);

    btnLayout->addWidget(m_cancelButton);
    btnLayout->addWidget(m_okButton);

    mainLayout->addLayout(btnLayout);
    
    // Apply styling if a theme is available would be nice, but QDialog usually inherits
}

void SpiceDirectiveDialog::loadFromItem() {
    if (m_item) {
        m_commandEdit->setPlainText(m_item->text());
    }
}

void SpiceDirectiveDialog::onAccepted() {
    saveToItem();
    accept();
}

void SpiceDirectiveDialog::saveToItem() {
    if (!m_item) return;

    QString newText = m_commandEdit->toPlainText().trimmed();
    QString oldText = m_item->text();

    if (newText != oldText) {
        if (m_undoStack && m_scene) {
            m_undoStack->push(new ChangePropertyCommand(m_scene, m_item, "Text", oldText, newText));
        } else {
            m_item->setText(newText);
            m_item->update();
        }
    }
}

void SpiceDirectiveDialog::validateDirectiveText() {
    if (!m_commandEdit || !m_validationLabel || !m_okButton) return;

    const QString text = m_commandEdit->toPlainText();
    const QStringList lines = collapseSpiceContinuationLines(text);
    const QSet<QString> sceneRefs = schematicReferences(m_scene);
    const QSet<QString> existingModelNames = schematicDirectiveModelNames(m_scene, m_item);
    const QSet<QString> powerRails = powerRailNetNames(m_scene);

    QStringList errors;
    QStringList warnings;
    QStringList analysisCards;
    QMap<QString, int> elementLineByRef;
    QMap<QString, int> modelLineByName;
    QStringList subcktStack;

    for (int i = 0; i < lines.size(); ++i) {
        const QString rawLine = lines.at(i);
        const QString line = rawLine.trimmed();
        const int lineNo = i + 1;

        if (line.isEmpty() || isCommentLine(line)) continue;

        if (isDirectiveLine(line)) {
            const QString card = firstToken(line).toLower();

            if (card == ".subckt") {
                const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                if (parts.size() < 2) {
                    errors << QString("Line %1: .subckt is missing a subcircuit name.").arg(lineNo);
                } else {
                    subcktStack.append(parts.at(1));
                }
            } else if (card == ".ends") {
                const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                const QString endName = parts.size() >= 2 ? parts.at(1) : QString();
                if (subcktStack.isEmpty()) {
                    errors << QString("Line %1: .ends has no matching .subckt.").arg(lineNo);
                } else {
                    const QString openName = subcktStack.takeLast();
                    if (!endName.isEmpty() && endName.compare(openName, Qt::CaseInsensitive) != 0) {
                        errors << QString("Line %1: .ends %2 does not match open .subckt %3.")
                                      .arg(lineNo)
                                      .arg(endName, openName);
                    }
                }
            } else if (card == ".model") {
                const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                if (parts.size() < 2) {
                    errors << QString("Line %1: .model is missing a model name.").arg(lineNo);
                } else {
                    const QString modelName = parts.at(1).toUpper();
                    if (modelLineByName.contains(modelName)) {
                        warnings << QString("Line %1: duplicate .model %2 (first seen on line %3).")
                                        .arg(lineNo)
                                        .arg(parts.at(1))
                                        .arg(modelLineByName.value(modelName));
                    } else {
                        modelLineByName.insert(modelName, lineNo);
                    }
                    if (existingModelNames.contains(modelName)) {
                        warnings << QString("Line %1: .model %2 duplicates a model declared in another directive block.")
                                        .arg(lineNo)
                                        .arg(parts.at(1));
                    }
                }

                if (line.contains(" D(", Qt::CaseInsensitive) &&
                    (line.contains("Ron=", Qt::CaseInsensitive) || line.contains("Roff=", Qt::CaseInsensitive) || line.contains("Vfwd=", Qt::CaseInsensitive))) {
                    warnings << QString("Line %1: LTspice-style diode model parameters Ron/Roff/Vfwd may fail in ngspice.").arg(lineNo);
                }
            }

            static const QSet<QString> kAnalysisCards = {
                ".tran", ".ac", ".op", ".dc", ".noise", ".four", ".tf", ".disto", ".sens"
            };
            if (kAnalysisCards.contains(card)) {
                analysisCards.append(QString("%1 (line %2)").arg(card, QString::number(lineNo)));
            }

            if (card == ".meas" && line.contains("I(", Qt::CaseInsensitive)) {
                warnings << QString("Line %1: I(R...) style .meas current expressions may not be portable to ngspice.").arg(lineNo);
            }

            continue;
        }

        if (line.contains("if(", Qt::CaseInsensitive)) {
            warnings << QString("Line %1: LTspice-style if(...) may fail in ngspice; prefer u(...) or let VioSpice rewrite simple cases during netlist generation.").arg(lineNo);
        }

        const QString token = firstToken(line);
        if (token.isEmpty()) continue;

        const QString normalizedRef = token.toUpper();
        if (elementLineByRef.contains(normalizedRef)) {
            warnings << QString("Line %1: duplicate element reference %2 (first seen on line %3).").arg(lineNo).arg(token).arg(elementLineByRef.value(normalizedRef));
        } else {
            elementLineByRef.insert(normalizedRef, lineNo);
        }

        if (sceneRefs.contains(normalizedRef)) {
            warnings << QString("Line %1: element reference %2 conflicts with an existing schematic reference.").arg(lineNo).arg(token);
        }

        const QChar prefix = normalizedRef.isEmpty() ? QChar() : normalizedRef.at(0);
        if ((prefix == 'V' || prefix == 'I') && !powerRails.isEmpty()) {
            const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (parts.size() >= 2) {
                const QString netName = parts.at(1).trimmed().toUpper();
                if (powerRails.contains(netName)) {
                    warnings << QString("Line %1: source %2 drives rail %3, which already exists as a schematic power rail.")
                                    .arg(lineNo)
                                    .arg(token)
                                    .arg(parts.at(1));
                }
            }
        }
    }

    if (!subcktStack.isEmpty()) {
        errors << QString("Missing .ends for subcircuit %1.").arg(subcktStack.last());
    }

    if (analysisCards.size() > 1) {
        warnings << QString("Multiple analysis cards in one directive block: %1.").arg(analysisCards.join(", "));
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
        m_validationLabel->setStyleSheet("color: #15803d;");
        m_validationLabel->setText("Validation: no obvious directive block problems found.");
    } else if (!errors.isEmpty()) {
        m_validationLabel->setStyleSheet("color: #b91c1c;");
        m_validationLabel->setText(messages.join('\n'));
    } else {
        m_validationLabel->setStyleSheet("color: #b45309;");
        m_validationLabel->setText(messages.join('\n'));
    }

    m_okButton->setEnabled(errors.isEmpty());
}

void SpiceDirectiveDialog::updatePreview() {
    if (!m_commandEdit || !m_previewEdit) return;

    const QString text = m_commandEdit->toPlainText().trimmed();
    if (text.isEmpty()) {
        m_previewEdit->setPlainText("Directive preview will appear here.");
        return;
    }

    QStringList preview;
    preview << "* SPICE block preview";

    const QStringList lines = collapseSpiceContinuationLines(text);
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) {
            preview << QString();
            continue;
        }
        if (isCommentLine(line)) {
            preview << line;
            continue;
        }
        preview << line;
    }

    m_previewEdit->setPlainText(preview.join('\n'));
    QTextCursor cursor = m_previewEdit->textCursor();
    cursor.movePosition(QTextCursor::Start);
    m_previewEdit->setTextCursor(cursor);
}
