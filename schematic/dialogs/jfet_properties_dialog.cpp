#include "jfet_properties_dialog.h"
#include "jfet_model_picker_dialog.h"

#include "../items/schematic_item.h"
#include "../../core/theme_manager.h"
#include "../../simulator/bridge/model_library_manager.h"

#include <QCompleter>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

JfetPropertiesDialog::JfetPropertiesDialog(SchematicItem* item, QWidget* parent)
    : QDialog(parent), m_item(item) {
    setWindowTitle(QString("JFET Properties - %1").arg(item ? item->reference() : "J?"));
    setModal(true);
    setMinimumWidth(460);

    setupUI();
    loadValues();
    updateCommandPreview();

    if (ThemeManager::theme()) {
        setStyleSheet(ThemeManager::theme()->widgetStylesheet());
    }
}

void JfetPropertiesDialog::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);

    m_modelNameEdit = new QLineEdit();
    m_modelNameEdit->setPlaceholderText("e.g. J2N5457 / J2N5460");
    form->addRow("Model Name:", m_modelNameEdit);

    // Add autocomplete completer for JFET models
    {
        QStringList jfetModels;
        for (const auto& info : ModelLibraryManager::instance().allModels()) {
            QString t = info.type.toUpper();
            if (t == "NJF" || t == "PJF") {
                jfetModels.append(info.name);
            }
        }
        jfetModels.sort(Qt::CaseInsensitive);
        jfetModels.removeDuplicates();
        auto* modelCompleter = new QCompleter(jfetModels, this);
        modelCompleter->setCaseSensitivity(Qt::CaseInsensitive);
        modelCompleter->setFilterMode(Qt::MatchContains);
        modelCompleter->setCompletionMode(QCompleter::PopupCompletion);
        m_modelNameEdit->setCompleter(modelCompleter);
        connect(modelCompleter, QOverload<const QString&>::of(&QCompleter::activated),
                this, &JfetPropertiesDialog::fillFromModel);
    }

    auto* pickLayout = new QHBoxLayout();
    auto* pickBtn = new QPushButton(isPChannel() ? "Pick PJF Model" : "Pick NJF Model");
    pickBtn->setFixedHeight(26);
    connect(pickBtn, &QPushButton::clicked, this, [this]() {
        JfetModelPickerDialog dlg(isPChannel(), this);
        if (dlg.exec() == QDialog::Accepted && !dlg.selectedModel().isEmpty()) {
            fillFromModel(dlg.selectedModel());
        }
    });
    pickLayout->addWidget(pickBtn);
    form->addRow("", pickLayout);

    m_betaEdit = new QLineEdit();
    m_betaEdit->setPlaceholderText("e.g. 1m");
    form->addRow("Beta:", m_betaEdit);

    m_vtoEdit = new QLineEdit();
    m_vtoEdit->setPlaceholderText("e.g. -2 (NJF), 2 (PJF)");
    form->addRow("Vto:", m_vtoEdit);

    m_lambdaEdit = new QLineEdit();
    m_lambdaEdit->setPlaceholderText("e.g. 0.02");
    form->addRow("Lambda:", m_lambdaEdit);

    m_rdEdit = new QLineEdit();
    m_rdEdit->setPlaceholderText("e.g. 1");
    form->addRow("Rd:", m_rdEdit);

    m_rsEdit = new QLineEdit();
    m_rsEdit->setPlaceholderText("e.g. 1");
    form->addRow("Rs:", m_rsEdit);

    m_cgsEdit = new QLineEdit();
    m_cgsEdit->setPlaceholderText("e.g. 2p");
    form->addRow("Cgs:", m_cgsEdit);

    m_cgdEdit = new QLineEdit();
    m_cgdEdit->setPlaceholderText("e.g. 1p");
    form->addRow("Cgd:", m_cgdEdit);

    m_isEdit = new QLineEdit();
    m_isEdit->setPlaceholderText("e.g. 1e-14");
    form->addRow("Is:", m_isEdit);

    mainLayout->addLayout(form);

    mainLayout->addWidget(new QLabel("SPICE Preview:"));
    m_commandPreview = new QLineEdit();
    m_commandPreview->setReadOnly(true);
    m_commandPreview->setStyleSheet("color: #3b82f6; font-family: 'Courier New';");
    mainLayout->addWidget(m_commandPreview);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &JfetPropertiesDialog::applyChanges);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    auto connectPreview = [&](QLineEdit* edit) {
        connect(edit, &QLineEdit::textChanged, this, &JfetPropertiesDialog::updateCommandPreview);
    };

    connectPreview(m_modelNameEdit);
    connect(m_modelNameEdit, &QLineEdit::editingFinished, this, &JfetPropertiesDialog::autoMatchModel);
    connectPreview(m_betaEdit);
    connectPreview(m_vtoEdit);
    connectPreview(m_lambdaEdit);
    connectPreview(m_rdEdit);
    connectPreview(m_rsEdit);
    connectPreview(m_cgsEdit);
    connectPreview(m_cgdEdit);
    connectPreview(m_isEdit);
}

bool JfetPropertiesDialog::isPChannel() const {
    if (!m_item) return false;
    const QString t = m_item->itemTypeName().trimmed().toLower();
    return t == "pjf" || m_item->referencePrefix().compare("JP", Qt::CaseInsensitive) == 0;
}

void JfetPropertiesDialog::loadValues() {
    if (!m_item) return;

    const bool pch = isPChannel();
    const QString defaultModel = pch ? "2N5460" : "2N3819";

    QString modelName = m_item->value().trimmed();
    if (modelName.isEmpty()) modelName = defaultModel;
    m_modelNameEdit->setText(modelName);

    const auto pe = m_item->paramExpressions();
    auto loadParam = [&](QLineEdit* edit, const QString& key, const QString& fallback) {
        QString v = pe.value(key).trimmed();
        if (v.isEmpty()) v = fallback;
        edit->setText(v);
    };

    loadParam(m_betaEdit, "jfet.Beta", "1m");
    loadParam(m_vtoEdit, "jfet.Vto", pch ? "2" : "-2");
    loadParam(m_lambdaEdit, "jfet.Lambda", "0.02");
    loadParam(m_rdEdit, "jfet.Rd", "1");
    loadParam(m_rsEdit, "jfet.Rs", "1");
    loadParam(m_cgsEdit, "jfet.Cgs", "2p");
    loadParam(m_cgdEdit, "jfet.Cgd", "1p");
    loadParam(m_isEdit, "jfet.Is", "1e-14");
}

void JfetPropertiesDialog::fillFromModel(const QString& modelName) {
    const SimModel* mdl = ModelLibraryManager::instance().findModel(modelName);
    if (!mdl) return;

    m_modelNameEdit->setText(modelName);

    auto getParam = [&](const std::string& key, const QString& fallback) {
        auto it = mdl->params.find(key);
        if (it != mdl->params.end()) {
            return QString::number(it->second, 'g', 12);
        }
        return fallback;
    };

    m_betaEdit->setText(getParam("Beta", "1m"));
    m_vtoEdit->setText(getParam("Vto", isPChannel() ? "2" : "-2"));
    m_lambdaEdit->setText(getParam("Lambda", "0.02"));
    m_rdEdit->setText(getParam("Rd", "1"));
    m_rsEdit->setText(getParam("Rs", "1"));
    m_cgsEdit->setText(getParam("Cgs", "2p"));
    m_cgdEdit->setText(getParam("Cgd", "1p"));
    m_isEdit->setText(getParam("Is", "1e-14"));

    updateCommandPreview();
}

void JfetPropertiesDialog::autoMatchModel() {
    const QString name = m_modelNameEdit->text().trimmed();
    if (name.isEmpty()) return;

    const SimModel* mdl = ModelLibraryManager::instance().findModel(name);
    if (!mdl) return;

    auto getParam = [&](const std::string& key, const QString& fallback) {
        auto it = mdl->params.find(key);
        if (it != mdl->params.end()) {
            return QString::number(it->second, 'g', 12);
        }
        return fallback;
    };

    auto setIfDefault = [&](QLineEdit* edit, const std::string& key, const QString& defaultVal) {
        const QString current = edit->text().trimmed();
        if (current == defaultVal || current.isEmpty()) {
            edit->setText(getParam(key, defaultVal));
        }
    };

    setIfDefault(m_betaEdit, "Beta", "1m");
    setIfDefault(m_vtoEdit, "Vto", isPChannel() ? "2" : "-2");
    setIfDefault(m_lambdaEdit, "Lambda", "0.02");
    setIfDefault(m_rdEdit, "Rd", "1");
    setIfDefault(m_rsEdit, "Rs", "1");
    setIfDefault(m_cgsEdit, "Cgs", "2p");
    setIfDefault(m_cgdEdit, "Cgd", "1p");
    setIfDefault(m_isEdit, "Is", "1e-14");

    updateCommandPreview();
}

void JfetPropertiesDialog::updateCommandPreview() {
    QString model = modelName();
    if (model.isEmpty()) model = isPChannel() ? "2N5460" : "2N3819";

    QStringList params;
    auto addIf = [&](const QString& key, QLineEdit* edit) {
        const QString v = edit ? edit->text().trimmed() : QString();
        if (!v.isEmpty()) params << QString("%1=%2").arg(key, v);
    };

    addIf("Beta", m_betaEdit);
    addIf("Vto", m_vtoEdit);
    addIf("Lambda", m_lambdaEdit);
    addIf("Rd", m_rdEdit);
    addIf("Rs", m_rsEdit);
    addIf("Cgs", m_cgsEdit);
    addIf("Cgd", m_cgdEdit);
    addIf("Is", m_isEdit);

    const QString jType = isPChannel() ? "PJF" : "NJF";
    m_commandPreview->setText(QString(".model %1 %2(%3)").arg(model, jType, params.join(" ")));
}

void JfetPropertiesDialog::applyChanges() {
    accept();
}

QString JfetPropertiesDialog::modelName() const {
    return m_modelNameEdit ? m_modelNameEdit->text().trimmed() : QString();
}

QMap<QString, QString> JfetPropertiesDialog::paramExpressions() const {
    QMap<QString, QString> pe;
    auto add = [&](const QString& key, QLineEdit* edit) {
        if (!edit) return;
        const QString v = edit->text().trimmed();
        if (!v.isEmpty()) pe[key] = v;
    };

    add("jfet.Beta", m_betaEdit);
    add("jfet.Vto", m_vtoEdit);
    add("jfet.Lambda", m_lambdaEdit);
    add("jfet.Rd", m_rdEdit);
    add("jfet.Rs", m_rsEdit);
    add("jfet.Cgs", m_cgsEdit);
    add("jfet.Cgd", m_cgdEdit);
    add("jfet.Is", m_isEdit);
    return pe;
}
