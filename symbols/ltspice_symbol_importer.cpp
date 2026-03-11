#include "ltspice_symbol_importer.h"
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDebug>
#include <QSet>
#include <QLineF>
#include <cmath>

using Flux::Model::SymbolDefinition;
using Flux::Model::SymbolPrimitive;

namespace {
QString mapLtspicePinJustification(const QString& rawJustification) {
    const QString j = rawJustification.trimmed().toUpper();
    // Flux orientation means "lead extends from connection point toward symbol body".
    if (j == "LEFT" || j == "VLEFT") return "Right";
    if (j == "RIGHT" || j == "VRIGHT") return "Left";
    if (j == "TOP" || j == "VTOP") return "Down";
    if (j == "BOTTOM" || j == "VBOTTOM") return "Up";
    return QString();
}
}

SymbolDefinition LtspiceSymbolImporter::importSymbol(const QString& filePath) {
    return importSymbolDetailed(filePath).symbol;
}

LtspiceSymbolImporter::ImportResult LtspiceSymbolImporter::importSymbolDetailed(const QString& filePath) {
    ImportResult result;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.errorMessage = "Could not open file: " + filePath;
        return result;
    }

    SymbolDefinition symbol;
    QString fileName = QFileInfo(filePath).baseName();
    symbol.setName(fileName);

    struct RawPin {
        QPointF pos;
        int number = 0;
        QString name;
        QString orientation;
    };
    QList<RawPin> rawPins;
    
    struct RawLine {
        QPointF p1, p2;
    };
    QList<RawLine> rawLines;

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        QStringList parts = line.split(" ", Qt::SkipEmptyParts);
        if (parts.isEmpty()) continue;

        QString cmd = parts[0].toUpper();

        if (cmd == "LINE") {
            if (parts.size() >= 6) {
                rawLines.append({ parsePoint(parts[2], parts[3], false), parsePoint(parts[4], parts[5], false) });
            }
        } else if (cmd == "RECTANGLE") {
            if (parts.size() >= 6) {
                QPointF p1 = parsePoint(parts[2], parts[3]);
                QPointF p2 = parsePoint(parts[4], parts[5]);
                symbol.addPrimitive(SymbolPrimitive::createRect(QRectF(p1, p2).normalized()));
            }
        } else if (cmd == "CIRCLE") {
            if (parts.size() >= 6) {
                QPointF p1 = parsePoint(parts[2], parts[3]);
                QPointF p2 = parsePoint(parts[4], parts[5]);
                QRectF rect = QRectF(p1, p2).normalized();
                symbol.addPrimitive(SymbolPrimitive::createCircle(rect.center(), rect.width() / 2.0));
            }
        } else if (cmd == "ARC") {
            if (parts.size() >= 10) {
                QPointF p1 = parsePoint(parts[2], parts[3]);
                QPointF p2 = parsePoint(parts[4], parts[5]);
                QPointF p3 = parsePoint(parts[6], parts[7]);
                QPointF p4 = parsePoint(parts[8], parts[9]);
                QRectF rect = QRectF(p1, p2).normalized();
                QPointF center = rect.center();
                qreal angleStart = std::atan2(-(p3.y() - center.y()), p3.x() - center.x()) * 180.0 / M_PI;
                qreal angleEnd = std::atan2(-(p4.y() - center.y()), p4.x() - center.x()) * 180.0 / M_PI;
                qreal span = angleEnd - angleStart;
                if (span < 0) span += 360.0;
                symbol.addPrimitive(SymbolPrimitive::createArc(rect, qRound(angleStart * 16), qRound(span * 16)));
            }
        } else if (cmd == "PIN") {
            if (parts.size() >= 3) {
                RawPin pin;
                pin.pos = parsePoint(parts[1], parts[2], false);
                pin.number = rawPins.size() + 1;
                if (parts.size() >= 4) {
                    pin.orientation = mapLtspicePinJustification(parts[3]);
                }
                rawPins.append(pin);
            }
        } else if (cmd == "PINATTR") {
            if (!rawPins.isEmpty() && parts.size() >= 3) {
                if (parts[1].toUpper() == "PINNAME") rawPins.last().name = parts.mid(2).join(" ");
                else if (parts[1].toUpper() == "SPICEORDER") rawPins.last().number = parts[2].toInt();
            }
        } else if (cmd == "SYMATTR") {
            if (parts.size() >= 3) {
                QString key = parts[1].toUpper();
                QString val = parts.mid(2).join(" ");
                if (key == "PREFIX") symbol.setReferencePrefix(val);
                else if (key == "DESCRIPTION") symbol.setDescription(val);
                else if (key == "VALUE") symbol.setDefaultValue(val);
            }
        } else if (cmd == "WINDOW") {
            if (parts.size() >= 4) {
                int id = parts[1].toInt();
                QPointF p = parsePoint(parts[2], parts[3]);
                if (id == 0) symbol.setReferencePos(p);
                else if (id == 3) symbol.setNamePos(p);
            }
        }
    }

    // Process Pins and detect orientation/stubs
    QSet<int> usedLineIndices;
    for (const auto& rawPin : rawPins) {
        QString orient = rawPin.orientation.isEmpty() ? QString("Right") : rawPin.orientation;
        qreal len = 0.0;
        bool foundStub = false;

        for (int i = 0; i < rawLines.size(); ++i) {
            if (usedLineIndices.contains(i)) continue;
            const auto& line = rawLines[i];
            QPointF pOther;
            bool connected = false;
            if (QLineF(line.p1, rawPin.pos).length() < 0.1) { connected = true; pOther = line.p2; }
            else if (QLineF(line.p2, rawPin.pos).length() < 0.1) { connected = true; pOther = line.p1; }

            if (connected) {
                const qreal dx = pOther.x() - rawPin.pos.x();
                const qreal dy = pOther.y() - rawPin.pos.y();
                const bool axisAligned = (qAbs(dx) < 0.1) || (qAbs(dy) < 0.1);
                const qreal rawLen = QLineF(rawPin.pos, pOther).length();

                // Only consume explicit axis-aligned pin-stub segments.
                // Allow moderate/long stubs (many LTspice symbols use ~29 units).
                if (!axisAligned || rawLen > 40.0) {
                    continue;
                }

                bool hitsAnotherPin = false;
                for (const auto& otherPin : rawPins) {
                    if (&otherPin == &rawPin) continue;
                    if (QLineF(otherPin.pos, pOther).length() < 0.1) {
                        hitsAnotherPin = true;
                        break;
                    }
                }
                if (hitsAnotherPin) continue;

                QString stubOrient;
                if (qAbs(dx) >= qAbs(dy)) stubOrient = (dx > 0.0) ? "Right" : "Left";
                else stubOrient = (dy > 0.0) ? "Down" : "Up";

                if (rawPin.orientation.isEmpty() || rawPin.orientation == stubOrient) {
                    orient = stubOrient;
                    len = rawLen * 1.25;
                    usedLineIndices.insert(i);
                    foundStub = true;
                    break;
                }
            }
        }
        
        QPointF finalPos = parsePoint(QString::number(rawPin.pos.x()), QString::number(rawPin.pos.y()), true);
        symbol.addPrimitive(SymbolPrimitive::createPin(finalPos, rawPin.number, rawPin.name, orient, len));
    }

    // Add remaining lines that weren't pin stubs
    for (int i = 0; i < rawLines.size(); ++i) {
        if (!usedLineIndices.contains(i)) {
            symbol.addPrimitive(SymbolPrimitive::createLine(scale(rawLines[i].p1), scale(rawLines[i].p2)));
        }
    }

    result.symbol = symbol;
    result.success = true;
    return result;
}

QPointF LtspiceSymbolImporter::parsePoint(const QString& x, const QString& y) {
    return parsePoint(x, y, true);
}

QPointF LtspiceSymbolImporter::parsePoint(const QString& x, const QString& y, bool applyScale) {
    qreal dx = x.toDouble();
    qreal dy = y.toDouble();
    if (applyScale) {
        // 1.25 scale converts 16-unit grid to 20-unit grid (aligned with 10-unit system)
        dx = std::round(dx * 1.25 / 10.0) * 10.0;
        dy = std::round(dy * 1.25 / 10.0) * 10.0;
    }
    return QPointF(dx, dy);
}

qreal LtspiceSymbolImporter::scale(qreal val) {
    return val * 1.25;
}

QPointF LtspiceSymbolImporter::scale(QPointF p) {
    return QPointF(p.x() * 1.25, p.y() * 1.25);
}
