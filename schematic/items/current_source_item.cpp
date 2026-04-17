#include "current_source_item.h"
#include "schematic_text_item.h"
#include "theme_manager.h"
#include "../../symbols/symbol_library.h"
#include <QPainter>
#include <QJsonObject>

using Flux::Model::SymbolPrimitive;
using Flux::Model::SymbolDefinition;

CurrentSourceItem::CurrentSourceItem(QPointF pos, const QString& value, SourceType type, QGraphicsItem* parent)
    : SchematicItem(parent), m_sourceType(type), m_dcCurrent("1m"), 
      m_sineAmplitude("1m"), m_sineFrequency("1000"), m_sineOffset("0.0"), m_sineDelay("0"), m_sineTheta("0"), m_sinePhi("0"), m_sineNcycles("0"),
      m_pulseI1("0.0"), m_pulseI2("1m"), m_pulseDelay("0"), m_pulseRise("1u"), m_pulseFall("1u"), 
      m_pulseWidth("0.5m"), m_pulsePeriod("1m"), m_pulseNcycles("0"),
      m_expI1("0.0"), m_expI2("1m"), m_expTd1("0"), m_expTau1("1m"), m_expTd2("2m"), m_expTau2("1m"),
      m_sffmOff("0.0"), m_sffmAmplit("1m"), m_sffmCarrier("1000"), m_sffmModIndex("1.0"), m_sffmSignalFreq("100"),
      m_pwlRepeat(false),
      m_acAmplitude(""), m_acPhase(""),
      m_seriesResistance("0.0"), m_parallelCapacitance("0.0"),
      m_showFunction(true), m_showDc(true), m_showAc(true), m_showParasitic(true) {

    setExcludeFromPcb(true); // Current sources are excluded from PCB by default

    setPos(pos);
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemIsMovable, true);

    PCBTheme* theme = ThemeManager::theme();
    m_pen = QPen(theme ? theme->schematicLine() : Qt::white, 2);
    m_brush = QBrush(Qt::NoBrush);
    
    rebuildPrimitives();
    createLabels(QPointF(30, -15), QPointF(30, 15));
    setValue(value);
}

CurrentSourceItem::~CurrentSourceItem() {
    // Child graphics items are owned by this QGraphicsItem and are deleted by Qt.
}

QString CurrentSourceItem::itemTypeName() const {
    switch (m_sourceType) {
        case DC: return "Current_Source_DC";
        case Sine: return "Current_Source_Sine";
        case Pulse: return "Current_Source_Pulse";
        case EXP: return "Current_Source_EXP";
        case SFFM: return "Current_Source_SFFM";
        case PWL: return "Current_Source_PWL";
        case PWLFile: return "Current_Source_PWLFile";
        case Behavioral: return "Current_Source_Behavioral";
        default: return "Current_Source_DC";
    }
}

void CurrentSourceItem::setValue(const QString& val) {
    m_value = val;
    QString v = val.trimmed().toUpper();

    auto captureParams = [](const QString& v, const QString& func) -> QStringList {
        QRegularExpression re(func + "\\s*\\(([^\\)]*)\\)");
        auto match = re.match(v);
        if (match.hasMatch()) {
            return match.captured(1).split(QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts);
        }
        return {};
    };

    if (v.contains("SINE")) {
        m_sourceType = Sine;
        QStringList parts = captureParams(v, "SINE");
        if (parts.size() >= 1) m_sineOffset = parts[0];
        if (parts.size() >= 2) m_sineAmplitude = parts[1];
        if (parts.size() >= 3) m_sineFrequency = parts[2];
        if (parts.size() >= 4) m_sineDelay = parts[3];
        if (parts.size() >= 5) m_sineTheta = parts[4];
        if (parts.size() >= 6) m_sinePhi = parts[5];
        if (parts.size() >= 7) m_sineNcycles = parts[6];
    } else if (v.contains("PULSE")) {
        m_sourceType = Pulse;
        QStringList parts = captureParams(v, "PULSE");
        if (parts.size() >= 1) m_pulseI1 = parts[0];
        if (parts.size() >= 2) m_pulseI2 = parts[1];
        if (parts.size() >= 3) m_pulseDelay = parts[2];
        if (parts.size() >= 4) m_pulseRise = parts[3];
        if (parts.size() >= 5) m_pulseFall = parts[4];
        if (parts.size() >= 6) m_pulseWidth = parts[5];
        if (parts.size() >= 7) m_pulsePeriod = parts[6];
        if (parts.size() >= 8) m_pulseNcycles = parts[7];
    } else if (v.contains("EXP")) {
        m_sourceType = EXP;
        QStringList parts = captureParams(v, "EXP");
        if (parts.size() >= 1) m_expI1 = parts[0];
        if (parts.size() >= 2) m_expI2 = parts[1];
        if (parts.size() >= 3) m_expTd1 = parts[2];
        if (parts.size() >= 4) m_expTau1 = parts[3];
        if (parts.size() >= 5) m_expTd2 = parts[4];
        if (parts.size() >= 6) m_expTau2 = parts[5];
    } else if (v.contains("SFFM")) {
        m_sourceType = SFFM;
        QStringList parts = captureParams(v, "SFFM");
        if (parts.size() >= 1) m_sffmOff = parts[0];
        if (parts.size() >= 2) m_sffmAmplit = parts[1];
        if (parts.size() >= 3) m_sffmCarrier = parts[2];
        if (parts.size() >= 4) m_sffmModIndex = parts[3];
        if (parts.size() >= 5) m_sffmSignalFreq = parts[4];
    } else if (v.contains("PWL")) {
        QRegularExpression reFile("PWL\\s*\\([^\\)]*FILE\\s*=\\s*\\\"([^\\\"]+)\\\"[^\\)]*\\)", QRegularExpression::CaseInsensitiveOption);
        auto m1 = reFile.match(v);
        if (m1.hasMatch()) {
            m_sourceType = PWLFile;
            m_pwlFile = m1.captured(1);
        } else {
            QRegularExpression reFile2("PWL\\s*\\([^\\)]*FILE\\s*=\\s*([^\\)\\s]+)[^\\)]*\\)", QRegularExpression::CaseInsensitiveOption);
            auto m2 = reFile2.match(v);
            if (m2.hasMatch()) {
                m_sourceType = PWLFile;
                m_pwlFile = m2.captured(1);
            } else if (v.contains("PWL FILE")) {
                m_sourceType = PWLFile;
                QRegularExpression re("PWL\\s+FILE\\s+\\\"([^\\\"]+)\\\"", QRegularExpression::CaseInsensitiveOption);
                auto match = re.match(v);
                if (match.hasMatch()) m_pwlFile = match.captured(1);
            } else {
                m_sourceType = PWL;
                QStringList parts = captureParams(v, "PWL");
                m_pwlPoints = parts.join(" ");
            }
        }
    } else {
        m_sourceType = DC;
        QString dcPart = v;
        if (v.contains("AC")) {
             int acIdx = v.indexOf("AC");
             dcPart = v.left(acIdx).trimmed();
             QString acPart = v.mid(acIdx + 2).trimmed();
             QStringList acItems = acPart.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
             if (!acItems.isEmpty()) m_acAmplitude = acItems[0];
             if (acItems.size() > 1) m_acPhase = acItems[1];
        }
        
        QString valStripped = dcPart;
        if (valStripped.endsWith("A") && valStripped.length() > 1 && valStripped[valStripped.length()-2].isDigit()) {
            valStripped.chop(1);
        }
        m_dcCurrent = valStripped;

        if (v.startsWith("I=")) {
            m_sourceType = Behavioral;
        }
    }

    if (m_sourceType == PWL || m_sourceType == PWLFile) {
        QRegularExpression reRepeat("\\bR\\s*=\\s*[-+]?\\d*\\.?\\d+|\\bREPEAT\\b", QRegularExpression::CaseInsensitiveOption);
        m_pwlRepeat = reRepeat.match(v).hasMatch();
    } else {
        m_pwlRepeat = false;
    }

    if (v.contains("RSER=")) {
        QRegularExpression re("RSER=([-+]?[0-9]*\\.?[0-9]+[a-zA-Z]*)");
        auto match = re.match(v);
        if (match.hasMatch()) m_seriesResistance = match.captured(1);
    }
    if (v.contains("CPAR=")) {
        QRegularExpression re("CPAR=([-+]?[0-9]*\\.?[0-9]+[a-zA-Z]*)");
        auto match = re.match(v);
        if (match.hasMatch()) m_parallelCapacitance = match.captured(1);
    }

    rebuildPrimitives();
    updateLabelText();
    update();
}

void CurrentSourceItem::setSourceType(SourceType type) {
    m_sourceType = type;
    updateValue();
    rebuildPrimitives();
    update();
}

void CurrentSourceItem::updateValue() {
    QString tail;
    const bool allowParasitics = true;
    if (allowParasitics) {
        if (m_seriesResistance != "0" && !m_seriesResistance.isEmpty()) tail += " Rser=" + m_seriesResistance;
        if (m_parallelCapacitance != "0" && !m_parallelCapacitance.isEmpty()) tail += " Cpar=" + m_parallelCapacitance;
    }

    QString acStr;
    if (m_acAmplitude != "0" && !m_acAmplitude.isEmpty()) {
        acStr = " AC " + m_acAmplitude;
        if (m_acPhase != "0" && !m_acPhase.isEmpty()) acStr += " " + m_acPhase;
    }
    const QString repeatStr = m_pwlRepeat ? " r=0" : "";

    switch (m_sourceType) {
        case DC: {
            QString i = m_dcCurrent.trimmed();
            if (i.startsWith("{") && i.endsWith("}")) {
                m_value = QString("%1 %2 %3").arg(i).arg(acStr).arg(tail);
            } else {
                m_value = QString("%1 %2 %3").arg(i).arg(acStr).arg(tail);
            }
            break;
        }
        case Sine: m_value = QString("SINE(%1 %2 %3 %4 %5 %6 %7)%8%9")
                                .arg(m_sineOffset).arg(m_sineAmplitude).arg(m_sineFrequency)
                                .arg(m_sineDelay).arg(m_sineTheta).arg(m_sinePhi).arg(m_sineNcycles)
                                .arg(acStr).arg(tail); break;
        case Pulse: m_value = QString("PULSE(%1 %2 %3 %4 %5 %6 %7 %8)%9%10")
                                .arg(m_pulseI1).arg(m_pulseI2).arg(m_pulseDelay)
                                .arg(m_pulseRise).arg(m_pulseFall).arg(m_pulseWidth).arg(m_pulsePeriod).arg(m_pulseNcycles)
                                .arg(acStr).arg(tail); break;
        case EXP:   m_value = QString("EXP(%1 %2 %3 %4 %5 %6)%7%8")
                                .arg(m_expI1).arg(m_expI2).arg(m_expTd1).arg(m_expTau1).arg(m_expTd2).arg(m_expTau2)
                                .arg(acStr).arg(tail); break;
        case SFFM:  m_value = QString("SFFM(%1 %2 %3 %4 %5)%6%7")
                                .arg(m_sffmOff).arg(m_sffmAmplit).arg(m_sffmCarrier).arg(m_sffmModIndex).arg(m_sffmSignalFreq)
                                .arg(acStr).arg(tail); break;
        case PWL:   m_value = QString("PWL(%1)%2").arg(m_pwlPoints).arg(repeatStr); break;
        case PWLFile: m_value = QString("pwlfile=\"%1\" %2").arg(m_pwlFile).arg(repeatStr); break;
        case Behavioral: 
            if (!m_value.startsWith("I=")) m_value = "I=0";
            break;
        default: break;
    }
    updateLabelText();
}

void CurrentSourceItem::updateLabelText() {
    if (!m_refLabelItem || !m_valueLabelItem) {
        SchematicItem::updateLabelText();
        return;
    }

    QString refText = reference().isEmpty() ? (referencePrefix() + "?") : reference();
    m_refLabelItem->setText(refText);

    QString display = value();
    const int acIdx = display.indexOf(QRegularExpression("\\bAC\\b"));
    if (acIdx > 0) {
        const QString before = display.left(acIdx).trimmed();
        const QString after = display.mid(acIdx).trimmed();
        if (!before.isEmpty() && !after.isEmpty()) {
            display = before + "\n" + after;
        }
    }
    if ((m_sourceType == PWL || m_sourceType == PWLFile) && display.length() > 48) {
        display = display.left(45) + "...";
    } else if (display.length() > 64) {
        display = display.left(61) + "...";
    }
    m_valueLabelItem->setText(display);
}

QList<SymbolPrimitive> CurrentSourceItem::resolvedExternalPrimitives() const {
    QList<SymbolPrimitive> out;
    const QList<SymbolPrimitive> effective = m_externalSymbol.effectivePrimitives();
    out.reserve(effective.size());

    constexpr int kSchematicBodyStyle = 1;
    for (const auto& prim : effective) {
        if (prim.unit() != 0 && prim.unit() != unit()) continue;
        if (prim.bodyStyle() != 0 && prim.bodyStyle() != kSchematicBodyStyle) continue;
        out.append(prim);
    }
    return out;
}

void CurrentSourceItem::clearExternalSymbolItems() {
    for (auto* item : m_symbolItems) {
        if (item) {
            delete item;
        }
    }
    m_symbolItems.clear();
}

void CurrentSourceItem::rebuildExternalSymbol() {
    clearExternalSymbolItems();
    m_externalBounds = QRectF();

    if (!m_useExternalSymbol) return;

    const QList<SymbolPrimitive> primitives = resolvedExternalPrimitives();
    for (const auto& prim : primitives) {
        SymbolPrimitiveItem* visual = nullptr;
        switch (prim.type) {
            case SymbolPrimitive::Line:    visual = new SymbolLineItem(prim, this); break;
            case SymbolPrimitive::Rect:    visual = new SymbolRectItem(prim, this); break;
            case SymbolPrimitive::Circle:  visual = new SymbolCircleItem(prim, this); break;
            case SymbolPrimitive::Arc:     visual = new SymbolArcItem(prim, this); break;
            case SymbolPrimitive::Polygon: visual = new SymbolPolygonItem(prim, this); break;
            case SymbolPrimitive::Text: {
                auto* t = new SymbolTextItem(prim, this);
                t->setSymbolContext(name(), reference(), value());
                visual = t;
                break;
            }
            case SymbolPrimitive::Pin:     visual = new SymbolPinItem(prim, this); break;
            case SymbolPrimitive::Bezier:  visual = new SymbolBezierItem(prim, this); break;
            case SymbolPrimitive::Image:   visual = new SymbolImageItem(prim, this); break;
            default: break;
        }

        if (visual) {
            visual->setFlag(ItemIsSelectable, false);
            visual->setFlag(ItemIsMovable, false);
            m_symbolItems.append(visual);
        }
    }

    m_externalBounds = m_externalSymbol.boundingRect();
    update();
}

void CurrentSourceItem::rebuildPrimitives() {
    m_primitives.clear();
    m_useExternalSymbol = false;
    clearExternalSymbolItems();
    m_externalBounds = QRectF();

    SymbolDefinition* def = SymbolLibraryManager::instance().findSymbol(itemTypeName());
    if (!def) def = SymbolLibraryManager::instance().findSymbol("Current_Source_DC");
    if (!def) def = SymbolLibraryManager::instance().findSymbol("Current_Source");
    if (!def) def = SymbolLibraryManager::instance().findSymbol("current");
    if (def && def->isValid()) {
        m_externalSymbol = def->clone();
        m_useExternalSymbol = true;
        rebuildExternalSymbol();
        return;
    }
    
    m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(0, -45), QPointF(0, -22.5)));
    m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(0, 22.5), QPointF(0, 45)));
    
    m_primitives.push_back(std::make_unique<CirclePrimitive>(QPointF(0, 0), 22.5, false));
    
    m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(0, 38.75), QPointF(0, 52.8125)));
    m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(0, 61.25), QPointF(2.8125, 52.8125)));
    m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(2.8125, 52.8125), QPointF(-2.8125, 52.8125)));
    m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(-2.8125, 52.8125), QPointF(0, 61.25)));
    
    m_primitives.push_back(std::make_unique<CirclePrimitive>(QPointF(0, -45), 4, true));
    m_primitives.push_back(std::make_unique<CirclePrimitive>(QPointF(0, 45), 4, true));
}

QRectF CurrentSourceItem::boundingRect() const {
    if (m_useExternalSymbol) {
        QRectF rect = m_externalBounds;
        if (rect.isNull()) rect = m_externalSymbol.boundingRect();
        if (rect.isNull()) rect = QRectF(-20, -20, 40, 40);
        return rect.adjusted(-5, -5, 5, 5);
    }

    QRectF rect;
    for (const auto& prim : m_primitives) {
        rect = rect.united(prim->boundingRect());
    }
    return rect.adjusted(-5, -5, 5, 5);
}

void CurrentSourceItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(option)
    Q_UNUSED(widget)
    
    PCBTheme* theme = ThemeManager::theme();
    m_pen.setColor(theme ? theme->schematicLine() : Qt::white);
    
    if (!m_useExternalSymbol) {
        for (const auto& prim : m_primitives) {
            prim->paint(painter, m_pen, m_brush);
        }
    }
    
    drawConnectionPointHighlights(painter);
    
    if (isSelected()) {
        painter->setPen(QPen(theme->selectionBox(), 1, Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(boundingRect().adjusted(2, 2, -2, -2));
    }
}

QJsonObject CurrentSourceItem::toJson() const {
    QJsonObject json = SchematicItem::toJson();
    json["type"] = itemTypeName();
    json["sourceType"] = (int)m_sourceType;

    json["dcCurrent"] = m_dcCurrent;

    json["sineOffset"]    = m_sineOffset;
    json["sineAmplitude"] = m_sineAmplitude;
    json["sineFrequency"] = m_sineFrequency;
    json["sineDelay"]     = m_sineDelay;
    json["sineTheta"]     = m_sineTheta;
    json["sinePhi"]       = m_sinePhi;
    json["sineNcycles"]   = m_sineNcycles;

    json["pulseI1"]      = m_pulseI1;
    json["pulseI2"]      = m_pulseI2;
    json["pulseDelay"]   = m_pulseDelay;
    json["pulseRise"]    = m_pulseRise;
    json["pulseFall"]    = m_pulseFall;
    json["pulseWidth"]   = m_pulseWidth;
    json["pulsePeriod"]  = m_pulsePeriod;
    json["pulseNcycles"] = m_pulseNcycles;

    json["expI1"]   = m_expI1;
    json["expI2"]   = m_expI2;
    json["expTd1"]  = m_expTd1;
    json["expTau1"] = m_expTau1;
    json["expTd2"]  = m_expTd2;
    json["expTau2"] = m_expTau2;

    json["sffmOff"]        = m_sffmOff;
    json["sffmAmplit"]     = m_sffmAmplit;
    json["sffmCarrier"]    = m_sffmCarrier;
    json["sffmModIndex"]   = m_sffmModIndex;
    json["sffmSignalFreq"] = m_sffmSignalFreq;

    json["pwlPoints"] = m_pwlPoints;
    json["pwlFile"]   = m_pwlFile;
    json["pwlRepeat"] = m_pwlRepeat;

    json["acAmplitude"] = m_acAmplitude;
    json["acPhase"]     = m_acPhase;

    json["seriesResistance"]    = m_seriesResistance;
    json["parallelCapacitance"] = m_parallelCapacitance;

    json["showFunction"] = m_showFunction;
    json["showDc"]       = m_showDc;
    json["showAc"]       = m_showAc;
    json["showParasitic"] = m_showParasitic;

    return json;
}

bool CurrentSourceItem::fromJson(const QJsonObject& json) {
    if (!SchematicItem::fromJson(json)) return false;

    m_sourceType = static_cast<SourceType>(json["sourceType"].toInt(DC));

    m_dcCurrent = json["dcCurrent"].toString("1m");

    m_sineOffset    = json["sineOffset"].toString("0.0");
    m_sineAmplitude = json["sineAmplitude"].toString("1m");
    m_sineFrequency = json["sineFrequency"].toString("1000");
    m_sineDelay     = json["sineDelay"].toString("0");
    m_sineTheta     = json["sineTheta"].toString("0");
    m_sinePhi       = json["sinePhi"].toString("0");
    m_sineNcycles   = json["sineNcycles"].toString("0");

    m_pulseI1      = json["pulseI1"].toString("0.0");
    m_pulseI2      = json["pulseI2"].toString("1m");
    m_pulseDelay   = json["pulseDelay"].toString("0");
    m_pulseRise    = json["pulseRise"].toString("1u");
    m_pulseFall    = json["pulseFall"].toString("1u");
    m_pulseWidth   = json["pulseWidth"].toString("0.5m");
    m_pulsePeriod  = json["pulsePeriod"].toString("1m");
    m_pulseNcycles = json["pulseNcycles"].toString("0");

    m_expI1   = json["expI1"].toString("0.0");
    m_expI2   = json["expI2"].toString("1m");
    m_expTd1  = json["expTd1"].toString("0");
    m_expTau1 = json["expTau1"].toString("1m");
    m_expTd2  = json["expTd2"].toString("2m");
    m_expTau2 = json["expTau2"].toString("1m");

    m_sffmOff        = json["sffmOff"].toString("0.0");
    m_sffmAmplit     = json["sffmAmplit"].toString("1m");
    m_sffmCarrier    = json["sffmCarrier"].toString("1000");
    m_sffmModIndex   = json["sffmModIndex"].toString("1.0");
    m_sffmSignalFreq = json["sffmSignalFreq"].toString("100");

    m_pwlPoints = json["pwlPoints"].toString();
    m_pwlFile   = json["pwlFile"].toString();
    m_pwlRepeat = json["pwlRepeat"].toBool(false);

    m_acAmplitude = json["acAmplitude"].toString("");
    m_acPhase     = json["acPhase"].toString("");

    m_seriesResistance    = json["seriesResistance"].toString("0");
    m_parallelCapacitance = json["parallelCapacitance"].toString("0");

    m_showFunction = json["showFunction"].toBool(true);
    m_showDc       = json["showDc"].toBool(true);
    m_showAc       = json["showAc"].toBool(true);
    m_showParasitic = json["showParasitic"].toBool(true);

    updateValue();

    rebuildPrimitives();
    updateLabelText();
    update();
    return true;
}

void CurrentSourceItem::onInteractiveDoubleClick(const QPointF&) {
}

SchematicItem* CurrentSourceItem::clone() const {
    CurrentSourceItem* newItem = new CurrentSourceItem(pos(), m_value, m_sourceType, parentItem());
    newItem->fromJson(toJson());
    return newItem;
}

QList<QPointF> CurrentSourceItem::connectionPoints() const {
    if (m_useExternalSymbol) {
        QList<QPointF> pts = m_externalSymbol.connectionPoints();
        if (!pts.isEmpty()) return pts;
    }
    return { QPointF(0, -45), QPointF(0, 45) };
}
