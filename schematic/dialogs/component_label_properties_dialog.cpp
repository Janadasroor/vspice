#include "component_label_properties_dialog.h"
#include "../items/generic_component_item.h"
#include "../../core/theme_manager.h"
#include "../../simulator/bridge/model_library_manager.h"

#include <QCompleter>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

ComponentLabelPropertiesDialog::ComponentLabelPropertiesDialog(GenericComponentItem* component, LabelType labelType, QWidget* parent)
    : QDialog(parent), m_component(component), m_labelType(labelType) {
    QString title = (labelType == Reference) ? "Reference Properties" : "Value Properties";
    setWindowTitle(title);
    setModal(true);
    setMinimumWidth(400);

    setupUI();
    loadValues();

    if (ThemeManager::theme()) {
        setStyleSheet(ThemeManager::theme()->widgetStylesheet());
    }
}

void ComponentLabelPropertiesDialog::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);

    // Text field with model autocomplete
    m_textEdit = new QLineEdit();
    if (m_labelType == Reference) {
        m_textEdit->setPlaceholderText("e.g. M1, Q1, D1");
    } else {
        m_textEdit->setPlaceholderText("e.g. 2N7000, 1N4148, 10k");
        // Add autocomplete completer with all library models
        QStringList allModels;
        for (const auto& info : ModelLibraryManager::instance().allModels()) {
            allModels.append(info.name);
        }
        allModels.sort(Qt::CaseInsensitive);
        allModels.removeDuplicates();
        auto* completer = new QCompleter(allModels, this);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setFilterMode(Qt::MatchContains);
        completer->setCompletionMode(QCompleter::PopupCompletion);
        m_textEdit->setCompleter(completer);
    }
    form->addRow(m_labelType == Reference ? "Reference:" : "Value:", m_textEdit);

    mainLayout->addLayout(form);

    // SPICE Preview
    mainLayout->addWidget(new QLabel("SPICE Preview:"));
    m_commandPreview = new QLineEdit();
    m_commandPreview->setReadOnly(true);
    m_commandPreview->setStyleSheet("color: #3b82f6; font-family: 'Courier New';");
    mainLayout->addWidget(m_commandPreview);

    // Buttons
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &ComponentLabelPropertiesDialog::applyChanges);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    // Connect field to preview update
    connect(m_textEdit, &QLineEdit::textChanged, this, &ComponentLabelPropertiesDialog::updateCommandPreview);
}

void ComponentLabelPropertiesDialog::loadValues() {
    if (!m_component) return;

    if (m_labelType == Reference) {
        m_textEdit->setText(m_component->reference());
    } else {
        m_textEdit->setText(m_component->value());
    }

    updateCommandPreview();
}

QString ComponentLabelPropertiesDialog::detectModel(const QString& text) const {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return QString();

    // Direct match against library models
    const auto allModels = ModelLibraryManager::instance().allModels();
    for (const auto& info : allModels) {
        if (info.name.compare(trimmed, Qt::CaseInsensitive) == 0) {
            return info.name;
        }
    }
    return QString();
}

void ComponentLabelPropertiesDialog::updateCommandPreview() {
    if (!m_component) return;

    QString ref = m_labelType == Reference ? m_textEdit->text().trimmed() : m_component->reference();
    if (ref.isEmpty()) ref = "?";

    QString model = m_labelType == Value ? m_textEdit->text().trimmed() : m_component->spiceModel();
    if (model.isEmpty()) model = m_component->value();
    if (model.isEmpty()) model = "unnamed";

    m_commandPreview->setText(QString(".model %1 %2").arg(model, ref));
}

void ComponentLabelPropertiesDialog::applyChanges() {
    if (!m_component) return;

    QString newText = m_textEdit->text().trimmed();
    if (!newText.isEmpty()) {
        if (m_labelType == Reference) {
            m_component->setReference(newText);
        } else {
            m_component->setValue(newText);
            // Set model from value text — prefer exact library match, otherwise use as-is
            QString model = detectModel(newText);
            if (model.isEmpty()) model = newText; // Use typed text even if not in library
            m_component->setSpiceModel(model);
        }
    }

    m_component->rebuildPrimitives();
    m_component->update();

    accept();
}
