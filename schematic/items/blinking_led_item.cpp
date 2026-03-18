#include "blinking_led_item.h"
#include <QPainter>
#include <QJsonObject>
#include <QRegularExpression>

BlinkingLEDItem::BlinkingLEDItem(QPointF pos, QGraphicsItem* parent)
    : SchematicItem(parent) {
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    setReference("D?");
    setValue("1Hz");
    m_voltage = 0.0;
    m_powered = true;
    m_blinkOn = true;
    m_blinkEnabled = true;
    m_threshold = 1.5;
    m_colorName = "RED";

    m_blinkTimer = new QTimer(this);
    connect(m_blinkTimer, &QTimer::timeout, this, [this]() {
        m_blinkOn = !m_blinkOn;
        update();
    });
    updateBlinkTimer();
}

void BlinkingLEDItem::setValue(const QString& value) {
    SchematicItem::setValue(value);
    m_blinkHz = parseBlinkHz(value);
    m_colorName = parseColorName(value);
    if (m_blinkHz <= 0.0) m_blinkHz = 1.0;
    updateBlinkTimer();
    updateLabelText();
}

void BlinkingLEDItem::setBlinkEnabled(bool on) {
    m_blinkEnabled = on;
    update();
}

void BlinkingLEDItem::setBlinkHz(double hz) {
    m_blinkHz = hz;
    if (m_blinkHz <= 0.0) m_blinkHz = 1.0;
    updateBlinkTimer();
    updateValueString();
}

void BlinkingLEDItem::setColorName(const QString& name) {
    if (name.isEmpty()) return;
    m_colorName = name.toUpper();
    updateValueString();
}

double BlinkingLEDItem::parseBlinkHz(const QString& value) const {
    QString v = value.trimmed();
    if (v.isEmpty()) return 1.0;
    QRegularExpression re(R"(([0-9]*\.?[0-9]+))");
    auto m = re.match(v);
    if (!m.hasMatch()) return 1.0;
    return m.captured(1).toDouble();
}

QString BlinkingLEDItem::parseColorName(const QString& value) const {
    QString v = value.toUpper();
    if (v.contains("GREEN")) return "GREEN";
    if (v.contains("BLUE")) return "BLUE";
    if (v.contains("YELLOW")) return "YELLOW";
    if (v.contains("RED")) return "RED";
    return "RED";
}

void BlinkingLEDItem::updateValueString() {
    QString val = m_colorName;
    if (m_blinkEnabled) {
        val += QString(" %1Hz").arg(m_blinkHz);
    }
    SchematicItem::setValue(val);
    updateLabelText();
}

void BlinkingLEDItem::updateBlinkTimer() {
    if (!m_blinkTimer) return;
    double hz = m_blinkHz;
    if (hz < 0.1) hz = 0.1;
    int intervalMs = static_cast<int>(1000.0 / hz);
    if (intervalMs < 50) intervalMs = 50;
    m_blinkTimer->setInterval(intervalMs);
    if (!m_blinkTimer->isActive()) m_blinkTimer->start();
}

void BlinkingLEDItem::setSimState(const QMap<QString, double>& nodeVoltages,
                                  const QMap<QString, double>&) {
    QString nAnode = pinNet(0);
    QString nCathode = pinNet(1);
    double vAnode = nodeVoltages.value(nAnode, 0.0);
    double vCathode = nodeVoltages.value(nCathode, 0.0);

    if (nAnode.isEmpty()) vAnode = nodeVoltages.value(m_reference + ".1", 0.0);
    if (nCathode.isEmpty()) vCathode = nodeVoltages.value(m_reference + ".2", 0.0);

    m_voltage = vAnode - vCathode;
    if (nodeVoltages.isEmpty()) {
        m_powered = true;
    } else {
        m_powered = (m_voltage > m_threshold);
    }
    update();
}

QRectF BlinkingLEDItem::boundingRect() const { return QRectF(-50, -50, 100, 100); }

void BlinkingLEDItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    QColor baseColor = Qt::red;
    QString colorVal = m_colorName.toUpper();
    if (colorVal.contains("GREEN")) baseColor = Qt::green;
    else if (colorVal.contains("BLUE")) baseColor = Qt::blue;
    else if (colorVal.contains("YELLOW")) baseColor = QColor(255, 255, 0);

    bool lit = m_powered && (!m_blinkEnabled || m_blinkOn);
    double brightness = lit ? std::clamp((m_voltage - m_threshold) / 0.7, 0.0, 1.0) : 0.0;

    painter->setPen(QPen(Qt::white, 2));
    painter->setBrush(Qt::NoBrush);
    painter->drawLine(-45, 0, -20, 0);
    painter->drawLine(20, 0, 45, 0);

    if (lit) {
        QRadialGradient coreHalo(0, 0, 22);
        QColor c1 = baseColor;
        c1.setAlpha(static_cast<int>(120 * brightness));
        coreHalo.setColorAt(0, c1);
        coreHalo.setColorAt(0.4, c1);
        coreHalo.setColorAt(1, Qt::transparent);
        painter->setBrush(coreHalo);
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(-22, -22, 44, 44);

        QRadialGradient outerGlow(0, 0, 38);
        QColor c2 = baseColor;
        c2.setAlpha(static_cast<int>(40 * brightness));
        outerGlow.setColorAt(0, c2);
        outerGlow.setColorAt(1, Qt::transparent);
        painter->setBrush(outerGlow);
        painter->drawEllipse(-38, -38, 76, 76);
    }

    QRectF lensRect(-10, -10, 20, 20);
    QRadialGradient lensGrad(-3, -3, 10);
    if (lit) {
        lensGrad.setColorAt(0, baseColor.lighter(150));
        lensGrad.setColorAt(1, baseColor);
    } else {
        lensGrad.setColorAt(0, baseColor.darker(200));
        lensGrad.setColorAt(1, baseColor.darker(400));
    }
    painter->setBrush(lensGrad);
    painter->setPen(QPen(Qt::black, 0.5));
    painter->drawEllipse(lensRect);

    if (lit) {
        painter->setBrush(QColor(255, 255, 255, 180));
        painter->drawEllipse(-6, -6, 5, 4);
    } else {
        painter->setBrush(QColor(255, 255, 255, 40));
        painter->drawEllipse(-6, -6, 4, 3);
    }

    painter->setPen(QPen(lit ? Qt::white : QColor(100, 100, 100), 1.5));
    painter->setBrush(Qt::NoBrush);
    QPolygonF poly;
    poly << QPointF(-8, -8) << QPointF(-8, 8) << QPointF(8, 0);
    painter->drawPolygon(poly);
    painter->drawLine(8, -8, 8, 8);

    if (lit) {
        painter->setPen(QPen(baseColor.lighter(120), 1));
        painter->drawLine(10, -10, 16, -16);
        painter->drawLine(13, -7, 19, -13);
    }

    drawConnectionPointHighlights(painter);
}

QList<QPointF> BlinkingLEDItem::connectionPoints() const {
    return { QPointF(-45, 0), QPointF(45, 0) };
}

QJsonObject BlinkingLEDItem::toJson() const {
    QJsonObject j = SchematicItem::toJson();
    j["type"] = "Blinking LED";
    j["blinkHz"] = m_blinkHz;
    j["blinkEnabled"] = m_blinkEnabled;
    j["threshold"] = m_threshold;
    j["color"] = m_colorName;
    return j;
}

bool BlinkingLEDItem::fromJson(const QJsonObject& j) {
    if (!SchematicItem::fromJson(j)) return false;
    if (j.contains("blinkHz")) {
        m_blinkHz = j["blinkHz"].toDouble(1.0);
    } else {
        m_blinkHz = parseBlinkHz(m_value);
    }
    if (j.contains("blinkEnabled")) m_blinkEnabled = j["blinkEnabled"].toBool(true);
    if (j.contains("threshold")) m_threshold = j["threshold"].toDouble(1.5);
    if (j.contains("color")) m_colorName = j["color"].toString("RED").toUpper();
    updateBlinkTimer();
    updateValueString();
    return true;
}

SchematicItem* BlinkingLEDItem::clone() const {
    auto* item = new BlinkingLEDItem(pos());
    item->fromJson(this->toJson());
    return item;
}
