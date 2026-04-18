// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Janada Sroor

#include "waveform_expression_dialog.h"
#include "theme_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QRegularExpression>
#include <QColorDialog>

WaveformExpressionDialog::WaveformExpressionDialog(const QString &netName, const QStringList &availableSignals, const QColor &existingColor, const QString &expression, QWidget *parent)
    : QDialog(parent)
    , m_availableSignals(availableSignals)
    , m_netName(netName)
{
    setWindowTitle("Waveform Expression");
    setMinimumWidth(480);
    
    // Apply current theme
    if (ThemeManager::theme()) {
        ThemeManager::theme()->applyToWidget(this);
    }
    
    // Connect to theme changes
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        if (ThemeManager::theme()) {
            ThemeManager::theme()->applyToWidget(this);
        }
    });
    
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    PCBTheme* theme = ThemeManager::theme();
    
    m_netLabel = new QLabel(QString("Net: <b>%1</b>").arg(netName));
    if (theme) {
        m_netLabel->setStyleSheet(QString("color: %1; font-size: 12px; padding: 6px 8px; background: %2; border: 1px solid %3; border-radius: 6px;")
            .arg(theme->textColor().name())
            .arg(theme->panelBackground().lighter(105).name())
            .arg(theme->panelBorder().name()));
    }
    mainLayout->addWidget(m_netLabel);
    
    QLabel *hintLabel = new QLabel("Use V/I/P(net) syntax. Functions: derivative(), integral(). Example: V(N001)*2+derivative(V(N002))");
    if (theme) {
        hintLabel->setStyleSheet(QString("color: %1; font-size: 10px;").arg(theme->textSecondary().name()));
    }
    mainLayout->addWidget(hintLabel);
    
    QHBoxLayout *exprLayout = new QHBoxLayout();
    QLabel *exprLabel = new QLabel("Expression:");
    if (theme) {
        exprLabel->setStyleSheet(QString("color: %1;").arg(theme->textColor().name()));
    }
    exprLayout->addWidget(exprLabel);
    
    m_expressionEdit = new QLineEdit();
    m_expressionEdit->setPlaceholderText("e.g., V(N001)+V(N002)*2");
    m_expressionEdit->setText(expression.isEmpty() ? QString("V(%1)").arg(netName) : expression);
    m_expressionEdit->selectAll();
    exprLayout->addWidget(m_expressionEdit);
    
    mainLayout->addLayout(exprLayout);
    
    QHBoxLayout *opLayout = new QHBoxLayout();
    QLabel *opLabel = new QLabel("Insert:");
    if (theme) {
        opLabel->setStyleSheet(QString("color: %1;").arg(theme->textSecondary().name()));
    }
    opLayout->addWidget(opLabel);
    
    QStringList ops = {"+", "-", "*", "/", "V(", ")"};
    for (const QString &op : ops) {
        QPushButton *btn = new QPushButton(op);
        btn->setMinimumWidth(45);
        btn->setMaximumWidth(55);
        btn->setStyleSheet("QPushButton { padding: 4px 8px; }");
        connect(btn, &QPushButton::clicked, this, [this, op]() { insertOperator(op); });
        opLayout->addWidget(btn);
    }
    
    QComboBox *signalCombo = new QComboBox();
    signalCombo->addItems(m_availableSignals);
    signalCombo->setMinimumWidth(150);
    connect(signalCombo, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        if (!text.isEmpty()) {
            m_expressionEdit->insert(text);
            m_expressionEdit->setFocus();
        }
    });
    opLayout->addWidget(signalCombo);
    
    opLayout->addStretch();
    mainLayout->addLayout(opLayout);
    
    QHBoxLayout *colorLayout = new QHBoxLayout();
    QLabel *colorLabel = new QLabel("Color:");
    if (theme) {
        colorLabel->setStyleSheet(QString("color: %1;").arg(theme->textColor().name()));
    }
    colorLayout->addWidget(colorLabel);
    
    QColor defaultColor = existingColor.isValid() ? existingColor : QColor(0, 120, 215);
    m_colorButton = new QPushButton();
    m_colorButton->setProperty("color", defaultColor);
    m_colorButton->setFixedSize(40, 24);
    m_colorButton->setStyleSheet(QString("background-color: %1; border: 1px solid #94a3b8; border-radius: 4px;").arg(defaultColor.name()));
    connect(m_colorButton, &QPushButton::clicked, this, &WaveformExpressionDialog::onColorClicked);
    colorLayout->addWidget(m_colorButton);
    colorLayout->addStretch();
    mainLayout->addLayout(colorLayout);
    
    QHBoxLayout *scaleLayout = new QHBoxLayout();
    QLabel *scaleLabel = new QLabel("Scale:");
    if (theme) {
        scaleLabel->setStyleSheet(QString("color: %1;").arg(theme->textColor().name()));
    }
    scaleLayout->addWidget(scaleLabel);
    
    QMap<QString, QString> scaleOps = {
        {"+2", "+2"}, {"-2", "-2"}, {"*2", "*2"}, {"/2", "/2"}
    };
    for (auto it = scaleOps.begin(); it != scaleOps.end(); ++it) {
        QPushButton *btn = new QPushButton(it.key());
        btn->setMinimumWidth(50);
        btn->setMaximumWidth(65);
        btn->setStyleSheet("QPushButton { padding: 4px 8px; }");
        connect(btn, &QPushButton::clicked, this, [this, op = it.value()]() {
            QString expr = m_expressionEdit->text();
            if (expr.isEmpty()) return;
            if (expr.endsWith(")")) {
                int parenCount = 0;
                int i = expr.length() - 1;
                while (i >= 0) {
                    if (expr[i] == ')') parenCount++;
                    else if (expr[i] == '(') parenCount--;
                    if (parenCount == 0 && i > 0) {
                        expr.insert(i, op);
                        m_expressionEdit->setText(expr);
                        m_expressionEdit->setCursorPosition(i + op.length());
                        return;
                    }
                    i--;
                }
            }
            m_expressionEdit->setText(expr + op);
        });
        scaleLayout->addWidget(btn);
    }
    
    QPushButton *derivBtn = new QPushButton("d/dt");
    derivBtn->setMinimumWidth(55);
    derivBtn->setMaximumWidth(70);
    derivBtn->setStyleSheet("QPushButton { padding: 4px 8px; }");
    connect(derivBtn, &QPushButton::clicked, this, [this]() {
        QString sel = m_expressionEdit->selectedText();
        if (!sel.isEmpty()) {
            m_expressionEdit->insert(QString("derivative(%1)").arg(sel));
        } else {
            QString text = m_expressionEdit->text();
            if (!text.isEmpty()) {
                m_expressionEdit->setText(QString("derivative(%1)").arg(text));
            }
        }
    });
    scaleLayout->addWidget(derivBtn);
    
    QPushButton *integBtn = new QPushButton("\u222b dt");
    integBtn->setMinimumWidth(55);
    integBtn->setMaximumWidth(70);
    integBtn->setStyleSheet("QPushButton { padding: 4px 8px; }");
    connect(integBtn, &QPushButton::clicked, this, [this]() {
        QString sel = m_expressionEdit->selectedText();
        if (!sel.isEmpty()) {
            m_expressionEdit->insert(QString("integral(%1)").arg(sel));
        } else {
            QString text = m_expressionEdit->text();
            if (!text.isEmpty()) {
                m_expressionEdit->setText(QString("integral(%1)").arg(text));
            }
        }
    });
    scaleLayout->addWidget(integBtn);
    
    scaleLayout->addStretch();
    mainLayout->addLayout(scaleLayout);
    
    m_previewLabel = new QLabel("Preview:");
    if (theme) {
        m_previewLabel->setStyleSheet(QString("color: %1; font-size: 10px; font-family: 'Courier New';").arg(theme->textSecondary().name()));
    }
    mainLayout->addWidget(m_previewLabel);
    
    connect(m_expressionEdit, &QLineEdit::textChanged, this, &WaveformExpressionDialog::onExpressionChanged);
    
    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttonBox->button(QDialogButtonBox::Ok)->setText("Plot");
    if (theme) {
        // Style OK button with accent color
        buttonBox->button(QDialogButtonBox::Ok)->setStyleSheet(QString(
            "QPushButton { background: %1; color: #ffffff; border: 1px solid %2; padding: 6px 16px; border-radius: 6px; }"
            "QPushButton:hover { background: %3; }"
        ).arg(theme->accentColor().name())
         .arg(theme->accentColor().darker(110).name())
         .arg(theme->accentHover().name()));
    }
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
    
    onExpressionChanged();
}

void WaveformExpressionDialog::onExpressionChanged() {
    QString expr = m_expressionEdit->text();
    PCBTheme* theme = ThemeManager::theme();
    
    QString exprColor = theme ? theme->accentColor().name() : "#2563eb";
    QString secondaryColor = theme ? theme->textSecondary().name() : "#6b7280";
    
    m_previewLabel->setText(QString("Expression: <span style='color:%1;'>%2</span>").arg(exprColor).arg(expr.isEmpty() ? "(empty)" : expr));
    
    QRegularExpression re("V\\(([^)]+)\\)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = re.globalMatch(expr);
    QStringList found;
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        found.append(match.captured(1));
    }
    if (!found.isEmpty()) {
        m_previewLabel->setText(QString("Expression: <span style='color:%1;'>%2</span> <span style='color:%3;'>(signals: %4)</span>")
            .arg(exprColor).arg(expr).arg(secondaryColor).arg(found.join(", ")));
    }
    
    m_netLabel->setText(QString("Net: <b>%1</b>").arg(expr.isEmpty() ? m_netName : expr));
}

void WaveformExpressionDialog::insertOperator(const QString &op) {
    m_expressionEdit->insert(op);
    m_expressionEdit->setFocus();
}

void WaveformExpressionDialog::onColorClicked() {
    QColor currentColor = m_colorButton->property("color").value<QColor>();
    QColor newColor = QColorDialog::getColor(currentColor, this, "Select Signal Color");
    if (newColor.isValid()) {
        m_colorButton->setProperty("color", newColor);
        m_colorButton->setStyleSheet(QString("background-color: %1; border: 1px solid #94a3b8; border-radius: 4px;").arg(newColor.name()));
    }
}
