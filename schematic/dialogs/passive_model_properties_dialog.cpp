#include "passive_model_properties_dialog.h"

#include "passive_model_picker_dialog.h"
#include "../items/schematic_item.h"
#include "../../core/theme_manager.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
QString kindLabel(PassiveModelPropertiesDialog::Kind kind) {
    return kind == PassiveModelPropertiesDialog::Kind::Resistor ? "Resistor" : "Capacitor";
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

    m_valueEdit = new QLineEdit(item ? item->value() : QString());
    m_valueEdit->setPlaceholderText(kind == Kind::Resistor ? "e.g. 10k" : "e.g. 100n");
    form->addRow("Value:", m_valueEdit);

    auto* modelRow = new QHBoxLayout();
    m_spiceModelEdit = new QLineEdit(item ? item->spiceModel() : QString());
    m_spiceModelEdit->setPlaceholderText("Optional model name");
    auto* pickBtn = new QPushButton(QString("Pick %1 Model").arg(kindLabel(kind)));
    connect(pickBtn, &QPushButton::clicked, this, &PassiveModelPropertiesDialog::pickModel);
    modelRow->addWidget(m_spiceModelEdit, 1);
    modelRow->addWidget(pickBtn);
    form->addRow("SPICE Model:", modelRow);

    m_excludeSimCheck = new QCheckBox("Exclude from Simulation");
    m_excludeSimCheck->setChecked(item ? item->excludeFromSimulation() : false);
    form->addRow("", m_excludeSimCheck);

    m_excludePcbCheck = new QCheckBox("Exclude from PCB Editor");
    m_excludePcbCheck->setChecked(item ? item->excludeFromPcb() : false);
    form->addRow("", m_excludePcbCheck);

    mainLayout->addLayout(form);

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
        (m_kind == Kind::Resistor) ? PassiveModelPickerDialog::Kind::Resistor : PassiveModelPickerDialog::Kind::Capacitor;
    PassiveModelPickerDialog dlg(pickerKind, this);
    if (dlg.exec() == QDialog::Accepted && !dlg.selectedModel().isEmpty()) {
        m_spiceModelEdit->setText(dlg.selectedModel());
    }
}

QString PassiveModelPropertiesDialog::reference() const {
    return m_referenceEdit ? m_referenceEdit->text().trimmed() : QString();
}

QString PassiveModelPropertiesDialog::valueText() const {
    return m_valueEdit ? m_valueEdit->text().trimmed() : QString();
}

QString PassiveModelPropertiesDialog::spiceModel() const {
    return m_spiceModelEdit ? m_spiceModelEdit->text().trimmed() : QString();
}

bool PassiveModelPropertiesDialog::excludeFromSimulation() const {
    return m_excludeSimCheck && m_excludeSimCheck->isChecked();
}

bool PassiveModelPropertiesDialog::excludeFromPcb() const {
    return m_excludePcbCheck && m_excludePcbCheck->isChecked();
}
