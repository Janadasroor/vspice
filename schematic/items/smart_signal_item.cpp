#include "smart_signal_item.h"
#include <QPainter>
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>

SmartSignalItem::SmartSignalItem(QPointF pos, QGraphicsItem* parent)
    : SchematicItem(parent)
    , m_size(80, 60) {
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable |
             QGraphicsItem::ItemIsMovable |
             QGraphicsItem::ItemSendsGeometryChanges);
    
    // Default config
    m_inputPins << "In1";
    m_outputPins << "Out1";
    m_pythonCode = "class SmartSignal:\n    def update(self, t, inputs):\n        return inputs.get('In1', 0.0)";
    m_fluxCode = "def update(t) {\n    return V(\"In1\");\n}";
    m_engineType = EngineType::FluxScript;
    
    setReference("SB1");
    setName("Smart Block");
    updateSize();
    updateDocstring();
}


void SmartSignalItem::setInputPins(const QStringList& pins) {
    m_inputPins = pins;
    updateSize();
    rebuildPrimitives();
    update();
}

void SmartSignalItem::setOutputPins(const QStringList& pins) {
    m_outputPins = pins;
    updateSize();
    rebuildPrimitives();
    update();
}

void SmartSignalItem::updateSize() {
    int maxPins = std::max(m_inputPins.size(), m_outputPins.size());
    qreal height = std::max(60.0, maxPins * 20.0 + 20.0);
    const QSizeF newSize(100, height);
    if (m_size == newSize) return;

    prepareGeometryChange();
    m_size = newSize;
}

QRectF SmartSignalItem::boundingRect() const {
    return QRectF(-m_size.width()/2 - 10, -m_size.height()/2, m_size.width() + 20, m_size.height());
}

void SmartSignalItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    
    QRectF rect(-m_size.width()/2, -m_size.height()/2, m_size.width(), m_size.height());
    
    // Draw body
    QPen bodyPen(Qt::white, 2);
    if (isSelected()) bodyPen.setColor(QColor(99, 102, 241)); // Accent color
    painter->setPen(bodyPen);
    painter->setBrush(QColor(30, 30, 35));
    painter->drawRoundedRect(rect, 4, 4);
    
    // Draw Preview Waveform (Mini-Scope)
    if (!m_previewPoints.isEmpty()) {
        painter->setPen(QPen(QColor(139, 92, 246, 180), 1.5)); // Semi-transparent purple
        
        // Map points to a small area in the center of the block
        QRectF previewRect = rect.adjusted(15, 15, -15, -15);
        
        // Find min/max for scaling
        double minX = m_previewPoints[0].x();
        double maxX = m_previewPoints[0].x();
        double minY = m_previewPoints[0].y();
        double maxY = m_previewPoints[0].y();
        for (const auto& p : m_previewPoints) {
            minX = qMin(minX, p.x());
            maxX = qMax(maxX, p.x());
            minY = qMin(minY, p.y());
            maxY = qMax(maxY, p.y());
        }
        
        if (qAbs(maxY - minY) < 0.001) { minY -= 1.0; maxY += 1.0; }
        if (qAbs(maxX - minX) < 0.001) { maxX += 1.0; }

        QPainterPath path;
        for (int i = 0; i < m_previewPoints.size(); ++i) {
            double x = previewRect.left() + (m_previewPoints[i].x() - minX) / (maxX - minX) * previewRect.width();
            double y = previewRect.bottom() - (m_previewPoints[i].y() - minY) / (maxY - minY) * previewRect.height();
            if (i == 0) path.moveTo(x, y);
            else path.lineTo(x, y);
        }
        painter->drawPath(path);
    } else {
        // Fallback: Draw engine indicator
        if (m_engineType == EngineType::FluxScript) {
            painter->setPen(QColor(16, 185, 129)); // High-performance Green
            painter->drawText(rect.adjusted(5, 5, -5, -m_size.height() + 15), Qt::AlignCenter, "FLUX JIT LOGIC");
        } else {
            painter->setPen(QColor(139, 92, 246)); // Purple
            painter->drawText(rect.adjusted(5, 5, -5, -m_size.height() + 15), Qt::AlignCenter, "PYTHON LOGIC");
        }
        QFont font("Inter", 7, QFont::Bold);
        painter->setFont(font);
    }


    // Draw Input Pins (Left)
    painter->setPen(QPen(Qt::white, 1.5));
    QFont pinFont("Inter", 8);
    painter->setFont(pinFont);
    
    qreal startY = -m_size.height()/2 + 20;
    for (int i = 0; i < m_inputPins.size(); ++i) {
        qreal y = startY + i * 20;
        painter->drawLine(-m_size.width()/2 - 10, y, -m_size.width()/2, y);
        painter->drawText(QRectF(-m_size.width()/2 + 5, y - 10, 40, 20), Qt::AlignLeft | Qt::AlignVCenter, m_inputPins[i]);
    }

    // Draw Output Pins (Right)
    for (int i = 0; i < m_outputPins.size(); ++i) {
        qreal y = startY + i * 20;
        painter->drawLine(m_size.width()/2, y, m_size.width()/2 + 10, y);
        painter->drawText(QRectF(m_size.width()/2 - 45, y - 10, 40, 20), Qt::AlignRight | Qt::AlignVCenter, m_outputPins[i]);
    }

    drawConnectionPointHighlights(painter);
}

QList<QPointF> SmartSignalItem::connectionPoints() const {
    QList<QPointF> pts;
    qreal startY = -m_size.height()/2 + 20;
    
    for (int i = 0; i < m_inputPins.size(); ++i) {
        pts << QPointF(-m_size.width()/2 - 10, startY + i * 20);
    }
    for (int i = 0; i < m_outputPins.size(); ++i) {
        pts << QPointF(m_size.width()/2 + 10, startY + i * 20);
    }
    return pts;
}

QString SmartSignalItem::pinName(int index) const {
    if (index < 0) return QString();
    if (index < m_inputPins.size()) return m_inputPins[index];
    int outIdx = index - m_inputPins.size();
    if (outIdx < m_outputPins.size()) return m_outputPins[outIdx];
    return QString::number(index + 1);
}

QJsonObject SmartSignalItem::toJson() const {
    QJsonObject j = SchematicItem::toJson();
    j["type"] = itemTypeName();
    j["pythonCode"] = m_pythonCode;
    j["fluxCode"] = m_fluxCode;
    j["engineType"] = "flux";  // FluxScript only — Python is external orchestration

    
    QJsonArray inArray;
    for (const auto& p : m_inputPins) inArray.append(p);
    j["inputs"] = inArray;

    QJsonArray outArray;
    for (const auto& p : m_outputPins) outArray.append(p);
    j["outputs"] = outArray;

    QJsonObject paramObj;
    for (auto it = m_parameters.begin(); it != m_parameters.end(); ++it) {
        paramObj[it.key()] = it.value();
    }
    j["parameters"] = paramObj;

    QJsonArray testArray;
    for (const auto& tc : m_testCases) {
        QJsonObject tcObj;
        tcObj["name"] = tc.name;
        tcObj["time"] = tc.time;
        QJsonObject inObj, outObj;
        for (auto it = tc.inputs.begin(); it != tc.inputs.end(); ++it) inObj[it.key()] = it.value();
        for (auto it = tc.expectedOutputs.begin(); it != tc.expectedOutputs.end(); ++it) outObj[it.key()] = it.value();
        tcObj["inputs"] = inObj;
        tcObj["expected"] = outObj;
        testArray.append(tcObj);
    }
    j["test_cases"] = testArray;

    QJsonArray snapArray;
    for (const auto& s : m_snapshots) {
        QJsonObject sObj;
        sObj["name"] = s.name;
        sObj["code"] = s.code;
        sObj["timestamp"] = s.timestamp;
        snapArray.append(sObj);
    }
    j["snapshots"] = snapArray;

    return j;
}

bool SmartSignalItem::fromJson(const QJsonObject& json) {
    SchematicItem::fromJson(json);
    m_pythonCode = json["pythonCode"].toString();
    m_fluxCode = json["fluxCode"].toString();
    m_engineType = EngineType::FluxScript;  // Always FluxScript — Python is external

    
    m_inputPins.clear();
    QJsonArray inArray = json["inputs"].toArray();
    for (auto v : inArray) m_inputPins << v.toString();

    m_outputPins.clear();
    QJsonArray outArray = json["outputs"].toArray();
    for (auto v : outArray) m_outputPins << v.toString();

    m_parameters.clear();
    QJsonObject paramObj = json["parameters"].toObject();
    for (const QString& key : paramObj.keys()) {
        m_parameters[key] = paramObj[key].toDouble();
    }

    m_testCases.clear();
    QJsonArray testArray = json["test_cases"].toArray();
    for (auto v : testArray) {
        QJsonObject tcObj = v.toObject();
        TestCase tc;
        tc.name = tcObj["name"].toString();
        tc.time = tcObj["time"].toDouble();
        QJsonObject inObj = tcObj["inputs"].toObject();
        for (const QString& k : inObj.keys()) tc.inputs[k] = inObj[k].toDouble();
        QJsonObject outObj = tcObj["expected"].toObject();
        for (const QString& k : outObj.keys()) tc.expectedOutputs[k] = outObj[k].toDouble();
        m_testCases.append(tc);
    }

    m_snapshots.clear();
    QJsonArray snapArray = json["snapshots"].toArray();
    for (auto v : snapArray) {
        QJsonObject sObj = v.toObject();
        Snapshot s;
        s.name = sObj["name"].toString();
        s.code = sObj["code"].toString();
        s.timestamp = sObj["timestamp"].toString();
        m_snapshots.append(s);
    }

    updateSize();
    return true;
}

SchematicItem* SmartSignalItem::clone() const {
    auto* copy = new SmartSignalItem(pos());
    copy->fromJson(toJson());
    return copy;
}

QString SmartSignalItem::docstring() const {
    // Basic regex to find first """ or ''' block
    QRegularExpression re("(\"\"\"|''')(.*?)\\1", QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch match = re.match(m_pythonCode);
    if (match.hasMatch()) {
        return match.captured(2).trimmed();
    }
    return QString();
}

void SmartSignalItem::updateDocstring() {
    QString ds = docstring();
    if (!ds.isEmpty()) {
        setToolTip(ds);
        return;
    }

    const QString engineName =
        (m_engineType == EngineType::FluxScript) ? QStringLiteral("FluxScript")
                                                 : QStringLiteral("Python");
    setToolTip(QStringLiteral("Smart Signal Block (%1): %2").arg(engineName, name()));
}
