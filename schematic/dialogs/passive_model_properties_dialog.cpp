#include "passive_model_properties_dialog.h"

#include "passive_model_picker_dialog.h"
#include "../../pcb/dialogs/footprint_browser_dialog.h"
#include "../items/schematic_item.h"
#include "theme_manager.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace {
QString kindLabel(PassiveModelPropertiesDialog::Kind kind) {
    if (kind == PassiveModelPropertiesDialog::Kind::Resistor) return "Resistor";
    if (kind == PassiveModelPropertiesDialog::Kind::Capacitor) return "Capacitor";
    return "Inductor";
}

struct InductorValueParts {
    QString inductance;
    QString rser;
    QString rpar;
    QString cpar;
    QString ic;
};

InductorValueParts parseInductorValue(const QString& rawValue) {
    InductorValueParts parts;
    QString text = rawValue.trimmed();
    if (text.isEmpty()) {
        parts.inductance = "10u";
        parts.rser = "50m";
        parts.rpar = "100Meg";
        parts.cpar = "1p";
        parts.ic = "0";
        return parts;
    }

    const QStringList tokens = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (!tokens.isEmpty()) {
        parts.inductance = tokens.first();
    }

    auto extract = [&](const QString& key, QString* out) {
        static const QString patternTemplate = "\\b%1\\s*=\\s*([^\\s]+)";
        const QRegularExpression re(patternTemplate.arg(QRegularExpression::escape(key)), QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = re.match(text);
        if (match.hasMatch()) *out = match.captured(1).trimmed();
    };

    extract("Rser", &parts.rser);
    extract("Rpar", &parts.rpar);
    extract("Cpar", &parts.cpar);
    extract("ic", &parts.ic);

    if (parts.inductance.isEmpty()) parts.inductance = "10u";
    if (parts.rser.isEmpty()) parts.rser = "50m";
    if (parts.rpar.isEmpty()) parts.rpar = "100Meg";
    if (parts.cpar.isEmpty()) parts.cpar = "1p";
    if (parts.ic.isEmpty()) parts.ic = "0";
    return parts;
}
}

PassiveModelPropertiesDialog::PassiveModelPropertiesDialog(SchematicItem* item, Kind kind, QWidget* parent)
    : QDialog(parent), m_item(item), m_kind(kind) {
    setModal(true);
    setMinimumWidth(460);
    setWindowTitle(QString("%1 Properties - %2").arg(kindLabel(kind), item ? item->reference() : "?"));

    auto* mainLayout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);

    m_referenceEdit = new QLineEdit(item ? item->reference() : QString());
    form->addRow("Reference:", m_referenceEdit);

    const QString itemValue = item ? item->value() : QString();
    const InductorValueParts inductorParts = parseInductorValue(itemValue);

    m_valueEdit = new QLineEdit(kind == Kind::Inductor ? inductorParts.inductance : itemValue);
    if (kind == Kind::Resistor) {
        m_valueEdit->setPlaceholderText("e.g. 10k");
    } else if (kind == Kind::Capacitor) {
        m_valueEdit->setPlaceholderText("e.g. 100n");
    } else {
        m_valueEdit->setPlaceholderText("e.g. 10u");
    }
    form->addRow("Value:", m_valueEdit);

    auto* modelRow = new QHBoxLayout();
    m_spiceModelEdit = new QLineEdit(item ? item->spiceModel() : QString());
    m_spiceModelEdit->setPlaceholderText("Optional model name");
    auto* pickBtn = new QPushButton(QString("Pick %1 Model").arg(kindLabel(kind)));
    connect(pickBtn, &QPushButton::clicked, this, &PassiveModelPropertiesDialog::pickModel);
    modelRow->addWidget(m_spiceModelEdit, 1);
    modelRow->addWidget(pickBtn);
    form->addRow("SPICE Model:", modelRow);

    m_manufacturerEdit = new QLineEdit(item ? item->manufacturer() : QString());
    form->addRow("Manufacturer:", m_manufacturerEdit);

    m_mpnEdit = new QLineEdit(item ? item->mpn() : QString());
    form->addRow("MPN:", m_mpnEdit);

    auto* fpRow = new QHBoxLayout();
    m_footprintEdit = new QLineEdit(item ? item->footprint() : QString());
    m_footprintEdit->setPlaceholderText("Select a footprint");
    m_footprintEdit->setReadOnly(true);
    auto* fpBtn = new QPushButton("Pick Footprint");
    connect(fpBtn, &QPushButton::clicked, this, &PassiveModelPropertiesDialog::pickFootprint);
    fpRow->addWidget(m_footprintEdit, 1);
    fpRow->addWidget(fpBtn);
    form->addRow("Footprint:", fpRow);

    m_excludeSimCheck = new QCheckBox("Exclude from Simulation");
    m_excludeSimCheck->setChecked(item ? item->excludeFromSimulation() : false);
    form->addRow("", m_excludeSimCheck);

    m_excludePcbCheck = new QCheckBox("Exclude from PCB Editor");
    m_excludePcbCheck->setChecked(item ? item->excludeFromPcb() : false);
    form->addRow("", m_excludePcbCheck);

    mainLayout->addLayout(form);

    if (kind == Kind::Inductor) {
        m_inductorModelGroup = new QGroupBox("Inductor Model Defaults", this);
        auto* inductorLayout = new QFormLayout(m_inductorModelGroup);
        inductorLayout->setLabelAlignment(Qt::AlignRight);

        m_seriesResistanceEdit = new QLineEdit(inductorParts.rser, m_inductorModelGroup);
        m_seriesResistanceEdit->setPlaceholderText("e.g. 50m");
        inductorLayout->addRow("Series Resistance:", m_seriesResistanceEdit);

        m_parallelResistanceEdit = new QLineEdit(inductorParts.rpar, m_inductorModelGroup);
        m_parallelResistanceEdit->setPlaceholderText("e.g. 100Meg");
        inductorLayout->addRow("Parallel Resistance:", m_parallelResistanceEdit);

        m_parallelCapacitanceEdit = new QLineEdit(inductorParts.cpar, m_inductorModelGroup);
        m_parallelCapacitanceEdit->setPlaceholderText("e.g. 1p");
        inductorLayout->addRow("Parallel Capacitance:", m_parallelCapacitanceEdit);

        m_initialCurrentEdit = new QLineEdit(inductorParts.ic, m_inductorModelGroup);
        m_initialCurrentEdit->setPlaceholderText("e.g. 0");
        inductorLayout->addRow("Initial Current:", m_initialCurrentEdit);

        mainLayout->addWidget(m_inductorModelGroup);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    if (ThemeManager::theme()) {
        setStyleSheet(ThemeManager::theme()->widgetStylesheet());
    }
}

void PassiveModelPropertiesDialog::pickModel() {
    PassiveModelPickerDialog::Kind pickerKind =
        (m_kind == Kind::Resistor)
            ? PassiveModelPickerDialog::Kind::Resistor
            : (m_kind == Kind::Capacitor ? PassiveModelPickerDialog::Kind::Capacitor
                                         : PassiveModelPickerDialog::Kind::Inductor);
    PassiveModelPickerDialog dlg(pickerKind, this);
    if (dlg.exec() == QDialog::Accepted) {
        if (!dlg.selectedModel().isEmpty()) {
            m_spiceModelEdit->setText(dlg.selectedModel());
        }
        if (!dlg.selectedValue().isEmpty()) {
            m_valueEdit->setText(dlg.selectedValue());
        }
        if (!dlg.selectedManufacturer().isEmpty()) {
            m_manufacturerEdit->setText(dlg.selectedManufacturer());
        }
        if (!dlg.selectedMpn().isEmpty()) {
            m_mpnEdit->setText(dlg.selectedMpn());
        }
    }
}

void PassiveModelPropertiesDialog::pickFootprint() {
    FootprintBrowserDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        FootprintDefinition fp = dlg.selectedFootprint();
        if (!fp.name().isEmpty()) {
            m_footprintEdit->setText(fp.name());
        }
    }
}

QString PassiveModelPropertiesDialog::reference() const {
    return m_referenceEdit ? m_referenceEdit->text().trimmed() : QString();
}

QString PassiveModelPropertiesDialog::valueText() const {
    if (!m_valueEdit) return QString();

    const QString baseValue = m_valueEdit->text().trimmed();
    if (m_kind != Kind::Inductor) return baseValue;

    QStringList tokens;
    if (!baseValue.isEmpty()) tokens << baseValue;

    const auto appendParam = [&](const char* key, QLineEdit* edit) {
        if (!edit) return;
        const QString value = edit->text().trimmed();
        if (!value.isEmpty()) tokens << QString("%1=%2").arg(QString::fromLatin1(key), value);
    };

    appendParam("Rser", m_seriesResistanceEdit);
    appendParam("Rpar", m_parallelResistanceEdit);
    appendParam("Cpar", m_parallelCapacitanceEdit);
    appendParam("ic", m_initialCurrentEdit);
    return tokens.join(' ');
}

QString PassiveModelPropertiesDialog::spiceModel() const {
    return m_spiceModelEdit ? m_spiceModelEdit->text().trimmed() : QString();
}

QString PassiveModelPropertiesDialog::manufacturer() const {
    return m_manufacturerEdit ? m_manufacturerEdit->text().trimmed() : QString();
}

QString PassiveModelPropertiesDialog::mpn() const {
    return m_mpnEdit ? m_mpnEdit->text().trimmed() : QString();
}

QString PassiveModelPropertiesDialog::footprint() const {
    return m_footprintEdit ? m_footprintEdit->text().trimmed() : QString();
}

bool PassiveModelPropertiesDialog::excludeFromSimulation() const {
    return m_excludeSimCheck && m_excludeSimCheck->isChecked();
}

bool PassiveModelPropertiesDialog::excludeFromPcb() const {
    return m_excludePcbCheck && m_excludePcbCheck->isChecked();
}
