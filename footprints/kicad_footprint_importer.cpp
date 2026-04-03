#include "kicad_footprint_importer.h"

#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QLineF>
#include <QSet>
#include <QtMath>
#include <cmath>

using Flux::Model::FootprintDefinition;
using Flux::Model::FootprintPrimitive;
using Flux::Model::Footprint3DModel;

namespace {

bool isBoundary(QChar c) {
    return c.isSpace() || c == '(' || c == ')' || c == '"' || c == ';';
}

int findSExprStart(const QString& content, const QString& key, int from = 0) {
    const QString needle = "(" + key;
    bool inString = false;
    bool escaped = false;
    bool inComment = false;

    for (int i = qMax(0, from); i < content.size(); ++i) {
        const QChar ch = content[i];
        if (inComment) {
            if (ch == '\n' || ch == '\r') inComment = false;
            continue;
        }
        if (inString) {
            if (escaped) {
                escaped = false;
                continue;
            }
            if (ch == '\\') {
                escaped = true;
                continue;
            }
            if (ch == '"') inString = false;
            continue;
        }
        if (ch == ';') {
            inComment = true;
            continue;
        }
        if (ch == '"') {
            inString = true;
            continue;
        }
        if (ch == '(' && i + needle.size() <= content.size() && content.mid(i, needle.size()) == needle) {
            const int after = i + needle.size();
            if (after >= content.size() || isBoundary(content[after])) return i;
        }
    }
    return -1;
}

QString extractBalancedSExpr(const QString& content, int start, int* endOut = nullptr) {
    if (start < 0 || start >= content.size() || content[start] != '(') return QString();
    bool inString = false;
    bool escaped = false;
    bool inComment = false;
    int depth = 0;
    for (int i = start; i < content.size(); ++i) {
        const QChar ch = content[i];
        if (inComment) {
            if (ch == '\n' || ch == '\r') inComment = false;
            continue;
        }
        if (inString) {
            if (escaped) {
                escaped = false;
                continue;
            }
            if (ch == '\\') {
                escaped = true;
                continue;
            }
            if (ch == '"') inString = false;
            continue;
        }
        if (ch == ';') {
            inComment = true;
            continue;
        }
        if (ch == '"') {
            inString = true;
            continue;
        }
        if (ch == '(') ++depth;
        else if (ch == ')') --depth;
        if (depth == 0) {
            if (endOut) *endOut = i;
            return content.mid(start, i - start + 1);
        }
    }
    return QString();
}

QString stripQuotes(QString token) {
    token = token.trimmed();
    if (token.size() >= 2 && token.startsWith('"') && token.endsWith('"')) {
        token = token.mid(1, token.size() - 2);
    }
    return token;
}

QString decodeEscapes(QString s) {
    s.replace("\\n", "\n");
    s.replace("\\r", "\r");
    s.replace("\\t", "\t");
    s.replace("\\\"", "\"");
    s.replace("\\\\", "\\");
    return s;
}

QString parseFootprintName(const QString& sexpr) {
    QRegularExpression re("^\\((?:footprint|module)\\s+(?:\"([^\"]+)\"|([^\\s\\)]+))");
    QRegularExpressionMatch m = re.match(sexpr);
    if (!m.hasMatch()) return QString();
    return stripQuotes(m.captured(1).isEmpty() ? m.captured(2) : m.captured(1));
}

FootprintPrimitive::Layer mapLayer(const QString& kicadLayer, FootprintPrimitive::Layer fallback) {
    const QString l = kicadLayer.trimmed();
    if (l == "F.Cu") return FootprintPrimitive::Top_Copper;
    if (l == "B.Cu") return FootprintPrimitive::Bottom_Copper;
    if (l == "F.SilkS") return FootprintPrimitive::Top_Silkscreen;
    if (l == "B.SilkS") return FootprintPrimitive::Bottom_Silkscreen;
    if (l == "F.CrtYd" || l == "B.CrtYd") return FootprintPrimitive::Top_Courtyard;
    if (l == "F.Fab" || l == "B.Fab") return FootprintPrimitive::Top_Fabrication;
    return fallback;
}

FootprintPrimitive::Layer extractLayer(const QString& expr, FootprintPrimitive::Layer fallback = FootprintPrimitive::Top_Silkscreen) {
    QRegularExpression re("\\(layer\\s+\"([^\"]+)\"\\)");
    QRegularExpressionMatch m = re.match(expr);
    if (!m.hasMatch()) return fallback;
    return mapLayer(m.captured(1), fallback);
}

qreal parseStrokeWidth(const QString& expr, qreal fallback = 0.12) {
    QRegularExpression re("\\(stroke[\\s\\S]*?\\(width\\s+([\\-0-9.]+)\\)");
    QRegularExpressionMatch m = re.match(expr);
    return m.hasMatch() ? m.captured(1).toDouble() : fallback;
}

qreal kx(const QString& s) { return s.toDouble(); }
qreal ky(const QString& s) { return -s.toDouble(); } // KiCad -> scene convention

bool arcFrom3Points(
    const QPointF& p1, const QPointF& p2, const QPointF& p3,
    QPointF& center, qreal& radius, qreal& startDeg, qreal& spanDeg) {
    const qreal x1 = p1.x(), y1 = p1.y();
    const qreal x2 = p2.x(), y2 = p2.y();
    const qreal x3 = p3.x(), y3 = p3.y();
    const qreal D = 2.0 * (x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2));
    if (qAbs(D) < 1e-9) return false;

    const qreal cx = ((x1 * x1 + y1 * y1) * (y2 - y3) + (x2 * x2 + y2 * y2) * (y3 - y1) + (x3 * x3 + y3 * y3) * (y1 - y2)) / D;
    const qreal cy = ((x1 * x1 + y1 * y1) * (x3 - x2) + (x2 * x2 + y2 * y2) * (x1 - x3) + (x3 * x3 + y3 * y3) * (x2 - x1)) / D;

    center = QPointF(cx, cy);
    radius = QLineF(center, p1).length();

    auto norm360 = [](qreal a) {
        qreal v = std::fmod(a, 360.0);
        if (v < 0.0) v += 360.0;
        return v;
    };
    auto ccwDelta = [&](qreal from, qreal to) {
        qreal d = norm360(to) - norm360(from);
        if (d < 0.0) d += 360.0;
        return d;
    };

    const qreal aStart = norm360(qRadiansToDegrees(std::atan2(-(p1.y() - cy), (p1.x() - cx))));
    const qreal aMid = norm360(qRadiansToDegrees(std::atan2(-(p2.y() - cy), (p2.x() - cx))));
    const qreal aEnd = norm360(qRadiansToDegrees(std::atan2(-(p3.y() - cy), (p3.x() - cx))));
    const qreal ccwSE = ccwDelta(aStart, aEnd);
    const qreal ccwSM = ccwDelta(aStart, aMid);

    startDeg = aStart;
    spanDeg = (ccwSM <= ccwSE + 1e-6) ? ccwSE : -(360.0 - ccwSE);
    return true;
}

KicadFootprintImporter::ImportReport parseFootprintExpr(const QString& fpExpr) {
    const QString fpName = parseFootprintName(fpExpr);
    KicadFootprintImporter::ImportReport report;
    report.footprint = FootprintDefinition(fpName);
    FootprintDefinition& def = report.footprint;
    if (fpName.isEmpty()) return report;

    QRegularExpression descrRe("\\(descr\\s+\"([^\"]*)\"\\)");
    QRegularExpressionMatch mDescr = descrRe.match(fpExpr);
    if (mDescr.hasMatch()) def.setDescription(decodeEscapes(mDescr.captured(1)));

    // Graphics: fp_line
    int cursor = 0;
    while (true) {
        int pos = findSExprStart(fpExpr, "fp_line", cursor);
        if (pos < 0) break;
        int end = -1;
        QString e = extractBalancedSExpr(fpExpr, pos, &end);
        if (e.isEmpty()) break;
        QRegularExpression re("\\(start\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\)[\\s\\S]*?\\(end\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\)");
        QRegularExpressionMatch m = re.match(e);
        if (m.hasMatch()) {
            FootprintPrimitive p = FootprintPrimitive::createLine(
                QPointF(kx(m.captured(1)), ky(m.captured(2))),
                QPointF(kx(m.captured(3)), ky(m.captured(4))),
                parseStrokeWidth(e, 0.12));
            p.layer = extractLayer(e, FootprintPrimitive::Top_Silkscreen);
            def.addPrimitive(p);
            ++report.lineCount;
        }
        cursor = qMax(pos + 1, end + 1);
    }

    // fp_rect
    cursor = 0;
    while (true) {
        int pos = findSExprStart(fpExpr, "fp_rect", cursor);
        if (pos < 0) break;
        int end = -1;
        QString e = extractBalancedSExpr(fpExpr, pos, &end);
        if (e.isEmpty()) break;
        QRegularExpression re("\\(start\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\)[\\s\\S]*?\\(end\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\)");
        QRegularExpressionMatch m = re.match(e);
        if (m.hasMatch()) {
            QRectF r(QPointF(kx(m.captured(1)), ky(m.captured(2))), QPointF(kx(m.captured(3)), ky(m.captured(4))));
            FootprintPrimitive p = FootprintPrimitive::createRect(r.normalized(), false, parseStrokeWidth(e, 0.12));
            p.layer = extractLayer(e, FootprintPrimitive::Top_Silkscreen);
            def.addPrimitive(p);
            ++report.rectCount;
        }
        cursor = qMax(pos + 1, end + 1);
    }

    // fp_circle
    cursor = 0;
    while (true) {
        int pos = findSExprStart(fpExpr, "fp_circle", cursor);
        if (pos < 0) break;
        int end = -1;
        QString e = extractBalancedSExpr(fpExpr, pos, &end);
        if (e.isEmpty()) break;
        QRegularExpression re("\\(center\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\)[\\s\\S]*?\\(end\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\)");
        QRegularExpressionMatch m = re.match(e);
        if (m.hasMatch()) {
            QPointF c(kx(m.captured(1)), ky(m.captured(2)));
            QPointF pe(kx(m.captured(3)), ky(m.captured(4)));
            FootprintPrimitive p = FootprintPrimitive::createCircle(c, QLineF(c, pe).length(), false, parseStrokeWidth(e, 0.12));
            p.layer = extractLayer(e, FootprintPrimitive::Top_Silkscreen);
            def.addPrimitive(p);
            ++report.circleCount;
        }
        cursor = qMax(pos + 1, end + 1);
    }

    // fp_arc
    cursor = 0;
    while (true) {
        int pos = findSExprStart(fpExpr, "fp_arc", cursor);
        if (pos < 0) break;
        int end = -1;
        QString e = extractBalancedSExpr(fpExpr, pos, &end);
        if (e.isEmpty()) break;
        QRegularExpression re("\\(start\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\)[\\s\\S]*?\\(mid\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\)[\\s\\S]*?\\(end\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\)");
        QRegularExpressionMatch m = re.match(e);
        if (m.hasMatch()) {
            QPointF p1(kx(m.captured(1)), ky(m.captured(2)));
            QPointF p2(kx(m.captured(3)), ky(m.captured(4)));
            QPointF p3(kx(m.captured(5)), ky(m.captured(6)));
            QPointF c;
            qreal r = 0.0;
            qreal startDeg = 0.0;
            qreal spanDeg = 0.0;
            if (arcFrom3Points(p1, p2, p3, c, r, startDeg, spanDeg)) {
                FootprintPrimitive p = FootprintPrimitive::createArc(c, r, startDeg, spanDeg, parseStrokeWidth(e, 0.12));
                p.layer = extractLayer(e, FootprintPrimitive::Top_Silkscreen);
                def.addPrimitive(p);
                ++report.arcCount;
            }
        }
        cursor = qMax(pos + 1, end + 1);
    }

    // fp_text
    cursor = 0;
    while (true) {
        int pos = findSExprStart(fpExpr, "fp_text", cursor);
        if (pos < 0) break;
        int end = -1;
        QString e = extractBalancedSExpr(fpExpr, pos, &end);
        if (e.isEmpty()) break;
        QRegularExpression headRe("^\\(fp_text\\s+([^\\s\\)]+)\\s+\"([^\"]*)\"");
        QRegularExpression atRe("\\(at\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\s*([\\-0-9.]*)\\)");
        QRegularExpression sizeRe("\\(size\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\)");
        QRegularExpressionMatch mh = headRe.match(e);
        QRegularExpressionMatch ma = atRe.match(e);
        if (mh.hasMatch() && ma.hasMatch()) {
            const QString text = decodeEscapes(mh.captured(2));
            FootprintPrimitive p = FootprintPrimitive::createText(
                text,
                QPointF(kx(ma.captured(1)), ky(ma.captured(2))),
                1.0);
            QRegularExpressionMatch ms = sizeRe.match(e);
            if (ms.hasMatch()) p.data["height"] = ms.captured(2).toDouble();
            if (!ma.captured(3).isEmpty()) p.data["rotation"] = -ma.captured(3).toDouble();
            p.layer = extractLayer(e, FootprintPrimitive::Top_Silkscreen);
            def.addPrimitive(p);
            ++report.textCount;
        }
        cursor = qMax(pos + 1, end + 1);
    }

    // pad
    cursor = 0;
    while (true) {
        int pos = findSExprStart(fpExpr, "pad", cursor);
        if (pos < 0) break;
        int end = -1;
        QString e = extractBalancedSExpr(fpExpr, pos, &end);
        if (e.isEmpty()) break;

        QRegularExpression headRe("^\\(pad\\s+(\"[^\"]*\"|[^\\s\\)]+)\\s+([^\\s\\)]+)\\s+([^\\s\\)]+)");
        QRegularExpression atRe("\\(at\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\s*([\\-0-9.]*)\\)");
        QRegularExpression sizeRe("\\(size\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\)");
        QRegularExpression drillRe("\\(drill\\s+(?:oval\\s+)?([\\-0-9.]+)(?:\\s+([\\-0-9.]+))?\\)");
        QRegularExpression layersRe("\\(layers\\s+([^\\)]*)\\)");
        QRegularExpression rrRe("\\(roundrect_rratio\\s+([\\-0-9.]+)\\)");

        QRegularExpressionMatch mh = headRe.match(e);
        QRegularExpressionMatch ma = atRe.match(e);
        QRegularExpressionMatch ms = sizeRe.match(e);
        if (!mh.hasMatch() || !ma.hasMatch() || !ms.hasMatch()) {
            cursor = qMax(pos + 1, end + 1);
            continue;
        }

        const QString number = stripQuotes(mh.captured(1));
        const QString padType = mh.captured(2).trimmed().toLower();
        const QString padShapeKicad = mh.captured(3).trimmed().toLower();

        QString shape = "Rect";
        if (padShapeKicad == "circle") shape = "Round";
        else if (padShapeKicad == "oval") shape = "Oblong";
        else if (padShapeKicad == "roundrect") shape = "RoundedRect";
        else if (padShapeKicad == "custom") shape = "Custom";

        const qreal w = ms.captured(1).toDouble();
        const qreal h = ms.captured(2).toDouble();
        FootprintPrimitive p = FootprintPrimitive::createPad(
            QPointF(kx(ma.captured(1)), ky(ma.captured(2))),
            number,
            shape,
            QSizeF(w, h));

        if (!ma.captured(3).isEmpty()) p.data["rotation"] = -ma.captured(3).toDouble();

        QRegularExpressionMatch md = drillRe.match(e);
        if (md.hasMatch()) p.data["drill_size"] = md.captured(1).toDouble();

        QRegularExpressionMatch ml = layersRe.match(e);
        if (ml.hasMatch()) {
            const QString layers = ml.captured(1);
            const bool hasB = layers.contains("B.Cu");
            const bool hasF = layers.contains("F.Cu");
            if (hasB && !hasF) p.layer = FootprintPrimitive::Bottom_Copper;
            else p.layer = FootprintPrimitive::Top_Copper;
        } else {
            p.layer = FootprintPrimitive::Top_Copper;
        }

        if (padType == "np_thru_hole") p.data["plated"] = false;
        else p.data["plated"] = true;

        QRegularExpressionMatch mrr = rrRe.match(e);
        if (mrr.hasMatch() && shape == "RoundedRect") {
            const qreal rr = mrr.captured(1).toDouble();
            p.data["corner_radius"] = rr * qMin(w, h) * 0.5;
        }

        def.addPrimitive(p);
        ++report.padCount;
        cursor = qMax(pos + 1, end + 1);
    }

    // model
    cursor = 0;
    while (true) {
        int pos = findSExprStart(fpExpr, "model", cursor);
        if (pos < 0) break;
        int end = -1;
        QString e = extractBalancedSExpr(fpExpr, pos, &end);
        if (e.isEmpty()) break;

        Footprint3DModel model;
        QRegularExpression fileRe("^\\(model\\s+\"([^\"]+)\"");
        QRegularExpression xyzRe("\\(xyz\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\)");
        QRegularExpression offRe("\\(offset\\s*\\(xyz\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\)\\)");
        QRegularExpression sclRe("\\(scale\\s*\\(xyz\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\)\\)");
        QRegularExpression rotRe("\\(rotate\\s*\\(xyz\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\)\\)");

        QRegularExpressionMatch mf = fileRe.match(e);
        if (mf.hasMatch()) model.filename = decodeEscapes(mf.captured(1));
        QRegularExpressionMatch mo = offRe.match(e);
        if (mo.hasMatch()) model.offset = QVector3D(mo.captured(1).toDouble(), mo.captured(2).toDouble(), mo.captured(3).toDouble());
        QRegularExpressionMatch ms = sclRe.match(e);
        if (ms.hasMatch()) model.scale = QVector3D(ms.captured(1).toDouble(), ms.captured(2).toDouble(), ms.captured(3).toDouble());
        QRegularExpressionMatch mr = rotRe.match(e);
        if (mr.hasMatch()) model.rotation = QVector3D(mr.captured(1).toDouble(), mr.captured(2).toDouble(), mr.captured(3).toDouble());

        def.addModel3D(model);
        cursor = qMax(pos + 1, end + 1);
    }

    if (def.category().isEmpty()) def.setCategory("Imported KiCad");

    // Collect unsupported graphic primitives for visibility.
    static const QSet<QString> handled = {
        "fp_line", "fp_rect", "fp_circle", "fp_arc", "fp_text", "pad", "model"
    };
    QMap<QString, int> unknownKinds;
    QRegularExpression anyPrimRe("\\((fp_[a-z_]+|pad|model)\\b");
    QRegularExpressionMatchIterator it = anyPrimRe.globalMatch(fpExpr);
    while (it.hasNext()) {
        const QString kind = it.next().captured(1);
        if (!handled.contains(kind)) unknownKinds[kind] += 1;
    }
    for (auto i = unknownKinds.begin(); i != unknownKinds.end(); ++i) {
        report.unsupportedKinds.append(QString("%1 (%2)").arg(i.key()).arg(i.value()));
        report.unsupportedCount += i.value();
    }

    return report;
}

QString readTextFile(const QString& filePath) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    QTextStream in(&f);
    return in.readAll();
}

} // namespace

QStringList KicadFootprintImporter::getFootprintNames(const QString& filePath) {
    QStringList names;
    const QString content = readTextFile(filePath);
    if (content.isEmpty()) return names;

    int cursor = 0;
    while (true) {
        int pos = findSExprStart(content, "footprint", cursor);
        if (pos < 0) pos = findSExprStart(content, "module", cursor);
        if (pos < 0) break;
        int end = -1;
        const QString expr = extractBalancedSExpr(content, pos, &end);
        if (expr.isEmpty()) break;
        const QString name = parseFootprintName(expr);
        if (!name.isEmpty()) names << name;
        cursor = qMax(pos + 1, end + 1);
    }

    names.removeDuplicates();
    return names;
}

FootprintDefinition KicadFootprintImporter::importFootprint(const QString& filePath, const QString& footprintName) {
    return importFootprintDetailed(filePath, footprintName).footprint;
}

KicadFootprintImporter::ImportReport KicadFootprintImporter::importFootprintDetailed(
    const QString& filePath, const QString& footprintName) {
    const QString content = readTextFile(filePath);
    if (content.isEmpty()) return ImportReport();

    int cursor = 0;
    QString firstFoundExpr;
    while (true) {
        int pos = findSExprStart(content, "footprint", cursor);
        if (pos < 0) pos = findSExprStart(content, "module", cursor);
        if (pos < 0) break;
        int end = -1;
        const QString expr = extractBalancedSExpr(content, pos, &end);
        if (expr.isEmpty()) break;
        const QString name = parseFootprintName(expr);

        if (firstFoundExpr.isEmpty()) firstFoundExpr = expr;
        if (!footprintName.isEmpty() && name == footprintName) {
            return parseFootprintExpr(expr);
        }
        cursor = qMax(pos + 1, end + 1);
    }

    if (!firstFoundExpr.isEmpty()) {
        ImportReport report = parseFootprintExpr(firstFoundExpr);
        if (!footprintName.isEmpty() && report.footprint.name().isEmpty()) {
            report.footprint.setName(footprintName);
        }
        return report;
    }
    return ImportReport();
}
