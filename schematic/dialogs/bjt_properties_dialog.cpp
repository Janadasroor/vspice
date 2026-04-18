#include "bjt_properties_dialog.h"
#include "bjt_model_picker_dialog.h"
#include "../../pcb/dialogs/footprint_browser_dialog.h"
#include "../items/schematic_item.h"
#include "theme_manager.h"
#include "../../simulator/bridge/model_library_manager.h"

#include <QCompleter>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

BjtPropertiesDialog::BjtPropertiesDialog(SchematicItem* item, QWidget* parent)
    : QDialog(parent), m_item(item) {
    setWindowTitle(QString("BJT Properties - %1").arg(item ? item->reference() : "Q?"));
    setModal(true);
    setMinimumWidth(460);

    setupUI();
    loadValues();
    updateCommandPreview();

    if (ThemeManager::theme()) {
        setStyleSheet(ThemeManager::theme()->widgetStylesheet());
    }
}

void BjtPropertiesDialog::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);

    m_modelNameEdit = new QLineEdit();
    m_modelNameEdit->setPlaceholderText("e.g. 2N2222 / 2N3906");
    form->addRow("Model Name:", m_modelNameEdit);

    // Add autocomplete completer for BJT models
    {
        QStringList bjtModels;
        for (const auto& info : ModelLibraryManager::instance().allModels()) {
            QString t = info.type.toUpper();
            if (t == "NPN" || t == "PNP") {
                bjtModels.append(info.name);
            }
        }
        bjtModels.sort(Qt::CaseInsensitive);
        bjtModels.removeDuplicates();
        auto* modelCompleter = new QCompleter(bjtModels, this);
        modelCompleter->setCaseSensitivity(Qt::CaseInsensitive);
        modelCompleter->setFilterMode(Qt::MatchContains);
        modelCompleter->setCompletionMode(QCompleter::PopupCompletion);
        m_modelNameEdit->setCompleter(modelCompleter);
        connect(modelCompleter, QOverload<const QString&>::of(&QCompleter::activated),
                this, &BjtPropertiesDialog::fillFromModel);
    }

    m_typeCombo = new QComboBox();
    m_typeCombo->addItem("NPN");
    m_typeCombo->addItem("PNP");
    form->addRow("Type:", m_typeCombo);

    auto* pickLayout = new QHBoxLayout();
    m_pickModelButton = new QPushButton(isPnp() ? "Pick PNP Model" : "Pick NPN Model");
    m_pickModelButton->setFixedHeight(26);
    connect(m_pickModelButton, &QPushButton::clicked, this, [this]() {
        BjtModelPickerDialog dlg(isPnpSelected(), this);
        if (dlg.exec() == QDialog::Accepted && !dlg.selectedModel().isEmpty()) {
            fillFromModel(dlg.selectedModel());
        }
    });
    pickLayout->addWidget(m_pickModelButton);
    form->addRow("", pickLayout);

    m_isEdit = new QLineEdit();
    m_isEdit->setPlaceholderText("e.g. 1e-14");
    form->addRow("Is:", m_isEdit);

    m_bfEdit = new QLineEdit();
    m_bfEdit->setPlaceholderText("e.g. 100");
    form->addRow("Bf:", m_bfEdit);

    m_vafEdit = new QLineEdit();
    m_vafEdit->setPlaceholderText("e.g. 100");
    form->addRow("Vaf:", m_vafEdit);

    m_cjeEdit = new QLineEdit();
    m_cjeEdit->setPlaceholderText("e.g. 8p");
    form->addRow("Cje:", m_cjeEdit);

    m_cjcEdit = new QLineEdit();
    m_cjcEdit->setPlaceholderText("e.g. 3p");
    form->addRow("Cjc:", m_cjcEdit);

    m_tfEdit = new QLineEdit();
    m_tfEdit->setPlaceholderText("e.g. 400p");
    form->addRow("Tf:", m_tfEdit);

    m_trEdit = new QLineEdit();
    m_trEdit->setPlaceholderText("e.g. 50n");
    form->addRow("Tr:", m_trEdit);

    auto* fpRow = new QHBoxLayout();
    m_footprintEdit = new QLineEdit();
    m_footprintEdit->setPlaceholderText("Select a footprint");
    m_footprintEdit->setReadOnly(true);
    auto* fpBtn = new QPushButton("Pick Footprint");
    connect(fpBtn, &QPushButton::clicked, this, &BjtPropertiesDialog::pickFootprint);
    fpRow->addWidget(m_footprintEdit, 1);
    fpRow->addWidget(fpBtn);
    form->addRow("Footprint:", fpRow);

    mainLayout->addLayout(form);

    mainLayout->addWidget(new QLabel("SPICE Preview:"));
    m_commandPreview = new QLineEdit();
    m_commandPreview->setReadOnly(true);
    m_commandPreview->setStyleSheet("color: #3b82f6; font-family: 'Courier New';");
    mainLayout->addWidget(m_commandPreview);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &BjtPropertiesDialog::applyChanges);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    auto connectPreview = [&](QLineEdit* edit) {
        connect(edit, &QLineEdit::textChanged, this, &BjtPropertiesDialog::updateCommandPreview);
    };

    connectPreview(m_modelNameEdit);
    connect(m_modelNameEdit, &QLineEdit::editingFinished, this, &BjtPropertiesDialog::autoMatchModel);
    connect(m_typeCombo, &QComboBox::currentTextChanged, this, [this]() {
        if (m_pickModelButton) {
            m_pickModelButton->setText(isPnpSelected() ? "Pick PNP Model" : "Pick NPN Model");
        }
        updateCommandPreview();
    });
    connectPreview(m_isEdit);
    connectPreview(m_bfEdit);
    connectPreview(m_vafEdit);
    connectPreview(m_cjeEdit);
    connectPreview(m_cjcEdit);
    connectPreview(m_tfEdit);
    connectPreview(m_trEdit);
}

bool BjtPropertiesDialog::isPnp() const {
    if (!m_item) return false;
    const QString t = m_item->itemTypeName().trimmed().toLower();
    return t == "transistor_pnp" ||
           t == "pnp" ||
           t == "pnp2" ||
           t == "pnp4" ||
           t == "lpnp" ||
           m_item->referencePrefix().compare("QP", Qt::CaseInsensitive) == 0;
}

bool BjtPropertiesDialog::isPnpSelected() const {
    if (m_typeCombo) {
        return m_typeCombo->currentText().compare("PNP", Qt::CaseInsensitive) == 0;
    }
    return isPnp();
}

void BjtPropertiesDialog::loadValues() {
    if (!m_item) return;

    const auto pe = m_item->paramExpressions();
    const QString typeExpr = pe.value("bjt.type").trimmed();
    const bool pnp = typeExpr.isEmpty() ? isPnp() : (typeExpr.compare("PNP", Qt::CaseInsensitive) == 0);
    const QString defaultModel = pnp ? "2N3906" : "2N2222";

    if (m_typeCombo) {
        m_typeCombo->setCurrentText(pnp ? "PNP" : "NPN");
    }

    QString modelName = m_item->spiceModel().trimmed();
    if (modelName.isEmpty()) {
        modelName = m_item->value().trimmed();
        if (modelName.isEmpty() || modelName.compare("NPN", Qt::CaseInsensitive) == 0 || modelName.compare("PNP", Qt::CaseInsensitive) == 0) {
            modelName = defaultModel;
        }
    }
    m_modelNameEdit->setText(modelName);

    const SimModel* mdl = ModelLibraryManager::instance().findModel(modelName);
    if (mdl && m_typeCombo) {
        if (mdl->type == SimComponentType::BJT_PNP) m_typeCombo->setCurrentText("PNP");
        else if (mdl->type == SimComponentType::BJT_NPN) m_typeCombo->setCurrentText("NPN");
    }

    auto modelParam = [&](const QString& key, const QString& fallback) {
        if (!mdl) return fallback;
        for (const auto& kv : mdl->params) {
            if (QString::fromStdString(kv.first).compare(key, Qt::CaseInsensitive) == 0) {
                return QString::number(kv.second, 'g', 12);
            }
        }
        return fallback;
    };

    auto loadParam = [&](QLineEdit* edit, const QString& key, const QString& fallback) {
        QString v = pe.value(key).trimmed();
        if (v.isEmpty()) v = modelParam(key.section('.', 1), fallback);
        edit->setText(v);
    };

    loadParam(m_isEdit, "bjt.Is", "1e-14");
    loadParam(m_bfEdit, "bjt.Bf", "100");
    loadParam(m_vafEdit, "bjt.Vaf", "100");
    loadParam(m_cjeEdit, "bjt.Cje", "8p");
    loadParam(m_cjcEdit, "bjt.Cjc", "3p");
    loadParam(m_tfEdit, "bjt.Tf", "400p");
    loadParam(m_trEdit, "bjt.Tr", "50n");

    if (m_footprintEdit) {
        m_footprintEdit->setText(m_item->footprint());
    }
}

void BjtPropertiesDialog::fillFromModel(const QString& modelName) {
    const SimModel* mdl = ModelLibraryManager::instance().findModel(modelName);
    if (!mdl) return;

    m_modelNameEdit->setText(modelName);

    if (m_typeCombo) {
        if (mdl->type == SimComponentType::BJT_PNP) m_typeCombo->setCurrentText("PNP");
        else if (mdl->type == SimComponentType::BJT_NPN) m_typeCombo->setCurrentText("NPN");
    }

    auto getParam = [&](const QString& key, const QString& fallback) {
        for (const auto& kv : mdl->params) {
            if (QString::fromStdString(kv.first).compare(key, Qt::CaseInsensitive) == 0) {
                return QString::number(kv.second, 'g', 12);
            }
        }
        return fallback;
    };

    m_isEdit->setText(getParam("Is", "1e-14"));
    m_bfEdit->setText(getParam("Bf", "100"));
    m_vafEdit->setText(getParam("Vaf", "100"));
    m_cjeEdit->setText(getParam("Cje", "8p"));
    m_cjcEdit->setText(getParam("Cjc", "3p"));
    m_tfEdit->setText(getParam("Tf", "400p"));
    m_trEdit->setText(getParam("Tr", "50n"));

    updateCommandPreview();
}

void BjtPropertiesDialog::updateCommandPreview() {
    QString model = modelName();
    if (model.isEmpty()) model = isPnpSelected() ? "2N3906" : "2N2222";

    QStringList params;
    auto addIf = [&](const QString& key, QLineEdit* edit) {
        const QString v = edit ? edit->text().trimmed() : QString();
        if (!v.isEmpty()) params << QString("%1=%2").arg(key, v);
    };

    addIf("Is", m_isEdit);
    addIf("Bf", m_bfEdit);
    addIf("Vaf", m_vafEdit);
    addIf("Cje", m_cjeEdit);
    addIf("Cjc", m_cjcEdit);
    addIf("Tf", m_tfEdit);
    addIf("Tr", m_trEdit);

    const QString bjtType = isPnpSelected() ? "PNP" : "NPN";
    m_commandPreview->setText(QString(".model %1 %2(%3)").arg(model, bjtType, params.join(" ")));
}

void BjtPropertiesDialog::autoMatchModel() {
    const QString name = m_modelNameEdit->text().trimmed();
    if (name.isEmpty()) return;

    const SimModel* mdl = ModelLibraryManager::instance().findModel(name);
    if (!mdl) return;

    bool typeMatch = false;
    if (mdl->type == SimComponentType::BJT_NPN && m_typeCombo->currentText() == "NPN") typeMatch = true;
    if (mdl->type == SimComponentType::BJT_PNP && m_typeCombo->currentText() == "PNP") typeMatch = true;
    if (!typeMatch) return;

    auto getParam = [&](const QString& key, const QString& fallback) {
        for (const auto& kv : mdl->params) {
            if (QString::fromStdString(kv.first).compare(key, Qt::CaseInsensitive) == 0) {
                return QString::number(kv.second, 'g', 12);
            }
        }
        return fallback;
    };

    auto setIfDefault = [&](QLineEdit* edit, const QString& key, const QString& defaultVal) {
        const QString current = edit->text().trimmed();
        if (current == defaultVal || current.isEmpty()) {
            edit->setText(getParam(key, defaultVal));
        }
    };

    setIfDefault(m_isEdit, "Is", "1e-14");
    setIfDefault(m_bfEdit, "Bf", "100");
    setIfDefault(m_vafEdit, "Vaf", "100");
    setIfDefault(m_cjeEdit, "Cje", "8p");
    setIfDefault(m_cjcEdit, "Cjc", "3p");
    setIfDefault(m_tfEdit, "Tf", "400p");
    setIfDefault(m_trEdit, "Tr", "100n");

    updateCommandPreview();
}

void BjtPropertiesDialog::applyChanges() {
    accept();
}

QString BjtPropertiesDialog::modelName() const {
    return m_modelNameEdit ? m_modelNameEdit->text().trimmed() : QString();
}

QMap<QString, QString> BjtPropertiesDialog::paramExpressions() const {
    QMap<QString, QString> pe;
    auto add = [&](const QString& key, QLineEdit* edit) {
        if (!edit) return;
        const QString v = edit->text().trimmed();
        if (!v.isEmpty()) pe[key] = v;
    };

    add("bjt.Is", m_isEdit);
    add("bjt.Bf", m_bfEdit);
    add("bjt.Vaf", m_vafEdit);
    add("bjt.Cje", m_cjeEdit);
    add("bjt.Cjc", m_cjcEdit);
    add("bjt.Tf", m_tfEdit);
    add("bjt.Tr", m_trEdit);
    pe["bjt.type"] = isPnpSelected() ? "PNP" : "NPN";
    return pe;
}

QString BjtPropertiesDialog::newSymbolName() const {
    return isPnpSelected() ? "pnp" : "npn";
}

QString BjtPropertiesDialog::footprint() const {
    return m_footprintEdit ? m_footprintEdit->text().trimmed() : QString();
}

void BjtPropertiesDialog::pickFootprint() {
    FootprintBrowserDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        FootprintDefinition fp = dlg.selectedFootprint();
        if (!fp.name().isEmpty()) {
            m_footprintEdit->setText(fp.name());
        }
    }
}
