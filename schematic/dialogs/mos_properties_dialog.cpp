#include "mos_properties_dialog.h"
#include "mos_model_picker_dialog.h"
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

MosPropertiesDialog::MosPropertiesDialog(SchematicItem* item, QWidget* parent)
    : QDialog(parent), m_item(item) {
    setWindowTitle(QString("MOSFET Properties - %1").arg(item ? item->reference() : "M?"));
    setModal(true);
    setMinimumWidth(460);

    setupUI();
    loadValues();
    updateCommandPreview();

    if (ThemeManager::theme()) {
        setStyleSheet(ThemeManager::theme()->widgetStylesheet());
    }
}

void MosPropertiesDialog::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);

    m_modelNameEdit = new QLineEdit();
    m_modelNameEdit->setPlaceholderText("e.g. 2N7000 / BS250");
    form->addRow("Model Name:", m_modelNameEdit);

    // Add autocomplete completer for MOS models
    {
        QStringList mosModels;
        for (const auto& info : ModelLibraryManager::instance().allModels()) {
            QString t = info.type.toUpper();
            if (t == "NMOS" || t == "PMOS" || t == "VDMOS" || t == "NMF" || t == "PMF") {
                mosModels.append(info.name);
            }
        }
        mosModels.sort(Qt::CaseInsensitive);
        mosModels.removeDuplicates();
        auto* modelCompleter = new QCompleter(mosModels, this);
        modelCompleter->setCaseSensitivity(Qt::CaseInsensitive);
        modelCompleter->setFilterMode(Qt::MatchContains);
        modelCompleter->setCompletionMode(QCompleter::PopupCompletion);
        m_modelNameEdit->setCompleter(modelCompleter);
        connect(modelCompleter, QOverload<const QString&>::of(&QCompleter::activated),
                this, &MosPropertiesDialog::fillFromModel);
    }

    m_typeCombo = new QComboBox();
    m_typeCombo->addItem("NMOS");
    m_typeCombo->addItem("PMOS");
    form->addRow("Type:", m_typeCombo);

    auto* pickLayout = new QHBoxLayout();
    m_pickModelButton = new QPushButton(isPmos() ? "Pick PMOS Model" : "Pick NMOS Model");
    m_pickModelButton->setFixedHeight(26);
    connect(m_pickModelButton, &QPushButton::clicked, this, [this]() {
        MosModelPickerDialog dlg(isPmosSelected(), this);
        if (dlg.exec() == QDialog::Accepted && !dlg.selectedModel().isEmpty()) {
            fillFromModel(dlg.selectedModel());
        }
    });
    pickLayout->addWidget(m_pickModelButton);
    form->addRow("", pickLayout);

    m_vtoEdit = new QLineEdit();
    m_vtoEdit->setPlaceholderText("e.g. 2 (NMOS), -2 (PMOS)");
    form->addRow("Vto:", m_vtoEdit);

    m_kpEdit = new QLineEdit();
    m_kpEdit->setPlaceholderText("e.g. 100u");
    form->addRow("Kp:", m_kpEdit);

    m_lambdaEdit = new QLineEdit();
    m_lambdaEdit->setPlaceholderText("e.g. 0.02");
    form->addRow("Lambda:", m_lambdaEdit);

    m_rdEdit = new QLineEdit();
    m_rdEdit->setPlaceholderText("e.g. 1");
    form->addRow("Rd:", m_rdEdit);

    m_rsEdit = new QLineEdit();
    m_rsEdit->setPlaceholderText("e.g. 1");
    form->addRow("Rs:", m_rsEdit);

    m_cgsoEdit = new QLineEdit();
    m_cgsoEdit->setPlaceholderText("e.g. 50p");
    form->addRow("Cgso:", m_cgsoEdit);

    m_cgdoEdit = new QLineEdit();
    m_cgdoEdit->setPlaceholderText("e.g. 50p");
    form->addRow("Cgdo:", m_cgdoEdit);

    auto* fpRow = new QHBoxLayout();
    m_footprintEdit = new QLineEdit();
    m_footprintEdit->setPlaceholderText("Select a footprint");
    m_footprintEdit->setReadOnly(true);
    auto* fpBtn = new QPushButton("Pick Footprint");
    connect(fpBtn, &QPushButton::clicked, this, &MosPropertiesDialog::pickFootprint);
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
    connect(buttons, &QDialogButtonBox::accepted, this, &MosPropertiesDialog::applyChanges);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    auto connectPreview = [&](QLineEdit* edit) {
        connect(edit, &QLineEdit::textChanged, this, &MosPropertiesDialog::updateCommandPreview);
    };

    connectPreview(m_modelNameEdit);
    connect(m_modelNameEdit, &QLineEdit::editingFinished, this, &MosPropertiesDialog::autoMatchModel);
    connect(m_typeCombo, &QComboBox::currentTextChanged, this, [this]() {
        if (m_pickModelButton) {
            m_pickModelButton->setText(isPmosSelected() ? "Pick PMOS Model" : "Pick NMOS Model");
        }
        updateCommandPreview();
    });
    connectPreview(m_vtoEdit);
    connectPreview(m_kpEdit);
    connectPreview(m_lambdaEdit);
    connectPreview(m_rdEdit);
    connectPreview(m_rsEdit);
    connectPreview(m_cgsoEdit);
    connectPreview(m_cgdoEdit);
}

bool MosPropertiesDialog::isPmos() const {
    if (!m_item) return false;
    const QString t = m_item->itemTypeName().trimmed().toLower();
    return t == "transistor_pmos" ||
           t == "pmos" ||
           t == "pmos4" ||
           m_item->referencePrefix().compare("MP", Qt::CaseInsensitive) == 0;
}

bool MosPropertiesDialog::isPmosSelected() const {
    if (m_typeCombo) {
        return m_typeCombo->currentText().compare("PMOS", Qt::CaseInsensitive) == 0;
    }
    return isPmos();
}

void MosPropertiesDialog::loadValues() {
    if (!m_item) return;

    qDebug() << "[MosPropertiesDialog::loadValues] item:" << m_item->reference()
             << "spiceModel:" << m_item->spiceModel()
             << "value:" << m_item->value()
             << "itemType:" << m_item->itemTypeName();

    const auto pe = m_item->paramExpressions();
    const QString typeExpr = pe.value("mos.type").trimmed();
    const bool pmos = typeExpr.isEmpty() ? isPmos() : (typeExpr.compare("PMOS", Qt::CaseInsensitive) == 0);
    const QString defaultModel = pmos ? "BS250" : "2N7000";

    if (m_typeCombo) {
        m_typeCombo->setCurrentText(pmos ? "PMOS" : "NMOS");
    }

    QString modelName = m_item->spiceModel().trimmed();
    if (modelName.isEmpty()) {
        modelName = m_item->value().trimmed();
        if (modelName.isEmpty() || modelName.compare("NMOS", Qt::CaseInsensitive) == 0 || modelName.compare("PMOS", Qt::CaseInsensitive) == 0) {
            modelName = defaultModel;
        }
    }
    m_modelNameEdit->setText(modelName);

    const SimModel* mdl = ModelLibraryManager::instance().findModel(modelName);
    if (mdl && m_typeCombo) {
        if (mdl->type == SimComponentType::MOSFET_PMOS) m_typeCombo->setCurrentText("PMOS");
        else if (mdl->type == SimComponentType::MOSFET_NMOS) m_typeCombo->setCurrentText("NMOS");
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

    loadParam(m_vtoEdit, "mos.Vto", isPmosSelected() ? "-2" : "2");
    loadParam(m_kpEdit, "mos.Kp", "100u");
    loadParam(m_lambdaEdit, "mos.Lambda", "0.02");
    loadParam(m_rdEdit, "mos.Rd", "1");
    loadParam(m_rsEdit, "mos.Rs", "1");
    loadParam(m_cgsoEdit, "mos.Cgso", "50p");
    loadParam(m_cgdoEdit, "mos.Cgdo", "50p");

    if (m_footprintEdit) {
        m_footprintEdit->setText(m_item->footprint());
    }
}

void MosPropertiesDialog::fillFromModel(const QString& modelName) {
    const SimModel* mdl = ModelLibraryManager::instance().findModel(modelName);
    if (!mdl) return;

    m_modelNameEdit->setText(modelName);
    if (m_typeCombo) {
        if (mdl->type == SimComponentType::MOSFET_PMOS) m_typeCombo->setCurrentText("PMOS");
        else if (mdl->type == SimComponentType::MOSFET_NMOS) m_typeCombo->setCurrentText("NMOS");
    }

    auto getParam = [&](const QString& key, const QString& fallback) {
        for (const auto& kv : mdl->params) {
            if (QString::fromStdString(kv.first).compare(key, Qt::CaseInsensitive) == 0) {
                return QString::number(kv.second, 'g', 12);
            }
        }
        return fallback;
    };

    m_vtoEdit->setText(getParam("Vto", isPmosSelected() ? "-2" : "2"));
    m_kpEdit->setText(getParam("Kp", "100u"));
    m_lambdaEdit->setText(getParam("Lambda", "0.02"));
    m_rdEdit->setText(getParam("Rd", "1"));
    m_rsEdit->setText(getParam("Rs", "1"));
    m_cgsoEdit->setText(getParam("Cgso", "50p"));
    m_cgdoEdit->setText(getParam("Cgdo", "50p"));

    updateCommandPreview();
}

void MosPropertiesDialog::autoMatchModel() {
    const QString name = m_modelNameEdit->text().trimmed();
    if (name.isEmpty()) return;

    const SimModel* mdl = ModelLibraryManager::instance().findModel(name);
    if (!mdl) return;

    // Only auto-fill if the model matches the selected type
    bool typeMatch = false;
    if (mdl->type == SimComponentType::MOSFET_NMOS && m_typeCombo->currentText() == "NMOS") typeMatch = true;
    if (mdl->type == SimComponentType::MOSFET_PMOS && m_typeCombo->currentText() == "PMOS") typeMatch = true;
    if (!typeMatch) return;

    auto getParam = [&](const QString& key, const QString& fallback) {
        for (const auto& kv : mdl->params) {
            if (QString::fromStdString(kv.first).compare(key, Qt::CaseInsensitive) == 0) {
                return QString::number(kv.second, 'g', 12);
            }
        }
        return fallback;
    };

    // Only overwrite fields that are still at default values
    auto setIfDefault = [&](QLineEdit* edit, const QString& key, const QString& defaultVal) {
        const QString current = edit->text().trimmed();
        if (current == defaultVal || current.isEmpty()) {
            edit->setText(getParam(key, defaultVal));
        }
    };

    const QString defVto = isPmosSelected() ? "-2" : "2";
    setIfDefault(m_vtoEdit, "Vto", defVto);
    setIfDefault(m_kpEdit, "Kp", "100u");
    setIfDefault(m_lambdaEdit, "Lambda", "0.02");
    setIfDefault(m_rdEdit, "Rd", "1");
    setIfDefault(m_rsEdit, "Rs", "1");
    setIfDefault(m_cgsoEdit, "Cgso", "50p");
    setIfDefault(m_cgdoEdit, "Cgdo", "50p");

    updateCommandPreview();
}

void MosPropertiesDialog::updateCommandPreview() {
    QString model = modelName();
    if (model.isEmpty()) model = isPmosSelected() ? "BS250" : "2N7000";

    QStringList params;
    auto addIf = [&](const QString& key, QLineEdit* edit) {
        const QString v = edit ? edit->text().trimmed() : QString();
        if (!v.isEmpty()) params << QString("%1=%2").arg(key, v);
    };

    addIf("VTO", m_vtoEdit);
    addIf("KP", m_kpEdit);
    addIf("LAMBDA", m_lambdaEdit);
    addIf("RD", m_rdEdit);
    addIf("RS", m_rsEdit);
    addIf("CGSO", m_cgsoEdit);
    addIf("CGDO", m_cgdoEdit);

    const QString mosType = isPmosSelected() ? "PMOS" : "NMOS";
    m_commandPreview->setText(QString(".model %1 %2(%3)").arg(model, mosType, params.join(" ")));
}

void MosPropertiesDialog::applyChanges() {
    accept();
}

QString MosPropertiesDialog::modelName() const {
    return m_modelNameEdit ? m_modelNameEdit->text().trimmed() : QString();
}

QMap<QString, QString> MosPropertiesDialog::paramExpressions() const {
    QMap<QString, QString> pe;
    auto add = [&](const QString& key, QLineEdit* edit) {
        if (!edit) return;
        const QString v = edit->text().trimmed();
        if (!v.isEmpty()) pe[key] = v;
    };

    add("mos.Vto", m_vtoEdit);
    add("mos.Kp", m_kpEdit);
    add("mos.Lambda", m_lambdaEdit);
    add("mos.Rd", m_rdEdit);
    add("mos.Rs", m_rsEdit);
    add("mos.Cgso", m_cgsoEdit);
    add("mos.Cgdo", m_cgdoEdit);
    pe["mos.type"] = isPmosSelected() ? "PMOS" : "NMOS";
    return pe;
}

QString MosPropertiesDialog::newSymbolName() const {
    return isPmosSelected() ? "pmos" : "nmos";
}

QString MosPropertiesDialog::footprint() const {
    return m_footprintEdit ? m_footprintEdit->text().trimmed() : QString();
}

void MosPropertiesDialog::pickFootprint() {
    FootprintBrowserDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        FootprintDefinition fp = dlg.selectedFootprint();
        if (!fp.name().isEmpty()) {
            m_footprintEdit->setText(fp.name());
        }
    }
}
