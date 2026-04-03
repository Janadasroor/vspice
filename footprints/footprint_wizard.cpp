#include "footprint_wizard.h"
#include <QtMath>

using namespace Flux::Model;

// ============================================================================
// Layer helpers
// ============================================================================

Flux::Model::FootprintPrimitive::Layer FootprintWizard::topCopper() { return FootprintPrimitive::Top_Copper; }
Flux::Model::FootprintPrimitive::Layer FootprintWizard::bottomCopper() { return FootprintPrimitive::Bottom_Copper; }
Flux::Model::FootprintPrimitive::Layer FootprintWizard::topSilkscreen() { return FootprintPrimitive::Top_Silkscreen; }
Flux::Model::FootprintPrimitive::Layer FootprintWizard::topCourtyard() { return FootprintPrimitive::Top_Courtyard; }
Flux::Model::FootprintPrimitive::Layer FootprintWizard::topFabrication() { return FootprintPrimitive::Top_Fabrication; }

// ============================================================================
// Helper utilities
// ============================================================================

void FootprintWizard::addSilkscreenOutline(Flux::Model::FootprintDefinition& fp, QRectF rect, double lineWidth) {
    // Top line
    FootprintPrimitive t = Flux::Model::FootprintPrimitive::createLine(
        QPointF(rect.left(), rect.top()), QPointF(rect.right(), rect.top()), lineWidth);
    t.layer = topSilkscreen();
    fp.addPrimitive(t);

    // Right line
    FootprintPrimitive r = Flux::Model::FootprintPrimitive::createLine(
        QPointF(rect.right(), rect.top()), QPointF(rect.right(), rect.bottom()), lineWidth);
    r.layer = topSilkscreen();
    fp.addPrimitive(r);

    // Bottom line
    FootprintPrimitive b = Flux::Model::FootprintPrimitive::createLine(
        QPointF(rect.right(), rect.bottom()), QPointF(rect.left(), rect.bottom()), lineWidth);
    b.layer = topSilkscreen();
    fp.addPrimitive(b);

    // Left line (with gap for pin 1)
    double gapSize = 0.6;
    double gapCenter = rect.top() + rect.height() * 0.3;
    FootprintPrimitive l1 = Flux::Model::FootprintPrimitive::createLine(
        QPointF(rect.left(), rect.bottom()), QPointF(rect.left(), gapCenter + gapSize / 2), lineWidth);
    l1.layer = topSilkscreen();
    fp.addPrimitive(l1);

    FootprintPrimitive l2 = Flux::Model::FootprintPrimitive::createLine(
        QPointF(rect.left(), gapCenter - gapSize / 2), QPointF(rect.left(), rect.top()), lineWidth);
    l2.layer = topSilkscreen();
    fp.addPrimitive(l2);
}

void FootprintWizard::addCourtyard(Flux::Model::FootprintDefinition& fp, QRectF rect, double lineWidth) {
    FootprintPrimitive t = Flux::Model::FootprintPrimitive::createLine(
        QPointF(rect.left(), rect.top()), QPointF(rect.right(), rect.top()), lineWidth);
    t.layer = topCourtyard();
    fp.addPrimitive(t);

    FootprintPrimitive r = Flux::Model::FootprintPrimitive::createLine(
        QPointF(rect.right(), rect.top()), QPointF(rect.right(), rect.bottom()), lineWidth);
    r.layer = topCourtyard();
    fp.addPrimitive(r);

    FootprintPrimitive b = Flux::Model::FootprintPrimitive::createLine(
        QPointF(rect.right(), rect.bottom()), QPointF(rect.left(), rect.bottom()), lineWidth);
    b.layer = topCourtyard();
    fp.addPrimitive(b);

    FootprintPrimitive l = Flux::Model::FootprintPrimitive::createLine(
        QPointF(rect.left(), rect.bottom()), QPointF(rect.left(), rect.top()), lineWidth);
    l.layer = topCourtyard();
    fp.addPrimitive(l);
}

void FootprintWizard::addReferenceText(Flux::Model::FootprintDefinition& fp, const QString& refDes, QPointF pos, double height) {
    FootprintPrimitive txt = Flux::Model::FootprintPrimitive::createText(refDes, pos, height);
    txt.layer = topSilkscreen();
    fp.addPrimitive(txt);
}

void FootprintWizard::addValueText(Flux::Model::FootprintDefinition& fp, const QString& value, QPointF pos, double height) {
    FootprintPrimitive txt = Flux::Model::FootprintPrimitive::createText(value, pos, height);
    txt.layer = topSilkscreen();
    fp.addPrimitive(txt);
}

void FootprintWizard::addPin1Marker(Flux::Model::FootprintDefinition& fp, QPointF pos, double size) {
    // Small circle to indicate pin 1
    FootprintPrimitive circle = Flux::Model::FootprintPrimitive::createCircle(pos, size, false, 0.1);
    circle.layer = topSilkscreen();
    fp.addPrimitive(circle);
}

// ============================================================================
// DIP (Dual Inline Package)
// ============================================================================

Flux::Model::FootprintDefinition FootprintWizard::generateDIP(const WizardParams& p) {
    FootprintDefinition fp(QString("DIP-%1").arg(p.pinCount));
    fp.setDescription(QString("Dual Inline Package, %1 pins, %2mm pitch").arg(p.pinCount).arg(p.pitch));
    fp.setCategory("IC");
    fp.setClassification("Through-Hole");

    int halfPins = p.pinCount / 2;
    double rowSpan = p.rowSpan > 0 ? p.rowSpan : 7.62; // Standard DIP row span

    // Generate pads
    for (int i = 0; i < halfPins; ++i) {
        double y = (i - (halfPins - 1) / 2.0) * p.pitch;

        // Left row (pins 1..N/2)
        FootprintPrimitive pad = Flux::Model::FootprintPrimitive::createPad(
            QPointF(-rowSpan / 2, -y), QString::number(i + 1), "Round",
            QSizeF(p.padWidth, p.padWidth));
        pad.layer = topCopper();
        pad.data["drill_size"] = p.drillSize;
        pad.data["pad_type"] = "Through-Hole";
        fp.addPrimitive(pad);

        // Right row (pins N/2+1..N)
        FootprintPrimitive pad2 = Flux::Model::FootprintPrimitive::createPad(
            QPointF(rowSpan / 2, -y), QString::number(halfPins + i + 1), "Round",
            QSizeF(p.padWidth, p.padWidth));
        pad2.layer = topCopper();
        pad2.data["drill_size"] = p.drillSize;
        pad2.data["pad_type"] = "Through-Hole";
        fp.addPrimitive(pad2);
    }

    // Body outline
    double bodyHalfH = (halfPins - 1) * p.pitch / 2.0 + 1.5;
    addSilkscreenOutline(fp, QRectF(-p.bodyWidth / 2, -bodyHalfH, p.bodyWidth, bodyHalfH * 2));

    // Pin 1 marker
    double pin1Y = (halfPins - 1) * p.pitch / 2.0;
    addPin1Marker(fp, QPointF(-rowSpan / 2 - 0.8, pin1Y), 0.4);

    // Courtyard
    double cyExtra = p.courtyardExtra;
    addCourtyard(fp, QRectF(-rowSpan / 2 - p.padWidth / 2 - cyExtra, -bodyHalfH - cyExtra,
                             rowSpan + p.padWidth + cyExtra * 2, bodyHalfH * 2 + cyExtra * 2));

    // Reference designator
    addReferenceText(fp, "U?", QPointF(0, bodyHalfH + 1.5), 1.0);
    addValueText(fp, "Value", QPointF(0, -bodyHalfH - 1.0), 1.0);

    return fp;
}

// ============================================================================
// SOIC / SOP (Small Outline IC)
// ============================================================================

Flux::Model::FootprintDefinition FootprintWizard::generateSOIC(const WizardParams& p) {
    FootprintDefinition fp(QString("SOIC-%1").arg(p.pinCount));
    fp.setDescription(QString("Small Outline IC, %1 pins, %2mm pitch").arg(p.pinCount).arg(p.pitch));
    fp.setCategory("IC");
    fp.setClassification("SMD");

    int halfPins = p.pinCount / 2;
    double rowSpan = p.rowSpan > 0 ? p.rowSpan : 5.3; // Standard SOIC-8 row span

    // Generate pads (gull-wing style)
    for (int i = 0; i < halfPins; ++i) {
        double y = (i - (halfPins - 1) / 2.0) * p.pitch;

        // Left row
        FootprintPrimitive pad = Flux::Model::FootprintPrimitive::createPad(
            QPointF(-rowSpan / 2, -y), QString::number(i + 1), "Rect",
            QSizeF(p.padWidth, p.padHeight));
        pad.layer = topCopper();
        fp.addPrimitive(pad);

        // Right row
        FootprintPrimitive pad2 = Flux::Model::FootprintPrimitive::createPad(
            QPointF(rowSpan / 2, -y), QString::number(halfPins + i + 1), "Rect",
            QSizeF(p.padWidth, p.padHeight));
        pad2.layer = topCopper();
        fp.addPrimitive(pad2);
    }

    // Body outline
    double bodyHalfH = (halfPins - 1) * p.pitch / 2.0 + 0.5;
    addSilkscreenOutline(fp, QRectF(-p.bodyWidth / 2, -bodyHalfH, p.bodyWidth, bodyHalfH * 2));

    // Pin 1 marker
    double pin1Y = (halfPins - 1) * p.pitch / 2.0;
    addPin1Marker(fp, QPointF(-p.bodyWidth / 2 - 0.5, pin1Y), 0.3);

    // Courtyard
    double cyExtra = p.courtyardExtra;
    addCourtyard(fp, QRectF(-rowSpan / 2 - p.padWidth - cyExtra, -bodyHalfH - cyExtra,
                             rowSpan + p.padWidth * 2 + cyExtra * 2, bodyHalfH * 2 + cyExtra * 2));

    addReferenceText(fp, "U?", QPointF(0, bodyHalfH + 1.5), 1.0);
    addValueText(fp, "Value", QPointF(0, -bodyHalfH - 1.0), 1.0);

    return fp;
}

// ============================================================================
// QFP (Quad Flat Package)
// ============================================================================

Flux::Model::FootprintDefinition FootprintWizard::generateQFP(const WizardParams& p) {
    FootprintDefinition fp(QString("QFP-%1").arg(p.pinCount));
    fp.setDescription(QString("Quad Flat Package, %1 pins, %2mm pitch").arg(p.pinCount).arg(p.pitch));
    fp.setCategory("IC");
    fp.setClassification("SMD");

    int pinsPerSide = p.pinCount / 4;
    double rowSpan = p.rowSpan > 0 ? p.rowSpan : p.bodyWidth + 2 * p.padWidth + 0.5;

    // Generate pads on all 4 sides
    int pinNum = 1;

    for (int side = 0; side < 4; ++side) {
        for (int i = 0; i < pinsPerSide; ++i) {
            double pos = (i - (pinsPerSide - 1) / 2.0) * p.pitch;
            QPointF padPos;

            if (side == 0) { // Top: left to right
                padPos = QPointF(pos, -rowSpan / 2);
            } else if (side == 1) { // Right: top to bottom
                padPos = QPointF(rowSpan / 2, pos);
            } else if (side == 2) { // Bottom: right to left
                padPos = QPointF(-pos, rowSpan / 2);
            } else { // Left: bottom to top
                padPos = QPointF(-rowSpan / 2, -pos);
            }

            FootprintPrimitive pad = Flux::Model::FootprintPrimitive::createPad(
                padPos, QString::number(pinNum++), "Rect",
                QSizeF(p.padWidth, p.padHeight));
            pad.layer = topCopper();
            fp.addPrimitive(pad);
        }
    }

    // Body outline
    addSilkscreenOutline(fp, QRectF(-p.bodyWidth / 2, -p.bodyHeight / 2, p.bodyWidth, p.bodyHeight));

    // Pin 1 marker
    double pin1X = -(pinsPerSide - 1) * p.pitch / 2.0;
    addPin1Marker(fp, QPointF(pin1X, -rowSpan / 2 - 0.8), 0.3);

    // Courtyard
    double cyExtra = p.courtyardExtra;
    addCourtyard(fp, QRectF(-rowSpan / 2 - p.padWidth - cyExtra, -rowSpan / 2 - p.padHeight - cyExtra,
                             rowSpan + p.padWidth * 2 + cyExtra * 2, rowSpan + p.padHeight * 2 + cyExtra * 2));

    addReferenceText(fp, "U?", QPointF(0, p.bodyHeight / 2 + 1.5), 1.0);

    return fp;
}

// ============================================================================
// QFN (Quad Flat No-lead)
// ============================================================================

Flux::Model::FootprintDefinition FootprintWizard::generateQFN(const WizardParams& p) {
    FootprintDefinition fp(QString("QFN-%1").arg(p.pinCount));
    fp.setDescription(QString("Quad Flat No-lead, %1 pins, %2mm pitch").arg(p.pinCount).arg(p.pitch));
    fp.setCategory("IC");
    fp.setClassification("SMD");

    int pinsPerSide = p.pinCount / 4;
    double rowSpan = p.rowSpan > 0 ? p.rowSpan : p.bodyWidth + 0.5;

    // Generate pads on all 4 sides (L-shaped, modeled as small rects)
    int pinNum = 1;

    for (int side = 0; side < 4; ++side) {
        for (int i = 0; i < pinsPerSide; ++i) {
            double pos = (i - (pinsPerSide - 1) / 2.0) * p.pitch;
            QPointF padPos;

            if (side == 0) padPos = QPointF(pos, -rowSpan / 2);
            else if (side == 1) padPos = QPointF(rowSpan / 2, pos);
            else if (side == 2) padPos = QPointF(-pos, rowSpan / 2);
            else padPos = QPointF(-rowSpan / 2, -pos);

            FootprintPrimitive pad = Flux::Model::FootprintPrimitive::createPad(
                padPos, QString::number(pinNum++), "Rect",
                QSizeF(p.padWidth, p.padHeight));
            pad.layer = topCopper();
            fp.addPrimitive(pad);
        }
    }

    // Thermal pad (center)
    if (p.padWidth > 0) {
        double thermalSize = p.bodyWidth * 0.6;
        FootprintPrimitive thermal = Flux::Model::FootprintPrimitive::createPad(
            QPointF(0, 0), "EP", "Rect", QSizeF(thermalSize, thermalSize));
        thermal.layer = topCopper();
        fp.addPrimitive(thermal);
    }

    // Body outline
    addSilkscreenOutline(fp, QRectF(-p.bodyWidth / 2, -p.bodyHeight / 2, p.bodyWidth, p.bodyHeight));

    // Courtyard
    double cyExtra = p.courtyardExtra;
    addCourtyard(fp, QRectF(-rowSpan / 2 - p.padWidth - cyExtra, -rowSpan / 2 - p.padHeight - cyExtra,
                             rowSpan + p.padWidth * 2 + cyExtra * 2, rowSpan + p.padHeight * 2 + cyExtra * 2));

    addReferenceText(fp, "U?", QPointF(0, p.bodyHeight / 2 + 1.2), 1.0);

    return fp;
}

// ============================================================================
// BGA (Ball Grid Array)
// ============================================================================

Flux::Model::FootprintDefinition FootprintWizard::generateBGA(const WizardParams& p) {
    // Calculate grid dimensions
    int gridSize = qCeil(qSqrt(p.pinCount));
    FootprintDefinition fp(QString("BGA-%1").arg(p.pinCount));
    fp.setDescription(QString("Ball Grid Array, %1 balls, %2mm pitch").arg(p.pinCount).arg(p.pitch));
    fp.setCategory("IC");
    fp.setClassification("SMD");

    // Generate ball pads in a grid
    int pinNum = 1;
    int row = 0, col = 0;
    char colChar = 'A';

    for (int r = 0; r < gridSize; ++r) {
        for (int c = 0; c < gridSize; ++c) {
            if (pinNum > p.pinCount) break;

            double x = (c - (gridSize - 1) / 2.0) * p.pitch;
            double y = (r - (gridSize - 1) / 2.0) * p.pitch;

            QString padName = QString("%1%2").arg(colChar + c).arg(r + 1);

            FootprintPrimitive pad = Flux::Model::FootprintPrimitive::createPad(
                QPointF(x, y), padName, "Round",
                QSizeF(p.padWidth, p.padWidth));
            pad.layer = topCopper();
            pad.data["drill_size"] = p.drillSize;
            pad.data["pad_type"] = "Through-Hole";
            fp.addPrimitive(pad);

            pinNum++;
        }
    }

    // Body outline
    double bodySize = (gridSize - 1) * p.pitch + 1.0;
    addSilkscreenOutline(fp, QRectF(-bodySize / 2, -bodySize / 2, bodySize, bodySize));

    // Pin 1 marker (A1 corner)
    double a1X = -(gridSize - 1) * p.pitch / 2.0;
    double a1Y = -(gridSize - 1) * p.pitch / 2.0;
    addPin1Marker(fp, QPointF(a1X - 0.5, a1Y - 0.5), 0.4);

    // Courtyard
    double cyExtra = p.courtyardExtra;
    addCourtyard(fp, QRectF(-bodySize / 2 - cyExtra, -bodySize / 2 - cyExtra,
                             bodySize + cyExtra * 2, bodySize + cyExtra * 2));

    addReferenceText(fp, "U?", QPointF(0, bodySize / 2 + 1.5), 1.0);

    return fp;
}

// ============================================================================
// SOT (Small Outline Transistor)
// ============================================================================

Flux::Model::FootprintDefinition FootprintWizard::generateSOT(const WizardParams& p) {
    QString name = p.packageType.contains("SOT") ? p.packageType : "SOT-23";
    FootprintDefinition fp(name);
    fp.setDescription(QString("%1 transistor package").arg(name));
    fp.setCategory("Transistors");
    fp.setClassification("SMD");

    int pinCount = p.pinCount > 0 ? p.pinCount : 3;
    double bodyW = p.bodyWidth > 0 ? p.bodyWidth : 2.9;
    double bodyH = p.bodyHeight > 0 ? p.bodyHeight : 1.3;

    if (name == "SOT-23") {
        // 3 pads: left, right-top, right-bottom
        FootprintPrimitive pad1 = Flux::Model::FootprintPrimitive::createPad(
            QPointF(-1.5, 0), "1", "Rect", QSizeF(0.8, 0.7));
        pad1.layer = topCopper();
        fp.addPrimitive(pad1);

        FootprintPrimitive pad2 = Flux::Model::FootprintPrimitive::createPad(
            QPointF(1.5, 0.95), "2", "Rect", QSizeF(0.8, 0.7));
        pad2.layer = topCopper();
        fp.addPrimitive(pad2);

        FootprintPrimitive pad3 = Flux::Model::FootprintPrimitive::createPad(
            QPointF(1.5, -0.95), "3", "Rect", QSizeF(0.8, 0.7));
        pad3.layer = topCopper();
        fp.addPrimitive(pad3);

        addSilkscreenOutline(fp, QRectF(-bodyW / 2, -bodyH / 2, bodyW, bodyH));
        addReferenceText(fp, "Q?", QPointF(0, bodyH / 2 + 1.2), 1.0);
    } else if (name == "SOT-223") {
        // 4 pads: 3 signal + 1 thermal
        FootprintPrimitive pad1 = Flux::Model::FootprintPrimitive::createPad(
            QPointF(-1.5, 1.5), "1", "Rect", QSizeF(0.9, 1.2));
        pad1.layer = topCopper();
        fp.addPrimitive(pad1);

        FootprintPrimitive pad2 = Flux::Model::FootprintPrimitive::createPad(
            QPointF(-1.5, 0), "2", "Rect", QSizeF(0.9, 1.2));
        pad2.layer = topCopper();
        fp.addPrimitive(pad2);

        FootprintPrimitive pad3 = Flux::Model::FootprintPrimitive::createPad(
            QPointF(-1.5, -1.5), "3", "Rect", QSizeF(0.9, 1.2));
        pad3.layer = topCopper();
        fp.addPrimitive(pad3);

        // Thermal pad
        FootprintPrimitive thermal = Flux::Model::FootprintPrimitive::createPad(
            QPointF(1.8, 0), "4", "Rect", QSizeF(3.5, 6.5));
        thermal.layer = topCopper();
        fp.addPrimitive(thermal);

        addSilkscreenOutline(fp, QRectF(-bodyW / 2, -bodyH / 2, bodyW, bodyH));
        addReferenceText(fp, "Q?", QPointF(0, bodyH / 2 + 1.2), 1.0);
    } else {
        // Generic: evenly spaced pads
        for (int i = 0; i < pinCount; ++i) {
            double y = (i - (pinCount - 1) / 2.0) * p.pitch;
            FootprintPrimitive pad = Flux::Model::FootprintPrimitive::createPad(
                QPointF(p.rowSpan / 2, -y), QString::number(i + 1), "Rect",
                QSizeF(p.padWidth, p.padHeight));
            pad.layer = topCopper();
            fp.addPrimitive(pad);
        }
        addSilkscreenOutline(fp, QRectF(-bodyW / 2, -bodyH / 2, bodyW, bodyH));
    }

    return fp;
}

// ============================================================================
// Passive SMD (0201, 0402, 0603, 0805, 1206, 1812, 2512)
// ============================================================================

Flux::Model::FootprintDefinition FootprintWizard::generatePassiveSMD(const WizardParams& p) {
    QString name = p.packageType;
    if (!name.contains(QRegularExpression("^[0-9]"))) {
        name = "0603"; // Default
    }

    FootprintDefinition fp(name);
    fp.setDescription(QString("SMD passive %1").arg(name));
    fp.setCategory("Passives");
    fp.setClassification("SMD");

    // Standard dimensions for common packages
    double len = 1.6, wid = 0.8, padW = 0.8, padH = 0.9, gap = 0.6;

    if (name == "0201") { len = 0.6; wid = 0.3; padW = 0.3; padH = 0.35; gap = 0.2; }
    else if (name == "0402") { len = 1.0; wid = 0.5; padW = 0.6; padH = 0.6; gap = 0.4; }
    else if (name == "0603") { len = 1.6; wid = 0.8; padW = 0.8; padH = 0.9; gap = 0.6; }
    else if (name == "0805") { len = 2.0; wid = 1.25; padW = 1.0; padH = 1.2; gap = 0.8; }
    else if (name == "1206") { len = 3.2; wid = 1.6; padW = 1.3; padH = 1.5; gap = 1.0; }
    else if (name == "1812") { len = 4.5; wid = 3.2; padW = 1.5; padH = 2.5; gap = 1.5; }
    else if (name == "2512") { len = 6.3; wid = 3.2; padW = 1.8; padH = 2.5; gap = 2.0; }

    // Two pads
    FootprintPrimitive pad1 = Flux::Model::FootprintPrimitive::createPad(
        QPointF(-(len + gap) / 2, 0), "1", "Rect", QSizeF(padW, padH));
    pad1.layer = topCopper();
    fp.addPrimitive(pad1);

    FootprintPrimitive pad2 = Flux::Model::FootprintPrimitive::createPad(
        QPointF((len + gap) / 2, 0), "2", "Rect", QSizeF(padW, padH));
    pad2.layer = topCopper();
    fp.addPrimitive(pad2);

    // Body outline (fabrication layer)
    FootprintPrimitive body = Flux::Model::FootprintPrimitive::createRect(
        QRectF(-len / 2, -wid / 2, len, wid), false, 0.1);
    body.layer = topFabrication();
    fp.addPrimitive(body);

    // Courtyard
    double cyExtra = 0.3;
    addCourtyard(fp, QRectF(-(len + gap) / 2 - padW / 2 - cyExtra, -padH / 2 - cyExtra,
                             (len + gap) + padW + cyExtra * 2, padH + cyExtra * 2));

    addReferenceText(fp, "R?", QPointF(0, padH / 2 + 1.0), 1.0);
    addValueText(fp, "Value", QPointF(0, -padH / 2 - 0.8), 1.0);

    return fp;
}

// ============================================================================
// Passive THT (Axial, Radial)
// ============================================================================

Flux::Model::FootprintDefinition FootprintWizard::generatePassiveTHT(const WizardParams& p) {
    QString name = p.packageType.isEmpty() ? "Axial" : p.packageType;
    FootprintDefinition fp(name);
    fp.setDescription(QString("Through-hole passive %1").arg(name));
    fp.setCategory("Passives");
    fp.setClassification("Through-Hole");

    double holeSpacing = p.rowSpan > 0 ? p.rowSpan : 10.16; // Standard 0.4" spacing
    double drill = p.drillSize > 0 ? p.drillSize : 0.8;

    // Two through-hole pads
    FootprintPrimitive pad1 = Flux::Model::FootprintPrimitive::createPad(
        QPointF(-holeSpacing / 2, 0), "1", "Round", QSizeF(1.6, 1.6));
    pad1.layer = topCopper();
    pad1.data["drill_size"] = drill;
    pad1.data["pad_type"] = "Through-Hole";
    fp.addPrimitive(pad1);

    FootprintPrimitive pad2 = Flux::Model::FootprintPrimitive::createPad(
        QPointF(holeSpacing / 2, 0), "2", "Round", QSizeF(1.6, 1.6));
    pad2.layer = topCopper();
    pad2.data["drill_size"] = drill;
    pad2.data["pad_type"] = "Through-Hole";
    fp.addPrimitive(pad2);

    // Courtyard
    double cyExtra = p.courtyardExtra;
    addCourtyard(fp, QRectF(-holeSpacing / 2 - 1.0 - cyExtra, -1.0 - cyExtra,
                             holeSpacing + 2.0 + cyExtra * 2, 2.0 + cyExtra * 2));

    addReferenceText(fp, name == "Axial" ? "R?" : "C?", QPointF(0, 2.0), 1.0);

    return fp;
}

// ============================================================================
// TO (TO-220, TO-92, TO-3)
// ============================================================================

Flux::Model::FootprintDefinition FootprintWizard::generateTO(const WizardParams& p) {
    QString name = p.packageType.contains("TO") ? p.packageType : "TO-220";
    FootprintDefinition fp(name);
    fp.setDescription(QString("%1 transistor package").arg(name));
    fp.setCategory("Transistors");
    fp.setClassification("Through-Hole");

    if (name == "TO-220") {
        // 3 pads with 2.54mm pitch
        for (int i = 0; i < 3; ++i) {
            double x = (i - 1) * 2.54;
            FootprintPrimitive pad = Flux::Model::FootprintPrimitive::createPad(
                QPointF(x, 0), QString::number(i + 1), "Oblong", QSizeF(1.8, 1.2));
            pad.layer = topCopper();
            pad.data["drill_size"] = 1.0;
            pad.data["pad_type"] = "Through-Hole";
            fp.addPrimitive(pad);
        }

        // Body outline
        addSilkscreenOutline(fp, QRectF(-5.0, -2.5, 10.0, 5.0));

        // Courtyard
        addCourtyard(fp, QRectF(-6.5, -4.0, 13.0, 8.0), 0.05);
        addReferenceText(fp, "Q?", QPointF(0, 4.0), 1.0);

    } else if (name == "TO-92") {
        // 3 pads: E-B-C with specific spacing
        double positions[] = {-1.27, 0, 1.27};
        for (int i = 0; i < 3; ++i) {
            FootprintPrimitive pad = Flux::Model::FootprintPrimitive::createPad(
                QPointF(positions[i], 0), QString::number(i + 1), "Round",
                QSizeF(1.2, 1.2));
            pad.layer = topCopper();
            pad.data["drill_size"] = 0.7;
            pad.data["pad_type"] = "Through-Hole";
            fp.addPrimitive(pad);
        }

        // Body outline (semicircle approximation)
        FootprintPrimitive line1 = Flux::Model::FootprintPrimitive::createLine(
            QPointF(-2.5, 0), QPointF(-2.5, -2.5), 0.15);
        line1.layer = topSilkscreen();
        fp.addPrimitive(line1);

        FootprintPrimitive line2 = Flux::Model::FootprintPrimitive::createLine(
            QPointF(2.5, 0), QPointF(2.5, -2.5), 0.15);
        line2.layer = topSilkscreen();
        fp.addPrimitive(line2);

        FootprintPrimitive arc = Flux::Model::FootprintPrimitive::createArc(
            QPointF(0, -2.5), 2.5, 0, 180, 0.15);
        arc.layer = topSilkscreen();
        fp.addPrimitive(arc);

        addReferenceText(fp, "Q?", QPointF(0, -4.0), 1.0);

    } else {
        // Generic TO: pads in a row
        int pins = p.pinCount > 0 ? p.pinCount : 3;
        for (int i = 0; i < pins; ++i) {
            double x = (i - (pins - 1) / 2.0) * p.pitch;
            FootprintPrimitive pad = Flux::Model::FootprintPrimitive::createPad(
                QPointF(x, 0), QString::number(i + 1), "Round",
                QSizeF(p.padWidth, p.padWidth));
            pad.layer = topCopper();
            pad.data["drill_size"] = p.drillSize;
            pad.data["pad_type"] = "Through-Hole";
            fp.addPrimitive(pad);
        }
        addSilkscreenOutline(fp, QRectF(-p.bodyWidth / 2, -p.bodyHeight / 2, p.bodyWidth, p.bodyHeight));
    }

    return fp;
}

// ============================================================================
// Main generate dispatcher
// ============================================================================

Flux::Model::FootprintDefinition FootprintWizard::generate(const WizardParams& params) {
    QString type = params.packageType.toUpper();

    // Map package type to generator
    if (type.startsWith("DIP")) return generateDIP(params);
    if (type.startsWith("SOIC") || type.startsWith("SOP") || type.startsWith("TSSOP") ||
        type.startsWith("SSOP") || type.startsWith("MSOP")) return generateSOIC(params);
    if (type.startsWith("QFP") || type.startsWith("LQFP") || type.startsWith("TQFP") ||
        type.startsWith("PQFP")) return generateQFP(params);
    if (type.startsWith("QFN") || type.startsWith("DFN")) return generateQFN(params);
    if (type.startsWith("BGA") || type.startsWith("CSP")) return generateBGA(params);
    if (type.startsWith("SOT")) return generateSOT(params);
    if (type.startsWith("TO")) return generateTO(params);
    if (type == "AXIAL" || type == "RADIAL") return generatePassiveTHT(params);

    // Check for passive SMD package names
    if (type == "0201" || type == "0402" || type == "0603" || type == "0805" ||
        type == "1206" || type == "1812" || type == "2512") {
        return generatePassiveSMD(params);
    }

    // Default: try to infer from dimensions
    if (params.pinCount > 4 && params.classification == "Through-Hole") {
        return generateDIP(params);
    }
    if (params.pinCount > 4 && params.classification == "SMD") {
        return generateSOIC(params);
    }

    // Ultimate fallback
    FootprintDefinition fallback("Custom");
    fallback.setDescription("Custom footprint (wizard fallback)");
    fallback.setCategory(params.category.isEmpty() ? "Custom" : params.category);
    return fallback;
}

// ============================================================================
// Predefined package parameters
// ============================================================================

FootprintWizard::WizardParams FootprintWizard::getPredefined(const QString& packageName) {
    WizardParams p;
    p.packageType = packageName.toUpper();

    // DIP packages
    if (packageName.startsWith("DIP")) {
        p.classification = "Through-Hole";
        p.category = "IC";
        bool ok = false;
        int pins = packageName.mid(3).toInt(&ok);
        if (!ok) pins = 8;
        p.pinCount = pins;
        p.pitch = 2.54;
        p.padWidth = 1.0;
        p.rowSpan = (pins <= 18) ? 7.62 : 15.24; // 300mil vs 600mil
        p.drillSize = 0.8;
        p.bodyWidth = (pins <= 18) ? 6.35 : 14.0;
        p.courtyardExtra = 0.5;
    }
    // SOIC packages
    else if (packageName.startsWith("SOIC") || packageName.startsWith("SOP") ||
             packageName.startsWith("TSSOP")) {
        p.classification = "SMD";
        p.category = "IC";
        bool ok = false;
        int pins = packageName.mid(4).toInt(&ok);
        if (!ok) pins = 8;
        p.pinCount = pins;
        p.pitch = 1.27; // Standard SOIC pitch
        p.padWidth = 0.6;
        p.padHeight = 1.5;
        p.rowSpan = 5.3;
        p.bodyWidth = 3.9;
        p.bodyHeight = (pins - 1) * p.pitch + 1.0;
        p.courtyardExtra = 0.5;
    }
    // QFP packages
    else if (packageName.startsWith("QFP") || packageName.startsWith("LQFP") ||
             packageName.startsWith("TQFP")) {
        p.classification = "SMD";
        p.category = "IC";
        bool ok = false;
        int pins = packageName.mid(3).toInt(&ok);
        if (!ok) pins = 44;
        p.pinCount = pins;
        int pinsPerSide = pins / 4;
        p.pitch = (pins <= 64) ? 0.8 : 0.5;
        p.padWidth = 0.5;
        p.padHeight = 1.2;
        p.rowSpan = pinsPerSide * p.pitch + 3.0;
        p.bodyWidth = pinsPerSide * p.pitch + 1.0;
        p.bodyHeight = p.bodyWidth;
        p.courtyardExtra = 0.5;
    }
    // QFN packages
    else if (packageName.startsWith("QFN") || packageName.startsWith("DFN")) {
        p.classification = "SMD";
        p.category = "IC";
        bool ok = false;
        int pins = packageName.mid(3).toInt(&ok);
        if (!ok) pins = 16;
        p.pinCount = pins;
        int pinsPerSide = pins / 4;
        p.pitch = 0.5;
        p.padWidth = 0.3;
        p.padHeight = 0.8;
        p.rowSpan = pinsPerSide * p.pitch + 1.5;
        p.bodyWidth = pinsPerSide * p.pitch;
        p.bodyHeight = p.bodyWidth;
        p.courtyardExtra = 0.5;
    }
    // BGA packages
    else if (packageName.startsWith("BGA")) {
        p.classification = "SMD";
        p.category = "IC";
        bool ok = false;
        int pins = packageName.mid(3).toInt(&ok);
        if (!ok) pins = 256;
        p.pinCount = pins;
        p.pitch = 1.0;
        p.padWidth = 0.5;
        p.drillSize = 0.3;
        p.courtyardExtra = 0.5;
    }
    // SOT packages
    else if (packageName.startsWith("SOT")) {
        p.classification = "SMD";
        p.category = "Transistors";
        if (packageName.contains("23")) {
            p.pinCount = 3;
            p.bodyWidth = 2.9;
            p.bodyHeight = 1.3;
        } else if (packageName.contains("223")) {
            p.pinCount = 4;
            p.bodyWidth = 6.5;
            p.bodyHeight = 3.5;
        } else if (packageName.contains("89")) {
            p.pinCount = 3;
            p.bodyWidth = 4.5;
            p.bodyHeight = 2.5;
        } else {
            p.pinCount = 3;
            p.pitch = 1.27;
            p.rowSpan = 2.0;
            p.bodyWidth = 3.0;
            p.bodyHeight = 1.5;
        }
        p.padWidth = 0.8;
        p.padHeight = 0.7;
        p.courtyardExtra = 0.5;
    }
    // TO packages
    else if (packageName.startsWith("TO")) {
        p.classification = "Through-Hole";
        p.category = "Transistors";
        p.drillSize = 1.0;
        p.padWidth = 1.8;
        p.pitch = 2.54;
        if (packageName.contains("220")) {
            p.pinCount = 3;
            p.bodyWidth = 10.0;
            p.bodyHeight = 5.0;
        } else if (packageName.contains("92")) {
            p.pinCount = 3;
            p.pitch = 1.27;
        } else {
            p.pinCount = 3;
        }
        p.courtyardExtra = 1.0;
    }
    // Passive SMD
    else if (packageName == "0201" || packageName == "0402" || packageName == "0603" ||
             packageName == "0805" || packageName == "1206" || packageName == "1812" ||
             packageName == "2512") {
        p.classification = "SMD";
        p.category = "Passives";
        p.courtyardExtra = 0.3;
    }
    // Passive THT
    else if (packageName == "AXIAL" || packageName == "RADIAL") {
        p.classification = "Through-Hole";
        p.category = "Passives";
        p.drillSize = 0.8;
        p.rowSpan = 10.16;
        p.courtyardExtra = 0.5;
    }

    return p;
}

QStringList FootprintWizard::getSupportedPackages() {
    QStringList packages;

    // DIP
    for (int pins : {6, 8, 14, 16, 18, 20, 24, 28, 40})
        packages << QString("DIP-%1").arg(pins);

    // SOIC
    for (int pins : {8, 14, 16, 20, 24, 28})
        packages << QString("SOIC-%1").arg(pins);

    // TSSOP
    for (int pins : {8, 14, 16, 20, 24, 28})
        packages << QString("TSSOP-%1").arg(pins);

    // QFP
    for (int pins : {44, 52, 64, 80, 100, 128, 144, 176, 208})
        packages << QString("QFP-%1").arg(pins);

    // QFN
    for (int pins : {16, 20, 24, 28, 32, 40, 48, 56, 64})
        packages << QString("QFN-%1").arg(pins);

    // BGA
    for (int pins : {16, 25, 36, 49, 64, 100, 144, 256})
        packages << QString("BGA-%1").arg(pins);

    // SOT
    packages << "SOT-23" << "SOT-223" << "SOT-89";

    // TO
    packages << "TO-220" << "TO-92" << "TO-3";

    // Passive SMD
    packages << "0201" << "0402" << "0603" << "0805" << "1206" << "1812" << "2512";

    // Passive THT
    packages << "Axial" << "Radial";

    return packages;
}

QString FootprintWizard::getCategory(const QString& packageName) {
    if (packageName.startsWith("DIP") || packageName.startsWith("SOIC") ||
        packageName.startsWith("TSSOP") || packageName.startsWith("QFP") ||
        packageName.startsWith("QFN") || packageName.startsWith("BGA"))
        return "IC";
    if (packageName.startsWith("SOT") || packageName.startsWith("TO"))
        return "Transistors";
    if (packageName == "0201" || packageName == "0402" || packageName == "0603" ||
        packageName == "0805" || packageName == "1206" || packageName == "1812" ||
        packageName == "2512" || packageName == "Axial" || packageName == "Radial")
        return "Passives";
    return "Custom";
}
