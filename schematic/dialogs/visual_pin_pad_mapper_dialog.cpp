#include "visual_pin_pad_mapper_dialog.h"

#include "../items/schematic_item.h"
#include "../../footprints/footprint_library.h"
#include "../../footprints/models/footprint_definition.h"
#include "../../footprints/models/footprint_primitive.h"
#include "../../symbols/models/symbol_definition.h"
#include "../../symbols/models/symbol_primitive.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QMessageBox>
#include <QSet>
#include <algorithm>

using Flux::Model::FootprintDefinition;
using Flux::Model::FootprintPrimitive;
using Flux::Model::SymbolDefinition;
using Flux::Model::SymbolPrimitive;

namespace {

bool numericLess(const QString& a, const QString& b) {
    bool aOk = false;
    bool bOk = false;
    const int ai = a.toInt(&aOk);
    const int bi = b.toInt(&bOk);
    if (aOk && bOk) return ai < bi;
    if (aOk != bOk) return aOk;
    return a.toLower() < b.toLower();
}

}

PinPadMapperWidget::PinPadMapperWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumHeight(320);
    setMouseTracking(true);
}

void PinPadMapperWidget::setPinsAndPads(const QStringList& pins, const QStringList& pads) {
    m_pins = pins;
    m_pads = pads;
    updateLayout();
    update();
}

void PinPadMapperWidget::setMapping(const QMap<QString, QString>& mapping) {
    m_mapping.clear();
    for (auto it = mapping.begin(); it != mapping.end(); ++it) {
        const QString pin = it.key().trimmed();
        const QString pad = it.value().trimmed();
        if (!pin.isEmpty() && !pad.isEmpty() && m_pins.contains(pin) && m_pads.contains(pad)) {
            assignPinToPad(pin, pad);
        }
    }
    emit mappingChanged();
    update();
}

void PinPadMapperWidget::clearMapping() {
    if (m_mapping.isEmpty()) return;
    m_mapping.clear();
    emit mappingChanged();
    update();
}

void PinPadMapperWidget::autoMap() {
    m_mapping.clear();

    QSet<QString> usedPads;
    for (const QString& pin : m_pins) {
        if (m_pads.contains(pin)) {
            m_mapping[pin] = pin;
            usedPads.insert(pin);
        }
    }

    int nextPad = 0;
    for (const QString& pin : m_pins) {
        if (m_mapping.contains(pin)) continue;
        while (nextPad < m_pads.size() && usedPads.contains(m_pads[nextPad])) {
            ++nextPad;
        }
        if (nextPad >= m_pads.size()) break;
        m_mapping[pin] = m_pads[nextPad];
        usedPads.insert(m_pads[nextPad]);
        ++nextPad;
    }

    emit mappingChanged();
    update();
}

void PinPadMapperWidget::paintEvent(QPaintEvent*) {
    updateLayout();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(22, 22, 24));

    p.setPen(QColor(160, 160, 160));
    p.setFont(QFont("Inter", 10, QFont::Bold));
    p.drawText(QRectF(20, 8, width() / 2 - 20, 24), Qt::AlignLeft | Qt::AlignVCenter, "Symbol Pins");
    p.drawText(QRectF(width() / 2, 8, width() / 2 - 20, 24), Qt::AlignRight | Qt::AlignVCenter, "Footprint Pads");

    p.setPen(QPen(QColor(90, 140, 220), 2));
    for (auto it = m_mapping.begin(); it != m_mapping.end(); ++it) {
        QPointF pinPos;
        QPointF padPos;
        bool havePin = false;
        bool havePad = false;
        for (const auto& node : m_pinNodes) {
            if (node.name == it.key()) {
                pinPos = node.rect.center();
                havePin = true;
                break;
            }
        }
        for (const auto& node : m_padNodes) {
            if (node.name == it.value()) {
                padPos = node.rect.center();
                havePad = true;
                break;
            }
        }
        if (havePin && havePad) {
            p.drawLine(pinPos, padPos);
        }
    }

    if (m_dragging && !m_activePin.isEmpty()) {
        QPointF pinPos;
        for (const auto& node : m_pinNodes) {
            if (node.name == m_activePin) {
                pinPos = node.rect.center();
                break;
            }
        }
        p.setPen(QPen(QColor(220, 220, 220), 1, Qt::DashLine));
        p.drawLine(pinPos, m_dragPos);
    }

    for (const auto& node : m_pinNodes) {
        const bool active = (node.name == m_activePin);
        const bool mapped = m_mapping.contains(node.name);
        p.setPen(active ? QColor(240, 240, 240) : QColor(120, 120, 120));
        p.setBrush(mapped ? QColor(36, 90, 150) : QColor(50, 50, 55));
        p.drawRoundedRect(node.rect, 4, 4);
        p.setPen(QColor(235, 235, 235));
        p.drawText(node.rect.adjusted(8, 0, -8, 0), Qt::AlignLeft | Qt::AlignVCenter, node.name);
    }

    for (const auto& node : m_padNodes) {
        const bool used = m_mapping.values().contains(node.name);
        p.setPen(used ? QColor(240, 240, 240) : QColor(120, 120, 120));
        p.setBrush(used ? QColor(36, 90, 150) : QColor(50, 50, 55));
        p.drawRoundedRect(node.rect, 4, 4);
        p.setPen(QColor(235, 235, 235));
        p.drawText(node.rect.adjusted(8, 0, -8, 0), Qt::AlignRight | Qt::AlignVCenter, node.name);
    }
}

void PinPadMapperWidget::mousePressEvent(QMouseEvent* event) {
    for (const auto& pin : m_pinNodes) {
        if (pin.rect.contains(event->pos())) {
            m_dragging = true;
            m_activePin = pin.name;
            m_dragPos = event->pos();
            update();
            return;
        }
    }

    for (const auto& pad : m_padNodes) {
        if (!pad.rect.contains(event->pos())) continue;
        bool changed = false;
        for (auto it = m_mapping.begin(); it != m_mapping.end();) {
            if (it.value() == pad.name) {
                it = m_mapping.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
        if (changed) {
            emit mappingChanged();
            update();
        }
        return;
    }
}

void PinPadMapperWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!m_dragging) return;
    m_dragPos = event->pos();
    update();
}

void PinPadMapperWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (!m_dragging || m_activePin.isEmpty()) return;

    for (const auto& pad : m_padNodes) {
        if (pad.rect.contains(event->pos())) {
            assignPinToPad(m_activePin, pad.name);
            emit mappingChanged();
            break;
        }
    }

    m_dragging = false;
    m_activePin.clear();
    update();
}

void PinPadMapperWidget::updateLayout() {
    m_pinNodes.clear();
    m_padNodes.clear();

    const int marginX = 24;
    const int top = 36;
    const int rowH = 28;
    const int rowGap = 10;
    const int cardW = std::max(120, width() / 3);

    for (int i = 0; i < m_pins.size(); ++i) {
        PinNode node;
        node.name = m_pins[i];
        node.rect = QRectF(marginX, top + i * (rowH + rowGap), cardW, rowH);
        m_pinNodes.append(node);
    }

    for (int i = 0; i < m_pads.size(); ++i) {
        PinNode node;
        node.name = m_pads[i];
        node.rect = QRectF(width() - marginX - cardW, top + i * (rowH + rowGap), cardW, rowH);
        m_padNodes.append(node);
    }
}

void PinPadMapperWidget::assignPinToPad(const QString& pinName, const QString& padName) {
    for (auto it = m_mapping.begin(); it != m_mapping.end();) {
        if (it.value() == padName && it.key() != pinName) {
            it = m_mapping.erase(it);
        } else {
            ++it;
        }
    }
    m_mapping[pinName] = padName;
}

VisualPinPadMapperDialog::VisualPinPadMapperDialog(SchematicItem* item, const QString& footprintName, QWidget* parent)
    : QDialog(parent), m_item(item), m_footprintName(footprintName.trimmed()) {
    setWindowTitle("Pin/Pad Mapper");
    resize(720, 520);

    QVBoxLayout* layout = new QVBoxLayout(this);

    QLabel* title = new QLabel(QString("%1 (%2)  ->  %3")
                               .arg(item ? item->reference() : QString("?"),
                                    item ? item->name() : QString("?"),
                                    m_footprintName), this);
    title->setStyleSheet("font-weight: bold; color: #e5e7eb;");
    layout->addWidget(title);

    m_mapper = new PinPadMapperWidget(this);
    layout->addWidget(m_mapper, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet("color: #cbd5e1;");
    layout->addWidget(m_statusLabel);

    QHBoxLayout* actions = new QHBoxLayout();
    QPushButton* autoBtn = new QPushButton("Auto Map", this);
    QPushButton* clearBtn = new QPushButton("Clear", this);
    QPushButton* cancelBtn = new QPushButton("Cancel", this);
    m_applyButton = new QPushButton("Apply Mapping", this);
    m_applyButton->setStyleSheet("background:#0b72c9; color:white; font-weight:bold;");

    actions->addWidget(autoBtn);
    actions->addWidget(clearBtn);
    actions->addStretch();
    actions->addWidget(cancelBtn);
    actions->addWidget(m_applyButton);
    layout->addLayout(actions);

    buildPinsAndPads();
    m_mapper->setPinsAndPads(m_pinNames, m_padNames);
    if (m_item) {
        m_mapper->setMapping(m_item->pinPadMapping());
    }
    updateStatus();

    connect(autoBtn, &QPushButton::clicked, this, &VisualPinPadMapperDialog::onAutoMap);
    connect(clearBtn, &QPushButton::clicked, this, &VisualPinPadMapperDialog::onClear);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_applyButton, &QPushButton::clicked, this, &VisualPinPadMapperDialog::onAccept);
    connect(m_mapper, &PinPadMapperWidget::mappingChanged, this, &VisualPinPadMapperDialog::updateStatus);
}

void VisualPinPadMapperDialog::onAutoMap() {
    m_mapper->autoMap();
}

void VisualPinPadMapperDialog::onClear() {
    m_mapper->clearMapping();
}

void VisualPinPadMapperDialog::onAccept() {
    if (!m_item) {
        reject();
        return;
    }

    const QMap<QString, QString> mapping = m_mapper->mapping();
    int mappedPins = 0;
    for (const QString& pin : m_pinNames) {
        if (mapping.contains(pin) && !mapping.value(pin).trimmed().isEmpty()) {
            mappedPins++;
        }
    }

    const bool strictRequired = (m_item->itemType() == SchematicItem::ICType) ||
                                m_item->referencePrefix().toUpper().startsWith("U");
    if (strictRequired && mappedPins < m_pinNames.size()) {
        QMessageBox::warning(this, "Incomplete Mapping",
                             QString("This component requires full mapping: %1/%2 mapped.")
                                 .arg(mappedPins).arg(m_pinNames.size()));
        return;
    }

    m_item->setPinPadMapping(mapping);
    accept();
}

void VisualPinPadMapperDialog::updateStatus() {
    const QMap<QString, QString> mapping = m_mapper->mapping();
    int mappedPins = 0;
    for (const QString& pin : m_pinNames) {
        if (mapping.contains(pin) && !mapping.value(pin).trimmed().isEmpty()) {
            mappedPins++;
        }
    }

    const bool strictRequired = (m_item && ((m_item->itemType() == SchematicItem::ICType) ||
                                            m_item->referencePrefix().toUpper().startsWith("U")));
    const QString mode = strictRequired ? "Strict (IC)" : "Flexible";
    m_statusLabel->setText(
        QString("Mapped %1/%2 pins to %3 pads. Mode: %4. Click a pad to unmap it.")
            .arg(mappedPins).arg(m_pinNames.size()).arg(m_padNames.size()).arg(mode));
}

void VisualPinPadMapperDialog::buildPinsAndPads() {
    m_pinNames.clear();
    m_padNames.clear();

    if (!m_item) return;

    const QList<QPointF> points = m_item->connectionPoints();
    for (int i = 0; i < points.size(); ++i) {
        const QString pin = m_item->pinName(i).trimmed();
        m_pinNames.append(pin.isEmpty() ? QString::number(i + 1) : pin);
    }

    FootprintDefinition fp = FootprintLibraryManager::instance().findFootprint(m_footprintName);
    if (!fp.isValid()) return;
    m_padNames = sortedPadNames(fp);
}

QStringList VisualPinPadMapperDialog::sortedPadNames(const FootprintDefinition& def) const {
    QStringList pads;
    for (const auto& prim : def.primitives()) {
        if (prim.type != FootprintPrimitive::Pad) continue;
        const QString n = prim.data.value("number").toString().trimmed();
        if (!n.isEmpty()) pads.append(n);
    }
    pads.removeDuplicates();
    std::sort(pads.begin(), pads.end(), numericLess);
    return pads;
}
