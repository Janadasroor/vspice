#include "gerber_parser.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>
#include <cmath>

GerberLayer* GerberParser::parse(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "GerberParser: Could not open" << filePath;
        return nullptr;
    }

    GerberParser parser;
    GerberLayer* layer = new GerberLayer(filePath);
    
    QTextStream in(&file);
    parser.parseContent(in.readAll(), layer);

    qDebug() << "GerberParser: Parsed" << layer->primitives().size() << "primitives from" << filePath;
    return layer;
}

void GerberParser::parseContent(const QString& content, GerberLayer* layer) {
    // Check for Excellon
    if (content.startsWith("M48")) {
        m_state.isExcellon = true;
        // Default Excellon format is usually 2.4 trailing zeros, but headers specify
        m_state.unitFactor = 25.4; // Default to INCH until we see METRIC
        
        // Simple line-by-line for Excellon
        QStringList lines = content.split('\n');
        for (const QString& line : lines) {
            parseLine(line.trimmed(), layer);
        }
        return;
    }

    // Standard Gerber processing
    QString buffer;
    for (int i = 0; i < content.length(); ++i) {
        QChar c = content[i];
        if (c == '*') {
            parseLine(buffer.trimmed(), layer);
            buffer.clear();
        } else if (c == '%') {
            // Handle extended commands
            QString extended;
            i++;
            while (i < content.length() && content[i] != '%') {
                extended += content[i];
                i++;
            }
            
            if (extended.startsWith("MOIN")) m_state.unitFactor = 25.4;
            else if (extended.startsWith("MOMM")) m_state.unitFactor = 1.0;
            else if (extended.startsWith("FSLAX")) {
                m_state.formatInt = extended.mid(5, 1).toInt();
                m_state.formatDec = extended.mid(6, 1).toInt();
            } else if (extended.startsWith("AD")) {
                QRegularExpression re("ADD(\\d+)([CRPO]),?([\\d\\.]*)X?([\\d\\.]*)");
                QRegularExpressionMatch match = re.match(extended);
                if (match.hasMatch()) {
                    int id = match.captured(1).toInt();
                    QString type = match.captured(2);
                    GerberAperture ap;
                    if (type == "C") ap.type = GerberAperture::Circle;
                    else if (type == "R") ap.type = GerberAperture::Rectangle;
                    else if (type == "O") ap.type = GerberAperture::Obround;
                    else ap.type = GerberAperture::Circle;
                    
                    ap.params << match.captured(3).toDouble() * m_state.unitFactor;
                    if (!match.captured(4).isEmpty()) {
                        ap.params << match.captured(4).toDouble() * m_state.unitFactor;
                    }
                    layer->setAperture(id, ap);
                }
            }
        } else {
            buffer += c;
        }
    }
}

void GerberParser::parseLine(const QString& line, GerberLayer* layer) {
    if (line.isEmpty()) return;

    if (m_state.isExcellon) {
        // Excellon Parsing
        if (line.startsWith(";")) return; // Comment
        if (line == "METRIC") { m_state.unitFactor = 1.0; return; }
        if (line == "INCH") { m_state.unitFactor = 25.4; return; }
        
        // Tool Definition: T1C0.0276
        if (line.startsWith("T") && line.contains("C")) {
            QRegularExpression re("T(\\d+)C([\\d\\.]+)");
            QRegularExpressionMatch match = re.match(line);
            if (match.hasMatch()) {
                int toolId = match.captured(1).toInt();
                double size = match.captured(2).toDouble();
                // Store as aperture (mapping T code to Aperture ID 100+)
                int apId = 100 + toolId; 
                m_state.toolSizes[toolId] = size;
                
                GerberAperture ap;
                ap.type = GerberAperture::Circle;
                ap.params << size * m_state.unitFactor; // Scale immediately
                layer->setAperture(apId, ap);
            }
            return;
        }

        // Tool Change: T1
        if (line.startsWith("T") && !line.contains("C")) {
            int toolId = line.mid(1).toInt();
            m_state.currentAperture = 100 + toolId;
            return;
        }

        // Coordinates: X...Y...
        if (line.startsWith("X") || line.startsWith("Y")) {
            QRegularExpression re("X([+-]?[\\d\\.]+)Y([+-]?[\\d\\.]+)");
            QRegularExpressionMatch match = re.match(line);
            if (match.hasMatch()) {
                // Excellon decimal format in file usually explicit (e.g. 4.5441)
                double x = match.captured(1).toDouble() * m_state.unitFactor;
                double y = match.captured(2).toDouble() * m_state.unitFactor;
                
                GerberPrimitive prim;
                prim.type = GerberPrimitive::Flash;
                prim.apertureId = m_state.currentAperture;
                prim.center = QPointF(x, y);
                layer->addPrimitive(prim);
            }
        }
        return;
    }

    // Standard Gerber Parsing (D-codes etc)
    if (line.startsWith("D") && line.length() > 2) {
        int d = line.mid(1).toInt();
        if (d >= 10) m_state.currentAperture = d;
        return;
    }

    if (line.startsWith("G01")) m_state.interpolationLinear = true;
    if (line.startsWith("G02") || line.startsWith("G03")) m_state.interpolationLinear = false;

    QRegularExpression re("X?([+-]?\\d+)?Y?([+-]?\\d+)?(D0[123]|D\\d+)?");
    QRegularExpressionMatch match = re.match(line);
    
    if (match.hasMatch()) {
        double newX = m_state.currentPos.x();
        double newY = m_state.currentPos.y();
        
        QString xVal = match.captured(1);
        QString yVal = match.captured(2);
        QString dCode = match.captured(3);

        if (!xVal.isEmpty()) newX = parseCoordinate(xVal) * m_state.unitFactor;
        if (!yVal.isEmpty()) newY = parseCoordinate(yVal) * m_state.unitFactor;
        
        int dNum = 0;
        if (dCode.startsWith("D0")) {
            dNum = dCode.mid(2, 1).toInt();
            m_state.lastDCode = dNum;
        } else if (dCode.startsWith("D")) {
            m_state.currentAperture = dCode.mid(1).toInt();
            dNum = m_state.lastDCode;
        } else {
            dNum = m_state.lastDCode;
        }

        QPointF nextPoint(newX, newY);

        if (dNum == 1) { // Line
            GerberPrimitive prim;
            prim.type = GerberPrimitive::Line;
            prim.apertureId = m_state.currentAperture;
            QPainterPath path;
            path.moveTo(m_state.currentPos);
            path.lineTo(nextPoint);
            prim.path = path;
            layer->addPrimitive(prim);
        } else if (dNum == 3) { // Flash
            GerberPrimitive prim;
            prim.type = GerberPrimitive::Flash;
            prim.apertureId = m_state.currentAperture;
            prim.center = nextPoint;
            layer->addPrimitive(prim);
        }
        
        m_state.currentPos = nextPoint;
    }
}

double GerberParser::parseCoordinate(const QString& val) {
    if (val.isEmpty()) return 0;
    bool ok;
    double raw = val.toDouble(&ok);
    if (!ok) return 0;
    return raw / std::pow(10, m_state.formatDec);
}
