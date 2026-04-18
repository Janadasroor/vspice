#include "mesfet_properties_dialog.h"

#include "../items/schematic_item.h"
#include "theme_manager.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

MesfetPropertiesDialog::MesfetPropertiesDialog(SchematicItem* item, QWidget* parent)
    : QDialog(parent), m_item(item) {
    setWindowTitle(QString("MESFET Properties - %1").arg(item ? item->reference() : "Z?"));
    setModal(true);
    setMinimumWidth(460);

    auto* mainLayout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);

    m_modelNameEdit = new QLineEdit();
    m_modelNameEdit->setPlaceholderText("e.g. MyMesfet");
    QString initialModel = item ? item->spiceModel().trimmed() : QString();
    if (initialModel.isEmpty() && item) {
        initialModel = item->value().trimmed();
    }
    m_modelNameEdit->setText(initialModel);
    if (m_modelNameEdit->text().isEmpty()) {
        m_modelNameEdit->setText("MyMesfet");
    }
    form->addRow("Model Name:", m_modelNameEdit);
    mainLayout->addLayout(form);

    mainLayout->addWidget(new QLabel("SPICE Preview:"));
    m_previewEdit = new QLineEdit();
    m_previewEdit->setReadOnly(true);
    m_previewEdit->setStyleSheet("color: #3b82f6; font-family: 'Courier New';");
    mainLayout->addWidget(m_previewEdit);

    auto* info = new QLabel("Use the same name in a directive, e.g.\n"
                            ".model MyMesfet NMF(Vto=-2.1 Beta=0.05 Lambda=0.02 Alpha=3 B=0.5 Rd=1 Rs=1 Cgs=1p Cgd=0.2p)");
    info->setWordWrap(true);
    mainLayout->addWidget(info);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &MesfetPropertiesDialog::applyChanges);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    connect(m_modelNameEdit, &QLineEdit::textChanged, this, &MesfetPropertiesDialog::updatePreview);
    updatePreview();

    if (ThemeManager::theme()) {
        setStyleSheet(ThemeManager::theme()->widgetStylesheet());
    }
}

QString MesfetPropertiesDialog::modelName() const {
    return m_modelNameEdit ? m_modelNameEdit->text().trimmed() : QString();
}

void MesfetPropertiesDialog::updatePreview() {
    QString model = modelName();
    if (model.isEmpty()) model = "MyMesfet";
    if (m_previewEdit) {
        m_previewEdit->setText(QString(".model %1 NMF(Vto=-2.1 Beta=0.05 Lambda=0.02 Alpha=3 B=0.5 Rd=1 Rs=1 Cgs=1p Cgd=0.2p)").arg(model));
    }
}

void MesfetPropertiesDialog::applyChanges() {
    accept();
}
