#include "spice_directive_dialog.h"
#include "../editor/schematic_commands.h"
#include "../ui/spice_highlighter.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QRegularExpression>
#include <QSet>

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

bool isCommentLine(const QString& line) {
    return line.startsWith('*') || line.startsWith(';') || line.startsWith('#');
}

bool isDirectiveLine(const QString& line) {
    return line.startsWith('.');
}

QString firstToken(const QString& line) {
    return line.section(QRegularExpression("\\s+"), 0, 0).trimmed();
}

}

SpiceDirectiveDialog::SpiceDirectiveDialog(SchematicSpiceDirectiveItem* item, QUndoStack* undoStack, QGraphicsScene* scene, QWidget* parent)
    : QDialog(parent), m_item(item), m_undoStack(undoStack), m_scene(scene), m_commandEdit(nullptr), m_validationLabel(nullptr), m_okButton(nullptr), m_cancelButton(nullptr), m_highlighter(nullptr)
{
    setupUi();
    loadFromItem();

    connect(m_okButton, &QPushButton::clicked, this, &SpiceDirectiveDialog::onAccepted);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_commandEdit, &QPlainTextEdit::textChanged, this, &SpiceDirectiveDialog::validateDirectiveText);

    validateDirectiveText();
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

    m_commandEdit = new QPlainTextEdit(this);
    m_commandEdit->setPlaceholderText("Vcc vcc 0 DC 15\nVee vee 0 DC -15\nVin in 0 SIN(0 1 1k)\n\nR1 in inv 10k\nR2 out inv 100k\n\nX1 0 inv out vcc vee opamp\n.subckt opamp 1 2 3 4 5\nE1 3 0 1 2 100k\nR1 3 0 1k\n.ends opamp\n\n.tran 10u 10m");
    QFont font("Courier New");
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(10);
    m_commandEdit->setFont(font);
    m_highlighter = new SpiceHighlighter(m_commandEdit->document());
    mainLayout->addWidget(m_commandEdit);

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
    const QStringList lines = text.split('\n');
    const QSet<QString> sceneRefs = schematicReferences(m_scene);

    QStringList errors;
    QStringList warnings;
    QStringList analysisCards;
    QMap<QString, int> elementLineByRef;
    QStringList subcktStack;

    for (int i = 0; i < lines.size(); ++i) {
        const QString rawLine = lines.at(i);
        const QString line = rawLine.trimmed();
        const int lineNo = i + 1;

        if (line.isEmpty() || isCommentLine(line) || line.startsWith('+')) continue;

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
            }

            static const QSet<QString> kAnalysisCards = {
                ".tran", ".ac", ".op", ".dc", ".noise", ".four", ".tf", ".disto", ".sens"
            };
            if (kAnalysisCards.contains(card)) {
                analysisCards.append(QString("%1 (line %2)").arg(card, QString::number(lineNo)));
            }

            continue;
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
