#include "led_properties_dialog.h"
#include "../items/led_item.h"
#include "../items/blinking_led_item.h"
#include "../../core/theme_manager.h"
#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGraphicsScene>
#include <QVBoxLayout>

LedPropertiesDialog::LedPropertiesDialog(SchematicItem* item, QGraphicsScene* scene, QWidget* parent)
    : QDialog(parent), m_item(item), m_scene(scene) {
    setWindowTitle("LED Properties");
    setModal(true);
    resize(360, 200);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    m_colorCombo = new QComboBox();
    m_colorCombo->addItems({"Red", "Green", "Blue", "Yellow"});
    form->addRow("Color:", m_colorCombo);

    m_blinkCheck = new QCheckBox("Enable Blinking");
    form->addRow("", m_blinkCheck);

    m_blinkHz = new QDoubleSpinBox();
    m_blinkHz->setRange(0.1, 1000.0);
    m_blinkHz->setDecimals(2);
    m_blinkHz->setSuffix(" Hz");
    form->addRow("Blink Frequency:", m_blinkHz);

    m_threshold = new QDoubleSpinBox();
    m_threshold->setRange(0.1, 50.0);
    m_threshold->setDecimals(2);
    m_threshold->setSuffix(" V");
    form->addRow("Power Threshold:", m_threshold);

    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        applyChanges();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // Initialize from item
    if (auto* b = dynamic_cast<BlinkingLEDItem*>(item)) {
        m_blinkCheck->setChecked(b->blinkEnabled());
        m_blinkHz->setValue(b->blinkHz());
        m_threshold->setValue(b->threshold());
        m_colorCombo->setCurrentText(b->colorName().isEmpty() ? "Red" : b->colorName().toLower().replace(0,1, b->colorName().left(1).toUpper()));
    } else if (auto* l = dynamic_cast<LEDItem*>(item)) {
        m_blinkCheck->setChecked(false);
        m_blinkHz->setValue(1.0);
        m_threshold->setValue(l->threshold());
        m_colorCombo->setCurrentText(detectColor(l->value()));
    }

    connect(m_blinkCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_blinkHz->setEnabled(on);
    });
    m_blinkHz->setEnabled(m_blinkCheck->isChecked());

    if (ThemeManager::theme()) {
        setStyleSheet(ThemeManager::theme()->widgetStylesheet());
    }
}

QString LedPropertiesDialog::detectColor(const QString& value) const {
    QString v = value.toUpper();
    if (v.contains("GREEN")) return "Green";
    if (v.contains("BLUE")) return "Blue";
    if (v.contains("YELLOW")) return "Yellow";
    return "Red";
}

void LedPropertiesDialog::applyChanges() {
    if (!m_item) return;

    const QString color = m_colorCombo->currentText().toUpper();
    const bool blinkOn = m_blinkCheck->isChecked();
    const double hz = m_blinkHz->value();
    const double thr = m_threshold->value();

    if (auto* b = dynamic_cast<BlinkingLEDItem*>(m_item.data())) {
        b->setColorName(color);
        b->setBlinkEnabled(blinkOn);
        b->setBlinkHz(hz);
        b->setThreshold(thr);
        b->update();
        return;
    }

    // If normal LED and blinking enabled, convert to blinking LED
    if (blinkOn) {
        auto* old = m_item.data();
        if (!m_scene) return;
        QJsonObject base = old->toJson();
        base["type"] = "Blinking LED";
        auto* neu = new BlinkingLEDItem(old->pos());
        neu->fromJson(base);
        neu->setColorName(color);
        neu->setBlinkEnabled(true);
        neu->setBlinkHz(hz);
        neu->setThreshold(thr);
        neu->setSelected(old->isSelected());
        m_scene->addItem(neu);
        m_scene->removeItem(old);
        delete old;
        return;
    }

    if (auto* l = dynamic_cast<LEDItem*>(m_item.data())) {
        l->setThreshold(thr);
        l->setValue(color);
        l->updateLabelText();
        l->update();
    }
}
