#ifndef GERBER_PARSER_H
#define GERBER_PARSER_H

#include "gerber_layer.h"
#include <QString>

/**
 * @brief Parses RS-274X Gerber files into GerberLayer objects
 */
class GerberParser {
public:
    static GerberLayer* parse(const QString& filePath);

private:
    struct State {
        QPointF currentPos;
        int currentAperture;
        int lastDCode; // 1 for D01, 2 for D02, 3 for D03
        bool interpolationLinear;
        double unitFactor; // 1.0 for mm, 25.4 for inches
        int formatInt;
        int formatDec;
        
        // Drill specific
        bool isExcellon;
        QMap<int, double> toolSizes; // Tool code (T1) -> Diameter
        
        State() : currentPos(0,0), currentAperture(-1), lastDCode(2), 
                  interpolationLinear(true), unitFactor(1.0), formatInt(2), formatDec(4),
                  isExcellon(false) {}
    };

    void parseContent(const QString& content, GerberLayer* layer);
    void parseLine(const QString& line, GerberLayer* layer);
    double parseCoordinate(const QString& val);

    State m_state;
};

#endif // GERBER_PARSER_H