#include "text_properties_dialog.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QColorDialog>
#include <QLabel>

TextPropertiesDialog::TextPropertiesDialog(QWidget* parent)
    : QDialog(parent), m_color(Qt::white) {
    setWindowTitle("Text Properties");
    setMinimumWidth(350);
    
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    
    m_textEdit = new QLineEdit();
    m_textEdit->setPlaceholderText("Enter text content...");
    form->addRow("Text:", m_textEdit);
    
    m_fontSizeSpin = new QSpinBox();
    m_fontSizeSpin->setRange(1, 100);
    m_fontSizeSpin->setValue(10);
    form->addRow("Font Size:", m_fontSizeSpin);
    
    m_alignCombo = new QComboBox();
    m_alignCombo->addItems({"Left", "Center", "Right"});
    form->addRow("Alignment:", m_alignCombo);
    
    m_colorBtn = new QPushButton();
    m_colorBtn->setFixedWidth(60);
    m_colorBtn->setStyleSheet(QString("background-color: %1; border: 1px solid #555;").arg(m_color.name()));
    connect(m_colorBtn, &QPushButton::clicked, this, &TextPropertiesDialog::onPickColor);
    form->addRow("Color:", m_colorBtn);
    
    layout->addLayout(form);
    
    auto* buttons = new QHBoxLayout();
    auto* ok = new QPushButton("Place");
    ok->setDefault(true);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    auto* cancel = new QPushButton("Cancel");
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    
    buttons->addStretch();
    buttons->addWidget(ok);
    buttons->addWidget(cancel);
    layout->addLayout(buttons);
    
    setStyleSheet(
        "QDialog { background-color: #2d2d30; color: #cccccc; }"
        "QLabel { color: #cccccc; }"
        "QLineEdit, QSpinBox, QComboBox { background-color: #1e1e1e; border: 1px solid #3c3c3c; padding: 4px; color: #cccccc; }"
        "QPushButton { background-color: #3c3c3c; border: 1px solid #555; color: #cccccc; padding: 6px 12px; }"
        "QPushButton:hover { background-color: #4c4c4c; }"
    );
}

void TextPropertiesDialog::onPickColor() {
    QColor c = QColorDialog::getColor(m_color, this, "Pick Text Color");
    if (c.isValid()) {
        m_color = c;
        m_colorBtn->setStyleSheet(QString("background-color: %1; border: 1px solid #555;").arg(m_color.name()));
    }
}
