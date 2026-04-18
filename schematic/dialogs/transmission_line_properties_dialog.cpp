#include "transmission_line_properties_dialog.h"

#include "../items/schematic_item.h"
#include "../items/schematic_spice_directive_item.h"
#include "../editor/schematic_commands.h"
#include "theme_manager.h"

#include <QGraphicsScene>
#include <QUndoStack>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QMessageBox>
#include <QVBoxLayout>

TransmissionLinePropertiesDialog::TransmissionLinePropertiesDialog(SchematicItem* item, QGraphicsScene* scene, QUndoStack* undoStack, QWidget* parent)
    : QDialog(parent), m_item(item), m_scene(scene), m_undoStack(undoStack) {
    const QString typeLower = item ? item->itemTypeName().trimmed().toLower() : QString();
    m_isLossy = (typeLower == "ltline") || (item && item->referencePrefix().compare("O", Qt::CaseInsensitive) == 0);

    setWindowTitle(QString("%1 Properties - %2").arg(m_isLossy ? "Lossy Transmission Line" : "Transmission Line", item ? item->reference() : "T1"));
    setModal(true);
    setMinimumWidth(430);

    setupUI();
    loadValues();
    updateCommandPreview();

    if (ThemeManager::theme()) {
        setStyleSheet(ThemeManager::theme()->widgetStylesheet());
    }
}

void TransmissionLinePropertiesDialog::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    if (m_isLossy) {
        m_modelEdit = new QLineEdit();
        m_modelEdit->setPlaceholderText("e.g. LTRAmod");
        form->addRow("Model Name:", m_modelEdit);

        m_rEdit = new QLineEdit();
        m_rEdit->setPlaceholderText("series resistance per unit (e.g. 0.1)");
        form->addRow("R:", m_rEdit);

        m_lEdit = new QLineEdit();
        m_lEdit->setPlaceholderText("series inductance per unit (e.g. 250n)");
        form->addRow("L:", m_lEdit);

        m_gEdit = new QLineEdit();
        m_gEdit->setPlaceholderText("shunt conductance per unit (e.g. 0)");
        form->addRow("G:", m_gEdit);

        m_cEdit = new QLineEdit();
        m_cEdit->setPlaceholderText("shunt capacitance per unit (e.g. 100p)");
        form->addRow("C:", m_cEdit);

        m_lenEdit = new QLineEdit();
        m_lenEdit->setPlaceholderText("line length scale (e.g. 1)");
        form->addRow("LEN:", m_lenEdit);

        connect(m_modelEdit, &QLineEdit::textChanged, this, &TransmissionLinePropertiesDialog::updateCommandPreview);
        connect(m_rEdit, &QLineEdit::textChanged, this, &TransmissionLinePropertiesDialog::updateCommandPreview);
        connect(m_lEdit, &QLineEdit::textChanged, this, &TransmissionLinePropertiesDialog::updateCommandPreview);
        connect(m_gEdit, &QLineEdit::textChanged, this, &TransmissionLinePropertiesDialog::updateCommandPreview);
        connect(m_cEdit, &QLineEdit::textChanged, this, &TransmissionLinePropertiesDialog::updateCommandPreview);
        connect(m_lenEdit, &QLineEdit::textChanged, this, &TransmissionLinePropertiesDialog::updateCommandPreview);
    } else {
        m_z0Edit = new QLineEdit();
        m_z0Edit->setPlaceholderText("e.g. 50");
        form->addRow("Characteristic Impedance Z0:", m_z0Edit);

        m_tdEdit = new QLineEdit();
        m_tdEdit->setPlaceholderText("e.g. 50n");
        form->addRow("Delay Td:", m_tdEdit);

        connect(m_z0Edit, &QLineEdit::textChanged, this, &TransmissionLinePropertiesDialog::updateCommandPreview);
        connect(m_tdEdit, &QLineEdit::textChanged, this, &TransmissionLinePropertiesDialog::updateCommandPreview);
    }

    mainLayout->addLayout(form);

    mainLayout->addWidget(new QLabel("SPICE Preview:"));
    m_commandPreview = new QLineEdit();
    m_commandPreview->setReadOnly(true);
    m_commandPreview->setStyleSheet("color: #3b82f6; font-family: 'Courier New';");
    mainLayout->addWidget(m_commandPreview);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    if (m_isLossy) {
        auto* modelBtn = new QPushButton("Insert/Update .model Directive");
        connect(modelBtn, &QPushButton::clicked, this, &TransmissionLinePropertiesDialog::onInsertOrUpdateModelDirective);
        buttons->addButton(modelBtn, QDialogButtonBox::ActionRole);
    }
    connect(buttons, &QDialogButtonBox::accepted, this, &TransmissionLinePropertiesDialog::applyChanges);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);
}

void TransmissionLinePropertiesDialog::loadValues() {
    const QString v = m_item ? m_item->value().trimmed() : QString();
    if (m_isLossy) {
        QString model = v;
        if (model.isEmpty() || model.compare("LTRA", Qt::CaseInsensitive) == 0) model = "LTRAmod";
        if (m_modelEdit) m_modelEdit->setText(model);
        m_originalModelName = model;

        if (m_item) {
            const auto pe = m_item->paramExpressions();
            const QString r = pe.value("ltra.R");
            const QString l = pe.value("ltra.L");
            const QString g = pe.value("ltra.G");
            const QString c = pe.value("ltra.C");
            const QString len = pe.value("ltra.LEN");
            if (m_rEdit) m_rEdit->setText(r.isEmpty() ? "0" : r);
            if (m_lEdit) m_lEdit->setText(l.isEmpty() ? "0" : l);
            if (m_gEdit) m_gEdit->setText(g.isEmpty() ? "0" : g);
            if (m_cEdit) m_cEdit->setText(c.isEmpty() ? "0" : c);
            if (m_lenEdit) m_lenEdit->setText(len.isEmpty() ? "1" : len);
        }
        return;
    }

    QString z0 = "50";
    QString td = "50n";

    QRegularExpression reZ0("\\bZ0\\s*=\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpression reTd("\\bTd\\s*=\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
    auto mz = reZ0.match(v);
    auto mt = reTd.match(v);
    if (mz.hasMatch()) z0 = mz.captured(1);
    if (mt.hasMatch()) td = mt.captured(1);

    if (m_z0Edit) m_z0Edit->setText(z0);
    if (m_tdEdit) m_tdEdit->setText(td);
}

void TransmissionLinePropertiesDialog::updateCommandPreview() {
    const QString ref = m_item ? m_item->reference() : (m_isLossy ? "O1" : "T1");
    if (m_isLossy) {
        QString model = m_modelEdit ? m_modelEdit->text().trimmed() : QString();
        if (model.isEmpty()) model = "LTRAmod";
        QStringList modelTokens;
        if (m_rEdit && !m_rEdit->text().trimmed().isEmpty()) modelTokens << QString("R=%1").arg(m_rEdit->text().trimmed());
        if (m_lEdit && !m_lEdit->text().trimmed().isEmpty()) modelTokens << QString("L=%1").arg(m_lEdit->text().trimmed());
        if (m_gEdit && !m_gEdit->text().trimmed().isEmpty()) modelTokens << QString("G=%1").arg(m_gEdit->text().trimmed());
        if (m_cEdit && !m_cEdit->text().trimmed().isEmpty()) modelTokens << QString("C=%1").arg(m_cEdit->text().trimmed());
        if (m_lenEdit && !m_lenEdit->text().trimmed().isEmpty()) modelTokens << QString("LEN=%1").arg(m_lenEdit->text().trimmed());

        QString preview = QString("%1 in+ in- out+ out- %2").arg(ref, model);
        if (!modelTokens.isEmpty()) {
            preview += QString("    | .model %1 LTRA(%2)").arg(model, modelTokens.join(" "));
        }
        m_commandPreview->setText(preview);
    } else {
        QString z0 = m_z0Edit ? m_z0Edit->text().trimmed() : QString();
        QString td = m_tdEdit ? m_tdEdit->text().trimmed() : QString();
        if (z0.isEmpty()) z0 = "50";
        if (td.isEmpty()) td = "50n";
        m_commandPreview->setText(QString("%1 in+ in- out+ out- Z0=%2 Td=%3").arg(ref, z0, td));
    }
}

void TransmissionLinePropertiesDialog::applyChanges() {
    accept();
}

void TransmissionLinePropertiesDialog::onInsertOrUpdateModelDirective() {
    if (!m_isLossy || !m_scene) {
        return;
    }

    const QString modelName = valueString().trimmed();
    if (modelName.isEmpty()) {
        QMessageBox::warning(this, "LTRA Model", "Please enter a model name first.");
        return;
    }

    const auto pe = ltraParams();
    QStringList modelTokens;
    const QString r = pe.value("ltra.R").trimmed();
    const QString l = pe.value("ltra.L").trimmed();
    const QString g = pe.value("ltra.G").trimmed();
    const QString c = pe.value("ltra.C").trimmed();
    const QString len = pe.value("ltra.LEN").trimmed();
    if (!r.isEmpty()) modelTokens << QString("R=%1").arg(r);
    if (!l.isEmpty()) modelTokens << QString("L=%1").arg(l);
    if (!g.isEmpty()) modelTokens << QString("G=%1").arg(g);
    if (!c.isEmpty()) modelTokens << QString("C=%1").arg(c);
    if (!len.isEmpty()) modelTokens << QString("LEN=%1").arg(len);

    if (modelTokens.isEmpty()) {
        QMessageBox::warning(this, "LTRA Model", "Please fill at least one LTRA parameter (R/L/G/C/LEN).\nNo directive was inserted.");
        return;
    }

    m_directiveText = QString(".model %1 LTRA(%2)").arg(modelName, modelTokens.join(" "));
    m_wantsDirectiveUpdate = true;
    
    QMessageBox::information(this, "LTRA Model", "The .model directive will be inserted or updated when you click OK.");
}

QString TransmissionLinePropertiesDialog::valueString() const {
    if (m_isLossy) {
        QString model = m_modelEdit ? m_modelEdit->text().trimmed() : QString();
        if (model.isEmpty()) model = "LTRAmod";
        return model;
    }

    QString z0 = m_z0Edit ? m_z0Edit->text().trimmed() : QString();
    QString td = m_tdEdit ? m_tdEdit->text().trimmed() : QString();
    if (z0.isEmpty()) z0 = "50";
    if (td.isEmpty()) td = "50n";
    return QString("Td=%1 Z0=%2").arg(td, z0);
}

QMap<QString, QString> TransmissionLinePropertiesDialog::ltraParams() const {
    QMap<QString, QString> out;
    if (!m_isLossy) return out;

    auto put = [&](const QString& key, QLineEdit* edit) {
        if (!edit) return;
        const QString v = edit->text().trimmed();
        if (!v.isEmpty()) out[key] = v;
    };

    put("ltra.R", m_rEdit);
    put("ltra.L", m_lEdit);
    put("ltra.G", m_gEdit);
    put("ltra.C", m_cEdit);
    put("ltra.LEN", m_lenEdit);
    return out;
}
