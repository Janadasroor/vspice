#include "voltage_source_item.h"
#include "schematic_text_item.h"
#include "theme_manager.h"
#include <QPainter>
#include <QJsonObject>
#include <cmath>

VoltageSourceItem::VoltageSourceItem(QPointF pos, const QString& value, SourceType type, QGraphicsItem* parent)
    : SchematicItem(parent), m_sourceType(type), m_dcVoltage(5.0), 
      m_sineAmplitude(1.0), m_sineFrequency(1000.0), m_sineOffset(0.0), m_sineDelay(0.0), m_sineTheta(0.0), m_sinePhi(0.0), m_sineNcycles(0),
      m_pulseV1(0.0), m_pulseV2(5.0), m_pulseDelay(0.0), m_pulseRise(1e-6), m_pulseFall(1e-6), 
      m_pulseWidth(0.5e-3), m_pulsePeriod(1e-3), m_pulseNcycles(0),
      m_expV1(0.0), m_expV2(5.0), m_expTd1(0.0), m_expTau1(1e-3), m_expTd2(2e-3), m_expTau2(1e-3),
      m_sffmOff(0.0), m_sffmAmplit(1.0), m_sffmCarrier(1000.0), m_sffmModIndex(1.0), m_sffmSignalFreq(100.0),
      m_acAmplitude(1.0), m_acPhase(0.0),
      m_seriesResistance(0.0), m_parallelCapacitance(0.0),
      m_showFunction(true), m_showDc(true), m_showAc(true), m_showParasitic(true) {
    
    m_value = value;
    setPos(pos);
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemIsMovable, true);

    PCBTheme* theme = ThemeManager::theme();
    m_pen = QPen(theme ? theme->schematicLine() : Qt::white, 2);
    m_brush = QBrush(Qt::NoBrush);
    
    rebuildPrimitives();
    createLabels(QPointF(35, -15), QPointF(35, 15));
}

QString VoltageSourceItem::itemTypeName() const {
    switch (m_sourceType) {
        case DC: return "Voltage_Source_DC";
        case Sine: return "Voltage_Source_Sine";
        case Pulse: return "Voltage_Source_Pulse";
        case EXP: return "Voltage_Source_EXP";
        case SFFM: return "Voltage_Source_SFFM";
        case PWL: return "Voltage_Source_PWL";
        case PWLFile: return "Voltage_Source_PWLFile";
        default: return "Voltage_Source_DC";
    }
}

void VoltageSourceItem::setValue(const QString& val) {
    m_value = val;
    QString v = val.trimmed().toUpper();

    // Helper to parse engineering suffixes
    auto parseEng = [](const QString& str) -> double {
        QString s = str.trimmed();
        s.remove(QRegularExpression("[sSvVhHzZ]+$"));
        
        double multiplier = 1.0;
        if (s.endsWith('m', Qt::CaseInsensitive)) { multiplier = 1e-3; s.chop(1); }
        else if (s.endsWith('u', Qt::CaseInsensitive)) { multiplier = 1e-6; s.chop(1); }
        else if (s.endsWith('n', Qt::CaseInsensitive)) { multiplier = 1e-9; s.chop(1); }
        else if (s.endsWith('p', Qt::CaseInsensitive)) { multiplier = 1e-12; s.chop(1); }
        else if (s.endsWith('k', Qt::CaseInsensitive)) { multiplier = 1e3; s.chop(1); }
        else if (s.endsWith("meg", Qt::CaseInsensitive)) { multiplier = 1e6; s.chop(3); }
        else if (s.endsWith('M', Qt::CaseSensitive)) { multiplier = 1e6; s.chop(1); }
        else if (s.endsWith('G', Qt::CaseInsensitive)) { multiplier = 1e9; s.chop(1); }
        
        bool ok;
        double val = s.toDouble(&ok);
        return ok ? val * multiplier : 0.0;
    };

    if (v.contains("SINE")) {
        m_sourceType = Sine;
        QRegularExpression re("SINE\\s*\\(([^\\)]*)\\)");
        auto match = re.match(v);
        if (match.hasMatch()) {
            QStringList parts = match.captured(1).split(QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts);
            if (parts.size() >= 1) m_sineOffset = parseEng(parts[0]);
            if (parts.size() >= 2) m_sineAmplitude = parseEng(parts[1]);
            if (parts.size() >= 3) m_sineFrequency = parseEng(parts[2]);
            if (parts.size() >= 4) m_sineDelay = parseEng(parts[3]);
            if (parts.size() >= 5) m_sineTheta = parseEng(parts[4]);
            if (parts.size() >= 6) m_sinePhi = parseEng(parts[5]);
            if (parts.size() >= 7) m_sineNcycles = (int)parseEng(parts[6]);
        }
    } else if (v.contains("PULSE")) {
        m_sourceType = Pulse;
        QRegularExpression re("PULSE\\s*\\(([^\\)]*)\\)");
        auto match = re.match(v);
        if (match.hasMatch()) {
            QStringList parts = match.captured(1).split(QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts);
            if (parts.size() >= 1) m_pulseV1 = parseEng(parts[0]);
            if (parts.size() >= 2) m_pulseV2 = parseEng(parts[1]);
            if (parts.size() >= 3) m_pulseDelay = parseEng(parts[2]);
            if (parts.size() >= 4) m_pulseRise = parseEng(parts[3]);
            if (parts.size() >= 5) m_pulseFall = parseEng(parts[4]);
            if (parts.size() >= 6) m_pulseWidth = parseEng(parts[5]);
            if (parts.size() >= 7) m_pulsePeriod = parseEng(parts[6]);
            if (parts.size() >= 8) m_pulseNcycles = (int)parseEng(parts[7]);
        }
    } else if (v.contains("EXP")) {
        m_sourceType = EXP;
        QRegularExpression re("EXP\\s*\\(([^\\)]*)\\)");
        auto match = re.match(v);
        if (match.hasMatch()) {
            QStringList parts = match.captured(1).split(QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts);
            if (parts.size() >= 1) m_expV1 = parseEng(parts[0]);
            if (parts.size() >= 2) m_expV2 = parseEng(parts[1]);
            if (parts.size() >= 3) m_expTd1 = parseEng(parts[2]);
            if (parts.size() >= 4) m_expTau1 = parseEng(parts[3]);
            if (parts.size() >= 5) m_expTd2 = parseEng(parts[4]);
            if (parts.size() >= 6) m_expTau2 = parseEng(parts[5]);
        }
    } else if (v.contains("SFFM")) {
        m_sourceType = SFFM;
        QRegularExpression re("SFFM\\s*\\(([^\\)]*)\\)");
        auto match = re.match(v);
        if (match.hasMatch()) {
            QStringList parts = match.captured(1).split(QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts);
            if (parts.size() >= 1) m_sffmOff = parseEng(parts[0]);
            if (parts.size() >= 2) m_sffmAmplit = parseEng(parts[1]);
            if (parts.size() >= 3) m_sffmCarrier = parseEng(parts[2]);
            if (parts.size() >= 4) m_sffmModIndex = parseEng(parts[3]);
            if (parts.size() >= 5) m_sffmSignalFreq = parseEng(parts[4]);
        }
    } else {
        // DC or Simple value
        m_sourceType = DC;
        QString dcPart = v;
        if (v.contains("AC")) {
             int acIdx = v.indexOf("AC");
             dcPart = v.left(acIdx).trimmed();
             QString acPart = v.mid(acIdx + 2).trimmed();
             QStringList acItems = acPart.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
             if (!acItems.isEmpty()) m_acAmplitude = parseEng(acItems[0]);
             if (acItems.size() > 1) m_acPhase = parseEng(acItems[1]);
        }
        bool ok;
        double dc = dcPart.remove('V').toDouble(&ok);
        if (ok) m_dcVoltage = dc;
    }

    // Parse parasitics from tail if present Rser=... Cpar=...
    if (v.contains("RSER=")) {
        QRegularExpression re("RSER=([-+]?[0-9]*\\.?[0-9]+[a-zA-Z]*)");
        auto match = re.match(v);
        if (match.hasMatch()) m_seriesResistance = parseEng(match.captured(1));
    }
    if (v.contains("CPAR=")) {
        QRegularExpression re("CPAR=([-+]?[0-9]*\\.?[0-9]+[a-zA-Z]*)");
        auto match = re.match(v);
        if (match.hasMatch()) m_parallelCapacitance = parseEng(match.captured(1));
    }

    rebuildPrimitives();
    updateLabelText();
    update();
}

void VoltageSourceItem::setSourceType(SourceType type) {
    m_sourceType = type;
    updateValue();
    rebuildPrimitives();
    update();
}

void VoltageSourceItem::updateValue() {
    QString tail;
    if (m_seriesResistance > 0) tail += QString(" Rser=%1").arg(m_seriesResistance);
    if (m_parallelCapacitance > 0) tail += QString(" Cpar=%1").arg(m_parallelCapacitance);

    QString acStr;
    if (m_acAmplitude != 0) {
        acStr = QString(" AC %1").arg(m_acAmplitude);
        if (m_acPhase != 0) acStr += QString(" %1").arg(m_acPhase);
    }

    switch (m_sourceType) {
        case DC: m_value = QString("%1V%2%3").arg(m_dcVoltage).arg(acStr).arg(tail); break;
        case Sine: m_value = QString("SINE(%1 %2 %3 %4 %5 %6 %7)%8%9")
                                .arg(m_sineOffset).arg(m_sineAmplitude).arg(m_sineFrequency)
                                .arg(m_sineDelay).arg(m_sineTheta).arg(m_sinePhi).arg(m_sineNcycles)
                                .arg(acStr).arg(tail); break;
        case Pulse: m_value = QString("PULSE(%1 %2 %3 %4 %5 %6 %7 %8)%9%10")
                                .arg(m_pulseV1).arg(m_pulseV2).arg(m_pulseDelay)
                                .arg(m_pulseRise).arg(m_pulseFall).arg(m_pulseWidth).arg(m_pulsePeriod).arg(m_pulseNcycles)
                                .arg(acStr).arg(tail); break;
        case EXP:   m_value = QString("EXP(%1 %2 %3 %4 %5 %6)%7%8")
                                .arg(m_expV1).arg(m_expV2).arg(m_expTd1).arg(m_expTau1).arg(m_expTd2).arg(m_expTau2)
                                .arg(acStr).arg(tail); break;
        case SFFM:  m_value = QString("SFFM(%1 %2 %3 %4 %5)%6%7")
                                .arg(m_sffmOff).arg(m_sffmAmplit).arg(m_sffmCarrier).arg(m_sffmModIndex).arg(m_sffmSignalFreq)
                                .arg(acStr).arg(tail); break;
        case PWL:   m_value = QString("PWL(%1)%2%3").arg(m_pwlPoints).arg(acStr).arg(tail); break;
        case PWLFile: m_value = QString("PWL FILE \"%1\"%2%3").arg(m_pwlFile).arg(acStr).arg(tail); break;
        default: break;
    }
}

void VoltageSourceItem::rebuildPrimitives() {
    m_primitives.clear();
    
    // leads
    m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(0, -45), QPointF(0, -25)));
    m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(0, 25), QPointF(0, 45)));
    
    // Outer Circle
    m_primitives.push_back(std::make_unique<CirclePrimitive>(QPointF(0, 0), 25, false));
    
    if (m_sourceType == DC) {
        m_primitives.push_back(std::make_unique<TextPrimitive>("+", QPointF(0, -12), 12));
        m_primitives.push_back(std::make_unique<TextPrimitive>("-", QPointF(0, 12), 12));
    } else if (m_sourceType == Sine) {
        QList<QPointF> sineWave;
        for (int i = -15; i <= 15; ++i) {
            sineWave.append(QPointF(i, -10 * std::sin(i * M_PI / 15.0)));
        }
        m_primitives.push_back(std::make_unique<PolygonPrimitive>(sineWave, false)); 
    } else if (m_sourceType == Pulse) {
        QList<QPointF> pulseWave;
        pulseWave << QPointF(-15, 8) << QPointF(-10, 8) << QPointF(-10, -8) << QPointF(0, -8) << QPointF(0, 8) << QPointF(10, 8) << QPointF(10, -8) << QPointF(15, -8);
        m_primitives.push_back(std::make_unique<PolygonPrimitive>(pulseWave, false));
    }
    
    // Pin markers
    m_primitives.push_back(std::make_unique<CirclePrimitive>(QPointF(0, -45), 4, true));
    m_primitives.push_back(std::make_unique<CirclePrimitive>(QPointF(0, 45), 4, true));
}

QRectF VoltageSourceItem::boundingRect() const {
    QRectF rect;
    for (const auto& prim : m_primitives) {
        rect = rect.united(prim->boundingRect());
    }
    return rect.adjusted(-5, -5, 5, 5);
}

void VoltageSourceItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(option)
    Q_UNUSED(widget)
    
    PCBTheme* theme = ThemeManager::theme();
    m_pen.setColor(theme ? theme->schematicLine() : Qt::white);
    
    if (m_sourceType == Sine) {
        // ...
    }
    
    for (const auto& prim : m_primitives) {
        prim->paint(painter, m_pen, m_brush);
    }
    
    drawConnectionPointHighlights(painter);
    
    if (isSelected()) {
        painter->setPen(QPen(theme->selectionBox(), 1, Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(boundingRect().adjusted(2, 2, -2, -2));
    }
}

QJsonObject VoltageSourceItem::toJson() const {
    QJsonObject json = SchematicItem::toJson();
    json["type"] = itemTypeName();
    json["sourceType"] = (int)m_sourceType;
    json["dcVoltage"] = m_dcVoltage;
    
    // Sine
    json["sineAmplitude"] = m_sineAmplitude;
    json["sineFrequency"] = m_sineFrequency;
    json["sineOffset"] = m_sineOffset;
    
    // Pulse
    json["pulseV1"] = m_pulseV1;
    json["pulseV2"] = m_pulseV2;
    json["pulseDelay"] = m_pulseDelay;
    json["pulseRise"] = m_pulseRise;
    json["pulseFall"] = m_pulseFall;
    json["pulseWidth"] = m_pulseWidth;
    json["pulsePeriod"] = m_pulsePeriod;
    
    return json;
}

bool VoltageSourceItem::fromJson(const QJsonObject& json) {
    if (!SchematicItem::fromJson(json)) return false;
    
    m_sourceType = static_cast<SourceType>(json["sourceType"].toInt(DC));
    m_dcVoltage = json["dcVoltage"].toDouble(5.0);
    
    m_sineAmplitude = json["sineAmplitude"].toDouble(1.0);
    m_sineFrequency = json["sineFrequency"].toDouble(1000.0);
    m_sineOffset = json["sineOffset"].toDouble(0.0);
    
    m_pulseV1 = json["pulseV1"].toDouble(0.0);
    m_pulseV2 = json["pulseV2"].toDouble(5.0);
    m_pulseDelay = json["pulseDelay"].toDouble(0.0);
    m_pulseRise = json["pulseRise"].toDouble(1e-6);
    m_pulseFall = json["pulseFall"].toDouble(1e-6);
    m_pulseWidth = json["pulseWidth"].toDouble(0.5e-3);
    m_pulsePeriod = json["pulsePeriod"].toDouble(1e-3);
    
    rebuildPrimitives();
    update();
    return true;
}

void VoltageSourceItem::onInteractiveDoubleClick(const QPointF&) {
    // This will be connected to the properties dialog in Phase 2
}

SchematicItem* VoltageSourceItem::clone() const {
    VoltageSourceItem* newItem = new VoltageSourceItem(pos(), m_value, m_sourceType, parentItem());
    newItem->fromJson(toJson());
    return newItem;
}

QList<QPointF> VoltageSourceItem::connectionPoints() const {
    return { QPointF(0, -45), QPointF(0, 45) }; // [0] is positive, [1] is negative
}
