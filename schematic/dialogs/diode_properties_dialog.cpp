#include "diode_properties_dialog.h"
#include "diode_model_picker_dialog.h"
#include "../items/schematic_item.h"
#include "../../core/theme_manager.h"
#include "../../simulator/bridge/model_library_manager.h"
#include <QCompleter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QScrollArea>
#include <QRegularExpression>

namespace {
QString diodeTypeForSymbol(const QString& symName) {
    QString s = symName.toLower();
    if (s == "schottky")       return "schottky";
    if (s == "zener")          return "zener";
    if (s == "led")            return "led";
    if (s == "varactor")       return "varactor";
    if (s == "rectifier")      return "rectifier";
    if (s == "tvsdiode")       return "tvs";
    return "silicon";
}

QString symbolNameForDiodeType(const QString& type) {
    QString t = type.toLower();
    if (t == "silicon")   return "diode";
    if (t == "schottky")  return "schottky";
    if (t == "zener")     return "zener";
    if (t == "led")       return "LED";
    if (t == "varactor")  return "varactor";
    if (t == "rectifier") return "rectifier";
    if (t == "tvs")       return "TVSdiode";
    return "diode";
}
}

DiodePropertiesDialog::DiodePropertiesDialog(SchematicItem* item, QWidget* parent)
    : QDialog(parent), m_item(item) {
    m_diodeType = detectDiodeType();
    setWindowTitle("Diode Properties - " + item->reference());
    setModal(true);
    setMinimumWidth(480);

    setupUI();
    loadValues();
    updateCommandPreview();

    if (ThemeManager::theme()) {
        setStyleSheet(ThemeManager::theme()->widgetStylesheet());
    }
}

void DiodePropertiesDialog::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);

    // Scroll area for the form
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* container = new QWidget();
    auto* form = new QFormLayout(container);
    form->setLabelAlignment(Qt::AlignRight);

    // Model name
    m_modelNameEdit = new QLineEdit();
    m_modelNameEdit->setPlaceholderText("e.g. 1N4148, 1N5819");
    form->addRow("Model Name:", m_modelNameEdit);

    // Add autocomplete completer for diode models
    {
        QStringList diodeModels;
        for (const auto& info : ModelLibraryManager::instance().allModels()) {
            if (info.type == "Diode") {
                diodeModels.append(info.name);
            }
        }
        diodeModels.sort(Qt::CaseInsensitive);
        diodeModels.removeDuplicates();
        auto* modelCompleter = new QCompleter(diodeModels, this);
        modelCompleter->setCaseSensitivity(Qt::CaseInsensitive);
        modelCompleter->setFilterMode(Qt::MatchContains);
        modelCompleter->setCompletionMode(QCompleter::PopupCompletion);
        m_modelNameEdit->setCompleter(modelCompleter);
        connect(modelCompleter, QOverload<const QString&>::of(&QCompleter::activated),
                this, &DiodePropertiesDialog::fillFromModel);
    }

    // Pick model buttons
    auto* pickLayout = new QHBoxLayout();
    auto capitalize = [](const QString& s) -> QString {
        if (s.isEmpty()) return s;
        return s.left(1).toUpper() + s.mid(1);
    };

    struct TypeEntry { QString type; QString label; };
    QList<TypeEntry> types = {
        {"silicon",    "Pick Silicon"},
        {"schottky",   "Pick Schottky"},
        {"zener",      "Pick Zener"},
        {"led",        "Pick LED"},
        {"varactor",   "Pick Varactor"},
        {"rectifier",  "Pick Rectifier"},
        {"tvs",        "Pick TVS"},
    };

    // Highlight the current type
    for (const auto& te : types) {
        auto* btn = new QPushButton(te.label);
        btn->setProperty("diodeType", te.type);
        btn->setFixedHeight(26);
        if (te.type == m_diodeType) {
            btn->setStyleSheet("font-weight: bold; background: #2563eb; color: white; border-radius: 3px;");
        }
        connect(btn, &QPushButton::clicked, this, [this, te]() {
            m_diodeType = te.type;
            DiodeModelPickerDialog dlg(m_item, te.type, this);
            if (dlg.exec() == QDialog::Accepted && !dlg.selectedModel().isEmpty()) {
                fillFromModel(dlg.selectedModel());
                // Auto-switch symbol if type differs
                if (te.type != diodeTypeForSymbol(m_item->itemTypeName())) {
                    m_newSymbolName = symbolNameForDiodeType(te.type);
                }
            }
        });
        pickLayout->addWidget(btn);
    }
    form->addRow("", pickLayout);

    // Section separator
    auto addSection = [&](const QString& title) {
        auto* lbl = new QLabel(title);
        lbl->setStyleSheet("font-weight: bold; color: #555; margin-top: 8px;");
        form->addRow(lbl);
    };

    // Forward parameters
    addSection("Forward Parameters");

    m_isEdit = new QLineEdit();
    m_isEdit->setPlaceholderText("e.g. 2.52n, 1e-14");
    form->addRow("Is (Saturation Current):", m_isEdit);

    m_nEdit = new QLineEdit();
    m_nEdit->setPlaceholderText("e.g. 1.752, 1.0");
    form->addRow("N (Emission Coefficient):", m_nEdit);

    m_rsEdit = new QLineEdit();
    m_rsEdit->setPlaceholderText("e.g. 0.568, 0");
    form->addRow("Rs (Series Resistance):", m_rsEdit);

    // Capacitance parameters
    addSection("Capacitance Parameters");

    m_vjEdit = new QLineEdit();
    m_vjEdit->setPlaceholderText("e.g. 0.7");
    form->addRow("Vj (Junction Potential):", m_vjEdit);

    m_cjoEdit = new QLineEdit();
    m_cjoEdit->setPlaceholderText("e.g. 4p, 0");
    form->addRow("Cjo (Zero-bias Capacitance):", m_cjoEdit);

    m_mEdit = new QLineEdit();
    m_mEdit->setPlaceholderText("e.g. 0.4, 0.5");
    form->addRow("M (Grading Coefficient):", m_mEdit);

    // Advanced parameters
    addSection("Advanced Parameters");

    m_ttEdit = new QLineEdit();
    m_ttEdit->setPlaceholderText("e.g. 20n, 0");
    form->addRow("tt (Transit Time):", m_ttEdit);

    m_bvEdit = new QLineEdit();
    m_bvEdit->setPlaceholderText("e.g. 100 (Zener/TVS)");
    form->addRow("BV (Breakdown Voltage):", m_bvEdit);

    m_ibvEdit = new QLineEdit();
    m_ibvEdit->setPlaceholderText("e.g. 100u");
    form->addRow("IBV (Breakdown Current):", m_ibvEdit);

    scroll->setWidget(container);
    mainLayout->addWidget(scroll);

    // SPICE preview
    mainLayout->addWidget(new QLabel("SPICE Preview:"));
    m_commandPreview = new QLineEdit();
    m_commandPreview->setReadOnly(true);
    m_commandPreview->setStyleSheet("color: #3b82f6; font-family: 'Courier New';");
    mainLayout->addWidget(m_commandPreview);

    // Buttons
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &DiodePropertiesDialog::applyChanges);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    // Connect all fields to preview update
    connect(m_modelNameEdit, &QLineEdit::textChanged, this, &DiodePropertiesDialog::updateCommandPreview);
    connect(m_isEdit, &QLineEdit::textChanged, this, &DiodePropertiesDialog::updateCommandPreview);
    connect(m_nEdit, &QLineEdit::textChanged, this, &DiodePropertiesDialog::updateCommandPreview);
    connect(m_rsEdit, &QLineEdit::textChanged, this, &DiodePropertiesDialog::updateCommandPreview);
    connect(m_vjEdit, &QLineEdit::textChanged, this, &DiodePropertiesDialog::updateCommandPreview);
    connect(m_cjoEdit, &QLineEdit::textChanged, this, &DiodePropertiesDialog::updateCommandPreview);
    connect(m_mEdit, &QLineEdit::textChanged, this, &DiodePropertiesDialog::updateCommandPreview);
    connect(m_ttEdit, &QLineEdit::textChanged, this, &DiodePropertiesDialog::updateCommandPreview);
    connect(m_bvEdit, &QLineEdit::textChanged, this, &DiodePropertiesDialog::updateCommandPreview);
    connect(m_ibvEdit, &QLineEdit::textChanged, this, &DiodePropertiesDialog::updateCommandPreview);
}

void DiodePropertiesDialog::loadValues() {
    if (!m_item) return;

    // Load model name from component value
    QString rawValue = m_item->value().trimmed();

    // If value is just the device prefix letter (e.g. "D"), treat as no model
    bool hasModelName = !rawValue.isEmpty() && rawValue.length() > 1;
    if (hasModelName) {
        m_modelNameEdit->setText(rawValue);
    }

    // Type-specific defaults
    struct Defaults { QString Is, N, Rs, Vj, Cjo, M, tt, BV, IBV; };
    static const QMap<QString, Defaults> defaultsMap = {
        {"silicon",   {"2.52n",  "1.752", "0.568", "0.7", "4p",   "0.4",  "20n",  "100",  "100u"}},
        {"schottky",  {"31.7u",  "1.373", "0.065", "0.35","110p",  "0.35", "5n",   "40",   "10u"}},
        {"zener",     {"1e-18",  "1.5",   "5",     "0.7", "100p",  "0.5",  "0",    "6.2",  "5m"}},
        {"led",       {"3.5e-18","3.5",   "60",    "0.7", "30p",   "0.35", "0",    "5",    "10u"}},
        {"varactor",  {"1e-14",  "1.0",   "0.5",   "0.7", "50p",   "0.5",  "0",    "30",   "10u"}},
        {"rectifier", {"14.1n",  "1.984", "0.035", "0.7", "25p",   "0.5",  "5.7u", "1000", "5u"}},
        {"tvs",       {"1e-18",  "1.5",   "1",     "0.7", "500p",  "0.5",  "0",    "24",   "1m"}},
    };

    Defaults d = defaultsMap.value(m_diodeType, defaultsMap["silicon"]);

    // Try to load from ModelLibraryManager if we have a real model name
    if (hasModelName) {
        fillFromModel(rawValue);
    }

    // paramExpressions override everything (from library model or user edits)
    auto pe = m_item->paramExpressions();
    auto loadParam = [&](QLineEdit* edit, const QString& key, const QString& typeDefault) {
        QString val = pe.value(key, typeDefault).trimmed();
        if (val.isEmpty()) val = typeDefault;
        edit->setText(val);
    };

    loadParam(m_isEdit, "diode.Is", d.Is);
    loadParam(m_nEdit, "diode.N", d.N);
    loadParam(m_rsEdit, "diode.Rs", d.Rs);
    loadParam(m_vjEdit, "diode.Vj", d.Vj);
    loadParam(m_cjoEdit, "diode.Cjo", d.Cjo);
    loadParam(m_mEdit, "diode.M", d.M);
    loadParam(m_ttEdit, "diode.tt", d.tt);
    loadParam(m_bvEdit, "diode.BV", d.BV);
    loadParam(m_ibvEdit, "diode.IBV", d.IBV);
}

void DiodePropertiesDialog::fillFromModel(const QString& modelName) {
    const SimModel* mdl = ModelLibraryManager::instance().findModel(modelName);
    if (!mdl) return;

    m_modelNameEdit->setText(modelName);

    auto getParam = [&](const std::string& key, const QString& defaultVal) -> QString {
        auto it = mdl->params.find(key);
        if (it != mdl->params.end()) {
            return QString::number(it->second, 'g', 12);
        }
        return defaultVal;
    };

    m_isEdit->setText(getParam("Is", "1e-14"));
    m_nEdit->setText(getParam("N", "1.0"));
    m_rsEdit->setText(getParam("Rs", "0"));
    m_vjEdit->setText(getParam("Vj", "0.7"));
    m_cjoEdit->setText(getParam("Cjo", "0"));
    m_mEdit->setText(getParam("M", "0.5"));
    m_ttEdit->setText(getParam("tt", "0"));
    m_bvEdit->setText(getParam("BV", ""));
    m_ibvEdit->setText(getParam("IBV", ""));

    updateCommandPreview();
}

QString DiodePropertiesDialog::detectDiodeType() const {
    if (!m_item) return "silicon";
    return diodeTypeForSymbol(m_item->itemTypeName());
}

void DiodePropertiesDialog::updateCommandPreview() {
    QString name = m_modelNameEdit ? m_modelNameEdit->text().trimmed() : "";
    if (name.isEmpty()) name = "unnamed";

    QString line = QString(".model %1 D(").arg(name);

    QStringList params;
    auto addIf = [&](const QString& key, QLineEdit* edit) {
        QString val = edit ? edit->text().trimmed() : "";
        if (!val.isEmpty()) {
            params.append(QString("%1=%2").arg(key, val));
        }
    };

    addIf("Is", m_isEdit);
    addIf("N", m_nEdit);
    addIf("Rs", m_rsEdit);
    addIf("Vj", m_vjEdit);
    addIf("Cjo", m_cjoEdit);
    addIf("M", m_mEdit);
    addIf("tt", m_ttEdit);
    addIf("BV", m_bvEdit);
    addIf("IBV", m_ibvEdit);

    line += params.join(" ") + ")";
    m_commandPreview->setText(line);
}

void DiodePropertiesDialog::applyChanges() {
    accept();
}

QString DiodePropertiesDialog::modelName() const {
    return m_modelNameEdit ? m_modelNameEdit->text().trimmed() : "";
}

QMap<QString, QString> DiodePropertiesDialog::paramExpressions() const {
    QMap<QString, QString> pe;
    auto add = [&](const QString& key, QLineEdit* edit) {
        if (edit) {
            QString val = edit->text().trimmed();
            if (!val.isEmpty()) pe[key] = val;
        }
    };

    add("diode.Is", m_isEdit);
    add("diode.N", m_nEdit);
    add("diode.Rs", m_rsEdit);
    add("diode.Vj", m_vjEdit);
    add("diode.Cjo", m_cjoEdit);
    add("diode.M", m_mEdit);
    add("diode.tt", m_ttEdit);
    add("diode.BV", m_bvEdit);
    add("diode.IBV", m_ibvEdit);

    return pe;
}
