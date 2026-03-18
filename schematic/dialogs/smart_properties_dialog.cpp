#include "smart_properties_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QScrollArea>
#include <QToolTip>
#include <QRegularExpression>

SmartPropertiesDialog::SmartPropertiesDialog(const QList<SchematicItem*>& items, QUndoStack* undoStack, QGraphicsScene* scene, QWidget* parent)
    : QDialog(parent), m_items(items), m_undoStack(undoStack), m_scene(scene) {
    setWindowTitle("Properties");
    setMinimumWidth(400);
    
    // Snapshot original states for live preview reversion
    for (auto* item : m_items) {
        if (item) m_originalStates[item->id()] = item->toJson();
    }

    m_tabWidget = new QTabWidget(this);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(m_tabWidget);
    
    QDialogButtonBox* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply,
        this);
    
    mainLayout->addWidget(buttonBox);
    
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &SmartPropertiesDialog::onApply);
}

void SmartPropertiesDialog::addTab(const PropertyTab& tab) {
    m_tabs.append(tab);
    
    QWidget* page = new QWidget();
    QFormLayout* layout = new QFormLayout(page);
    
    for (const auto& field : tab.fields) {
        createFieldWidget(field, layout);
    }
    
    m_tabWidget->addTab(page, tab.title);
}

void SmartPropertiesDialog::createFieldWidget(const PropertyField& field, QFormLayout* layout) {
    QWidget* widget = nullptr;
    QWidget* rowWidget = nullptr;
    
    switch (field.type) {
        case PropertyField::Text:
        case PropertyField::EngineeringValue:
            widget = new QLineEdit();
            break;
        case PropertyField::MultilineText: {
            auto* edit = new QPlainTextEdit();
            edit->setMinimumHeight(90);
            widget = edit;
            break;
        }
        case PropertyField::Integer: {
            auto* spin = new QSpinBox();
            spin->setRange(-1000000, 1000000);
            widget = spin;
            break;
        }
        case PropertyField::Double: {
            auto* dspin = new QDoubleSpinBox();
            dspin->setRange(-1e12, 1e12);
            dspin->setDecimals(4);
            widget = dspin;
            break;
        }
        case PropertyField::Boolean:
            widget = new QCheckBox();
            break;
        case PropertyField::Choice: {
            auto* combo = new QComboBox();
            combo->addItems(field.choices);
            widget = combo;
            break;
        }
    }
    
    if (widget) {
        widget->setObjectName(field.name);
        widget->setToolTip(field.tooltip);
        
        m_widgets[field.name] = widget;
        
        QLabel* label = new QLabel(field.label + ":");
        if (!field.unit.isEmpty()) {
            QHBoxLayout* h = new QHBoxLayout();
            h->addWidget(widget);
            h->addWidget(new QLabel(field.unit));
            layout->addRow(label, h);
        } else {
            layout->addRow(label, widget);
        }
        
        // Error label (initially hidden)
        QLabel* errLabel = new QLabel();
        errLabel->setStyleSheet("color: #ff4444; font-size: 10px;");
        errLabel->setVisible(false);
        m_errorLabels[field.name] = errLabel;
        layout->addRow("", errLabel);
        
        // Connect change signal
        if (auto* le = qobject_cast<QLineEdit*>(widget))
            connect(le, &QLineEdit::textChanged, this, &SmartPropertiesDialog::onFieldChanged);
        else if (auto* pe = qobject_cast<QPlainTextEdit*>(widget))
            connect(pe, &QPlainTextEdit::textChanged, this, &SmartPropertiesDialog::onFieldChanged);
        else if (auto* cb = qobject_cast<QCheckBox*>(widget))
            connect(cb, &QCheckBox::stateChanged, this, &SmartPropertiesDialog::onFieldChanged);
        else if (auto* cmb = qobject_cast<QComboBox*>(widget))
            connect(cmb, &QComboBox::currentIndexChanged, this, &SmartPropertiesDialog::onFieldChanged);
        else if (auto* sb = qobject_cast<QSpinBox*>(widget))
            connect(sb, qOverload<int>(&QSpinBox::valueChanged), this, &SmartPropertiesDialog::onFieldChanged);
        else if (auto* dsb = qobject_cast<QDoubleSpinBox*>(widget))
            connect(dsb, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &SmartPropertiesDialog::onFieldChanged);
    }
}

QVariant SmartPropertiesDialog::getPropertyValue(const QString& name) const {
    QWidget* w = m_widgets.value(name);
    if (!w) return QVariant();
    
    if (auto* le = qobject_cast<QLineEdit*>(w)) return le->text();
    if (auto* pe = qobject_cast<QPlainTextEdit*>(w)) return pe->toPlainText();
    if (auto* cb = qobject_cast<QCheckBox*>(w)) return cb->isChecked();
    if (auto* cmb = qobject_cast<QComboBox*>(w)) return cmb->currentText();
    if (auto* sb = qobject_cast<QSpinBox*>(w)) return sb->value();
    if (auto* dsb = qobject_cast<QDoubleSpinBox*>(w)) return dsb->value();
    
    return QVariant();
}

void SmartPropertiesDialog::setPropertyValue(const QString& name, const QVariant& value) {
    QWidget* w = m_widgets.value(name);
    if (!w) return;
    
    if (auto* le = qobject_cast<QLineEdit*>(w)) le->setText(value.toString());
    else if (auto* pe = qobject_cast<QPlainTextEdit*>(w)) pe->setPlainText(value.toString());
    else if (auto* cb = qobject_cast<QCheckBox*>(w)) cb->setChecked(value.toBool());
    else if (auto* cmb = qobject_cast<QComboBox*>(w)) cmb->setCurrentText(value.toString());
    else if (auto* sb = qobject_cast<QSpinBox*>(w)) sb->setValue(value.toInt());
    else if (auto* dsb = qobject_cast<QDoubleSpinBox*>(w)) dsb->setValue(value.toDouble());
}

void SmartPropertiesDialog::setTabVisible(int index, bool visible) {
    if (m_tabWidget) {
        m_tabWidget->setTabVisible(index, visible);
    }
}

bool SmartPropertiesDialog::validateAll() {
    bool allValid = true;
    for (const auto& tab : m_tabs) {
        for (const auto& field : tab.fields) {
            QString err;
            QVariant value = getPropertyValue(field.name);
            
            if (field.type == PropertyField::EngineeringValue) {
                QString s = value.toString().trimmed();
                if (!s.isEmpty()) {
                    QRegularExpression re("^([\\-+]?\\d*\\.?\\d+)([kMGTunpfμ]?[ΩFHV]?)$");
                    if (!re.match(s).hasMatch()) {
                        err = "Invalid engineering value (e.g., 10k, 4.7u)";
                    }
                }
            }
            
            if (err.isEmpty() && field.validator) {
                err = field.validator(value);
            }
            
            QLabel* l = m_errorLabels.value(field.name);
            if (l) {
                l->setText(err);
                l->setVisible(!err.isEmpty());
            }
            if (!err.isEmpty()) allValid = false;
        }
    }
    return allValid;
}

void SmartPropertiesDialog::onFieldChanged() {
    if (validateAll()) {
        applyPreview();
    }
}

void SmartPropertiesDialog::applyPreview() {
    // To be implemented by subclasses
}

void SmartPropertiesDialog::revertToOriginal() {
    for (auto* item : m_items) {
        if (item && m_originalStates.contains(item->id())) {
            item->fromJson(m_originalStates[item->id()]);
            item->update();
        }
    }
}

void SmartPropertiesDialog::reject() {
    revertToOriginal();
    QDialog::reject();
}

void SmartPropertiesDialog::onApply() {
    if (validateAll()) {
        // To be implemented by subclasses to push commands to undo stack
    }
}

void SmartPropertiesDialog::accept() {
    onApply();
    QDialog::accept();
}
