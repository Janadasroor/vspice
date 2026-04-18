#include "behavioral_current_source_dialog.h"
#include "../items/behavioral_current_source_item.h"
#include "theme_manager.h"
#include "../analysis/net_manager.h"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QCompleter>
#include <QStringListModel>
#include <QInputDialog>
#include <QJSEngine>
#include <QKeyEvent>
#include <QAbstractItemView>

namespace {
QString normalizeExpr(QString expr) {
    expr = expr.trimmed();
    if (expr.isEmpty()) return "I=0";
    if (!expr.startsWith("I=", Qt::CaseInsensitive)) expr.prepend("I=");
    return expr;
}

QString validateExpr(const QString& expr) {
    const QString v = expr.trimmed();
    if (v.isEmpty()) return "Expression is empty. Using I=0.";
    int depth = 0;
    for (const QChar& c : v) {
        if (c == '(') depth++;
        else if (c == ')') depth--;
        if (depth < 0) break;
    }
    if (depth != 0) return "Unbalanced parentheses.";
    if (!v.startsWith("I=", Qt::CaseInsensitive)) return "Missing 'I=' prefix. It will be added.";
    return QString();
}
}

BehavioralCurrentSourceDialog::BehavioralCurrentSourceDialog(BehavioralCurrentSourceItem* item, QGraphicsScene* scene, QWidget* parent)
    : QDialog(parent), m_item(item), m_scene(scene) {
    setWindowTitle("Behavioral Current Source (BI)");
    setModal(true);
    resize(420, 260);

    auto* layout = new QVBoxLayout(this);

    layout->addWidget(new QLabel("Expression (use I=...):"));
    m_expr = new QPlainTextEdit();
    m_expr->setPlaceholderText("I=V(in)/1k");
    m_expr->setMinimumHeight(90);
    layout->addWidget(m_expr);

    auto* presetRow = new QHBoxLayout();
    auto* preset1 = new QPushButton("I=V(in)/1k");
    auto* preset2 = new QPushButton("I=if(V(in)>0,1m,0)");
    auto* preset3 = new QPushButton("I=table(time, 0 0 1m 1m)");
    presetRow->addWidget(preset1);
    presetRow->addWidget(preset2);
    presetRow->addWidget(preset3);
    layout->addLayout(presetRow);

    auto* helperRow = new QHBoxLayout();
    m_insertNodeBtn = new QPushButton("Insert Node...");
    helperRow->addWidget(m_insertNodeBtn);
    helperRow->addStretch();
    layout->addLayout(helperRow);

    auto* previewRow = new QHBoxLayout();
    previewRow->addWidget(new QLabel("Preview V:"));
    m_previewValue = new QDoubleSpinBox();
    m_previewValue->setRange(-1e6, 1e6);
    m_previewValue->setDecimals(4);
    m_previewValue->setValue(1.0);
    previewRow->addWidget(m_previewValue);
    previewRow->addWidget(new QLabel("time:"));
    m_previewTime = new QDoubleSpinBox();
    m_previewTime->setRange(-1e6, 1e6);
    m_previewTime->setDecimals(6);
    m_previewTime->setValue(0.0);
    previewRow->addWidget(m_previewTime);
    layout->addLayout(previewRow);

    m_status = new QLabel();
    m_status->setStyleSheet("color: #cc0000; font-size: 11px;");
    layout->addWidget(m_status);

    m_preview = new QLabel();
    m_preview->setStyleSheet("color: #333333; font-size: 11px;");
    layout->addWidget(m_preview);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        applyChanges();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    if (m_item) m_expr->setPlainText(m_item->value());

    m_completer = new QCompleter(this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setWidget(m_expr);
    m_expr->installEventFilter(this);
    auto updateCompleter = [this]() {
        QStringList words = {
            "V(", "I(", "time", "if(", "table(", "abs(", "sin(", "cos(", "tan(", "sqrt(", "exp(", "log(",
            "min(", "max(", "pow(", "limit("
        };
        if (m_scene) {
            NetManager nm;
            nm.updateNets(m_scene);
            auto nets = nm.netNames();
            for (const QString& n : nets) {
                if (!n.trimmed().isEmpty()) words << QString("V(%1)").arg(n);
            }
        }
        words.removeDuplicates();
        words.sort();
        auto* model = new QStringListModel(words, m_completer);
        m_completer->setModel(model);
    };
    updateCompleter();

    connect(m_completer, QOverload<const QString&>::of(&QCompleter::activated), this, [this](const QString& completion) {
        QTextCursor tc = m_expr->textCursor();
        tc.select(QTextCursor::WordUnderCursor);
        tc.removeSelectedText();
        tc.insertText(completion);
        m_expr->setTextCursor(tc);
    });

    connect(m_expr, &QPlainTextEdit::textChanged, this, &BehavioralCurrentSourceDialog::updateUi);
    connect(m_previewValue, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &BehavioralCurrentSourceDialog::updateUi);
    connect(m_previewTime, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &BehavioralCurrentSourceDialog::updateUi);

    connect(m_insertNodeBtn, &QPushButton::clicked, this, [this]() {
        if (!m_scene) return;
        NetManager nm;
        nm.updateNets(m_scene);
        QStringList nets = nm.netNames();
        nets.removeAll("0");
        nets.removeAll("GND");
        nets.sort();
        bool ok = false;
        QString picked = QInputDialog::getItem(this, "Insert Node", "Node:", nets, 0, false, &ok);
        if (!ok || picked.isEmpty()) return;
        QTextCursor cursor = m_expr->textCursor();
        cursor.insertText(QString("V(%1)").arg(picked));
        m_expr->setTextCursor(cursor);
    });

    connect(preset1, &QPushButton::clicked, this, [this]() { m_expr->setPlainText("I=V(in)/1k"); });
    connect(preset2, &QPushButton::clicked, this, [this]() { m_expr->setPlainText("I=if(V(in)>0,1m,0)"); });
    connect(preset3, &QPushButton::clicked, this, [this]() { m_expr->setPlainText("I=table(time, 0 0 1m 1m)"); });

    updateUi();

    if (ThemeManager::theme()) {
        setStyleSheet(ThemeManager::theme()->widgetStylesheet());
    }
}

void BehavioralCurrentSourceDialog::updateUi() {
    const QString expr = m_expr->toPlainText();
    const QString msg = validateExpr(expr);
    if (msg.isEmpty()) {
        m_status->setText("Expression looks good.");
        m_status->setStyleSheet("color: #008800; font-size: 11px;");
    } else {
        m_status->setText(msg);
        if (msg.contains("Unbalanced")) {
            m_status->setStyleSheet("color: #cc0000; font-size: 11px;");
        } else {
            m_status->setStyleSheet("color: #996600; font-size: 11px;");
        }
    }

    QString preview = "Preview: ";
    QString normalized = normalizeExpr(expr);
    QString evalExpr = normalized;
    evalExpr.remove(0, 2); // strip I=
    if (evalExpr.contains("if(", Qt::CaseInsensitive) || evalExpr.contains("table(", Qt::CaseInsensitive)) {
        m_preview->setText(preview + "not available for if()/table().");
        return;
    }

    QRegularExpression reV("V\\s*\\([^\\)]*\\)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpression reI("I\\s*\\([^\\)]*\\)", QRegularExpression::CaseInsensitiveOption);
    evalExpr = evalExpr.replace(reV, QString::number(m_previewValue->value(), 'g', 8));
    evalExpr = evalExpr.replace(reI, QString::number(m_previewValue->value(), 'g', 8));
    evalExpr.replace(QRegularExpression("\\btime\\b", QRegularExpression::CaseInsensitiveOption),
                     QString::number(m_previewTime->value(), 'g', 8));

    evalExpr.replace("abs(", "Math.abs(");
    evalExpr.replace("sin(", "Math.sin(");
    evalExpr.replace("cos(", "Math.cos(");
    evalExpr.replace("tan(", "Math.tan(");
    evalExpr.replace("sqrt(", "Math.sqrt(");
    evalExpr.replace("exp(", "Math.exp(");
    evalExpr.replace("log(", "Math.log(");
    evalExpr.replace("min(", "Math.min(");
    evalExpr.replace("max(", "Math.max(");
    evalExpr.replace("pow(", "Math.pow(");

    QJSEngine engine;
    QJSValue result = engine.evaluate(evalExpr);
    if (result.isError()) {
        m_preview->setText(preview + "error evaluating expression.");
    } else {
        m_preview->setText(preview + result.toString());
    }
}

void BehavioralCurrentSourceDialog::applyChanges() {
    if (!m_item) return;
    m_item->setValue(m_expr->toPlainText());
    m_item->update();
}

bool BehavioralCurrentSourceDialog::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_expr && m_completer) {
        if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (m_completer->popup()->isVisible()) {
                switch (keyEvent->key()) {
                    case Qt::Key_Enter:
                    case Qt::Key_Return:
                    case Qt::Key_Escape:
                    case Qt::Key_Tab:
                    case Qt::Key_Backtab:
                        keyEvent->ignore();
                        return true;
                }
            }

            const bool ctrlSpace = (keyEvent->modifiers() & Qt::ControlModifier) && keyEvent->key() == Qt::Key_Space;
            if (ctrlSpace) {
                QTextCursor tc = m_expr->textCursor();
                tc.select(QTextCursor::WordUnderCursor);
                QString prefix = tc.selectedText();
                if (prefix.isEmpty()) prefix = "V(";
                m_completer->setCompletionPrefix(prefix);
                m_completer->complete(m_expr->cursorRect());
                return true;
            }
        }
    }

    return QDialog::eventFilter(obj, event);
}
