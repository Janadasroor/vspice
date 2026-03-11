#include "kicad_symbol_importer.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QSet>
#include <QDebug>
//#include "../footprints/footprint_library.h"
#include <cmath>

using Flux::Model::SymbolDefinition;
using Flux::Model::SymbolPrimitive;

static const double KICAD_SCALE = 10.0;
static const double KICAD_MM_TO_QT_PT = 72.0 / 25.4;

namespace {
bool isSymbolBoundary(QChar c) {
    return c.isSpace() || c == '(' || c == ')' || c == '"' || c == ';';
}

int findSExprStart(const QString& content, const QString& key, int from) {
    bool inString = false;
    bool escaped = false;
    bool inComment = false;

    const QString needle = "(" + key;
    for (int i = std::max(0, from); i < content.size(); ++i) {
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
            if (after >= content.size() || isSymbolBoundary(content[after])) {
                return i;
            }
        }
    }
    return -1;
}

QString extractBalancedSExpr(const QString& content, int start, int* endOut = nullptr) {
    if (start < 0 || start >= content.size() || content[start] != '(') return QString();

    bool inString = false;
    bool escaped = false;
    bool inComment = false;
    int parenCount = 0;

    for (int i = start; i < content.length(); ++i) {
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

        if (ch == '(') ++parenCount;
        else if (ch == ')') --parenCount;

        if (parenCount == 0) {
            if (endOut) *endOut = i;
            return content.mid(start, i - start + 1);
        }
    }

    return QString();
}

QString parseSExprName(const QString& sexpr) {
    if (sexpr.isEmpty()) return QString();

    bool inString = false;
    bool escaped = false;
    int stringStart = -1;
    for (int i = 0; i < sexpr.size(); ++i) {
        const QChar ch = sexpr[i];
        if (!inString) {
            if (ch == '"') {
                inString = true;
                stringStart = i + 1;
            }
            continue;
        }

        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            return (stringStart >= 0 && i >= stringStart) ? sexpr.mid(stringStart, i - stringStart) : QString();
        }
    }

    return QString();
}

QString extractSymbolByName(const QString& content, const QString& symbolName) {
    int cursor = 0;
    while (true) {
        const int start = findSExprStart(content, "symbol", cursor);
        if (start < 0) break;

        int end = -1;
        const QString sym = extractBalancedSExpr(content, start, &end);
        if (sym.isEmpty()) break;

        if (parseSExprName(sym) == symbolName) {
            return sym;
        }
        cursor = std::max(start + 1, end + 1);
    }
    return QString();
}

void parseKiCadSubSymbolSuffix(const QString& fullName, int& outUnit, int& outBodyStyle) {
    // KiCad child symbol naming commonly uses: Parent_U_B
    //  U=0 shared/all units, otherwise specific unit index
    //  B=0 shared/all body styles, 1/2 = concrete style
    outUnit = 0;
    outBodyStyle = 0;

    QRegularExpression suffixRe("_(\\d+)_(\\d+)$");
    QRegularExpressionMatch m = suffixRe.match(fullName);
    if (!m.hasMatch()) return;

    outUnit = m.captured(1).toInt();
    outBodyStyle = m.captured(2).toInt();
}

qreal parseStrokeWidth(const QString& sexpr, qreal defaultWidth = 1.5) {
    QRegularExpression re("\\(stroke[\\s\\S]*?\\(width\\s+([\\-0-9.]+)\\)");
    QRegularExpressionMatch m = re.match(sexpr);
    if (!m.hasMatch()) return defaultWidth;
    const qreal w = m.captured(1).toDouble() * KICAD_SCALE;
    return (w > 0.0) ? w : defaultWidth;
}

QString parseFillType(const QString& sexpr) {
    QRegularExpression re("\\(fill[\\s\\S]*?\\(type\\s+([a-z_]+)\\)");
    QRegularExpressionMatch m = re.match(sexpr);
    if (!m.hasMatch()) return QString("none");
    return m.captured(1).toLower();
}

qreal normDeg360(qreal a) {
    qreal v = std::fmod(a, 360.0);
    if (v < 0.0) v += 360.0;
    return v;
}

qreal ccwDelta(qreal from, qreal to) {
    qreal d = normDeg360(to) - normDeg360(from);
    if (d < 0.0) d += 360.0;
    return d;
}

QString mapKiCadElectricalType(const QString& raw) {
    const QString t = raw.trimmed().toLower();
    if (t == "input") return "Input";
    if (t == "output") return "Output";
    if (t == "bidirectional") return "Bidirectional";
    if (t == "tri_state") return "Tri-state";
    if (t == "passive") return "Passive";
    if (t == "free") return "Free";
    if (t == "unspecified") return "Unspecified";
    if (t == "power_in") return "Power Input";
    if (t == "power_out") return "Power Output";
    if (t == "open_collector") return "Open Collector";
    if (t == "open_emitter") return "Open Emitter";
    if (t == "no_connect") return "Not Connected";
    return "Passive";
}

QString mapKiCadPinShape(const QString& raw) {
    const QString s = raw.trimmed().toLower();
    if (s == "line" || s == "non_logic") return "Line";
    if (s == "inverted") return "Inverted";
    if (s == "clock" || s == "edge_clock") return "Clock";
    if (s == "inverted_clock" || s == "clock_low") return "Inverted Clock";
    if (s == "input_low") return "Input Low";
    if (s == "output_low") return "Output Low";
    return "Line";
}

void parseKiCadTextStyle(const QString& textS, QString& outHAlign, QString& outVAlign, qreal& outRotationDeg) {
    // KiCad symbol text defaults to centered placement when justify is omitted.
    outHAlign = "center";
    outVAlign = "center";
    outRotationDeg = 0.0;

    QRegularExpression atRe("\\(at\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\s*([\\-0-9.]*)\\)");
    QRegularExpressionMatch mAt = atRe.match(textS);
    if (mAt.hasMatch() && !mAt.captured(3).isEmpty()) {
        outRotationDeg = mAt.captured(3).toDouble();
    }

    QRegularExpression justRe("\\(justify\\s+([^\\)]*)\\)");
    QRegularExpressionMatch mJust = justRe.match(textS);
    if (!mJust.hasMatch()) return;

    const QStringList toks = mJust.captured(1).split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    for (const QString& tRaw : toks) {
        const QString t = tRaw.toLower();
        if (t == "left" || t == "center" || t == "right") outHAlign = t;
        else if (t == "top" || t == "bottom") outVAlign = t;
        else if (t == "middle") outVAlign = "center";
    }
}

QString decodeKiCadEscapes(QString s) {
    s.replace("\\n", "\n");
    s.replace("\\r", "\r");
    s.replace("\\t", "\t");
    s.replace("\\\"", "\"");
    s.replace("\\\\", "\\");
    return s;
}

QString normalizeFootprintCandidate(QString value) {
    value = decodeKiCadEscapes(value).trimmed();
    if (value.isEmpty()) return value;

    if ((value.startsWith('"') && value.endsWith('"')) ||
        (value.startsWith('\'') && value.endsWith('\''))) {
        value = value.mid(1, value.size() - 2).trimmed();
    }

    // Common SnapEDA/KiCad wrappers: ${LIB:Footprint}
    if (value.startsWith("${") && value.endsWith("}")) {
        value = value.mid(2, value.size() - 3).trimmed();
    }
    if (value.startsWith('{') && value.endsWith('}')) {
        value = value.mid(1, value.size() - 2).trimmed();
    }

    return value;
}

QStringList splitFpFilters(const QString& raw) {
    QStringList out;
    const QString cleaned = decodeKiCadEscapes(raw).trimmed();
    if (cleaned.isEmpty()) return out;
    const QStringList parts = cleaned.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    for (const QString& p : parts) {
        const QString s = p.trimmed();
        if (!s.isEmpty()) out.append(s);
    }
    out.removeDuplicates();
    return out;
}

QStringList allFootprintNames() {
    return QStringList();
}

QString resolveFootprintName(const QString& rawFootprint) {
    const QString fp = normalizeFootprintCandidate(rawFootprint);
    if (fp.isEmpty()) return QString();

    QStringList candidates;
    candidates << fp;

    const int colon = fp.indexOf(':');
    if (colon > 0 && colon + 1 < fp.size()) {
        candidates << fp.mid(colon + 1).trimmed();
    }

    const int slash = fp.lastIndexOf('/');
    if (slash >= 0 && slash + 1 < fp.size()) {
        candidates << fp.mid(slash + 1).trimmed();
    }

    candidates.removeDuplicates();

    // Footprint library manager is removed. Just return the base name.
    return candidates.value(candidates.size() > 1 ? 1 : 0, fp);
}

QString resolveFootprintFromFilters(const QStringList& filters) {
    if (filters.isEmpty()) return QString();
    const QStringList names = allFootprintNames();
    if (names.isEmpty()) return QString();

    for (const QString& filter : filters) {
        if (filter.isEmpty()) continue;
        QRegularExpression re(
            QRegularExpression::wildcardToRegularExpression(filter),
            QRegularExpression::CaseInsensitiveOption);
        for (const QString& name : names) {
            if (re.match(name).hasMatch()) return name;
        }
    }
    return QString();
}

bool isLikelyFootprintKey(const QString& key) {
    const QString k = key.trimmed().toLower();
    return k.contains("footprint") || k == "package" || k == "case_package" || k == "case/package";
}

bool isLikelyFootprintValue(const QString& value) {
    const QString v = value.trimmed();
    if (v.isEmpty()) return false;
    if (v.contains(':')) return true;            // KiCad Lib:Name
    if (v.contains('*') || v.contains('?')) return true; // filter-style
    return v.contains("DIP", Qt::CaseInsensitive) ||
           v.contains("SOIC", Qt::CaseInsensitive) ||
           v.contains("QFN", Qt::CaseInsensitive) ||
           v.contains("QFP", Qt::CaseInsensitive) ||
           v.contains("SOT", Qt::CaseInsensitive) ||
           v.contains("TSSOP", Qt::CaseInsensitive) ||
           v.contains("DFN", Qt::CaseInsensitive) ||
           v.contains("LGA", Qt::CaseInsensitive) ||
           v.contains("BGA", Qt::CaseInsensitive);
}

void mergeStackedPins(SymbolDefinition& def) {
    QMap<QString, int> firstByKey;
    QSet<int> removeIdx;
    QList<SymbolPrimitive>& prims = def.primitives();

    auto pinNumberString = [](const SymbolPrimitive& p) -> QString {
        if (p.data.contains("num")) return p.data.value("num").toString();
        return QString::number(p.data.value("number").toInt());
    };

    for (int i = 0; i < prims.size(); ++i) {
        SymbolPrimitive& p = prims[i];
        if (p.type != SymbolPrimitive::Pin) continue;

        const qreal x = p.data.value("x").toDouble();
        const qreal y = p.data.value("y").toDouble();
        const qreal l = p.data.value("length").toDouble();
        const QString o = p.data.value("orientation").toString();
        const QString k = QString("%1|%2|%3|%4|%5|%6|%7")
            .arg(p.unit()).arg(p.bodyStyle())
            .arg(qRound(x * 1000.0)).arg(qRound(y * 1000.0))
            .arg(qRound(l * 1000.0)).arg(o).arg(p.data.value("name").toString());

        if (!firstByKey.contains(k)) {
            firstByKey[k] = i;
            continue;
        }

        SymbolPrimitive& base = prims[firstByKey[k]];
        QStringList stacked = base.data.value("stackedNumbers").toString()
            .split(",", Qt::SkipEmptyParts);
        for (QString& s : stacked) s = s.trimmed();

        const QString baseNum = pinNumberString(base).trimmed();
        const QString thisNum = pinNumberString(p).trimmed();
        if (!thisNum.isEmpty() && thisNum != baseNum && !stacked.contains(thisNum)) {
            stacked.append(thisNum);
            base.data["stackedNumbers"] = stacked.join(",");
        }

        QStringList alts = base.data.value("alternateNames").toString()
            .split(",", Qt::SkipEmptyParts);
        for (QString& s : alts) s = s.trimmed();
        const QString thisName = p.data.value("name").toString().trimmed();
        const QString baseName = base.data.value("name").toString().trimmed();
        if (!thisName.isEmpty() && thisName != baseName && !alts.contains(thisName)) {
            alts.append(thisName);
            base.data["alternateNames"] = alts.join(",");
        }

        removeIdx.insert(i);
    }

    if (removeIdx.isEmpty()) return;
    QList<SymbolPrimitive> out;
    out.reserve(prims.size() - removeIdx.size());
    for (int i = 0; i < prims.size(); ++i) {
        if (!removeIdx.contains(i)) out.append(prims[i]);
    }
    prims = out;
}

SymbolDefinition importSymbolFromContent(const QString& content, const QString& symbolName, QSet<QString>& visiting, QString* footprintSource = nullptr) {
    SymbolDefinition def(symbolName);
    if (symbolName.isEmpty()) return def;
    if (visiting.contains(symbolName)) {
        qWarning() << "KiCad importer: cyclic extends detected for symbol" << symbolName;
        return def;
    }

    QString symbolContent = extractSymbolByName(content, symbolName);
    if (symbolContent.isEmpty()) return def;

    visiting.insert(symbolName);

    // Resolve inheritance first so child overrides parent values/primitives.
    QRegularExpression extendsRe("\\(extends\\s+\"([^\"]+)\"\\)");
    QRegularExpressionMatch mExt = extendsRe.match(symbolContent);
    if (mExt.hasMatch()) {
        const QString parentName = mExt.captured(1).trimmed();
        if (!parentName.isEmpty() && parentName != symbolName) {
            QString parentSource;
            SymbolDefinition parent = importSymbolFromContent(content, parentName, visiting, &parentSource);
            if (!parent.name().isEmpty()) {
                def = parent.clone();
                def.setName(symbolName);
                if (footprintSource && !parentSource.isEmpty()) *footprintSource = parentSource;
            }
        }
    }

    // Parse properties from child and override inherited values.
    QRegularExpression propRe("\\(property\\s+\"([^\"]+)\"\\s+\"([^\"]*)\"");
    QString fallbackFootprintFromField;
    QRegularExpressionMatchIterator pi = propRe.globalMatch(symbolContent);
    while (pi.hasNext()) {
        QRegularExpressionMatch match = pi.next();
        const QString key = match.captured(1).trimmed();
        const QString val = match.captured(2);
        const QString keyL = key.toLower();

        if (keyL == "reference") def.setReferencePrefix(decodeKiCadEscapes(val));
        else if (keyL == "description") def.setDescription(decodeKiCadEscapes(val));
        else if (keyL == "datasheet") def.setDatasheet(decodeKiCadEscapes(val));
        else if (keyL == "value") def.setDefaultValue(decodeKiCadEscapes(val));
        else if (keyL == "footprint") {
            const QString resolved = resolveFootprintName(val);
            def.setDefaultFootprint(resolved);
            if (footprintSource && !resolved.isEmpty()) *footprintSource = "Footprint property";
        }
        else if (keyL == "ki_fp_filters") def.setFootprintFilters(splitFpFilters(val));
        else {
            const QString decoded = decodeKiCadEscapes(val);
            def.setCustomField(key, decoded);
            if (fallbackFootprintFromField.isEmpty() &&
                isLikelyFootprintKey(key) &&
                isLikelyFootprintValue(decoded)) {
                fallbackFootprintFromField = decoded;
            }
        }
    }

    if (def.defaultFootprint().isEmpty() && !fallbackFootprintFromField.isEmpty()) {
        const QString resolved = resolveFootprintName(fallbackFootprintFromField);
        def.setDefaultFootprint(resolved);
        if (footprintSource && !resolved.isEmpty()) *footprintSource = "Custom footprint/package field";
    }
    if (def.defaultFootprint().isEmpty()) {
        const QString autoFromFilters = resolveFootprintFromFilters(def.footprintFilters());
        if (!autoFromFilters.isEmpty()) {
            def.setDefaultFootprint(autoFromFilters);
            if (footprintSource) *footprintSource = "ki_fp_filters match";
        }
    }

    // Parse nested symbols (graphics and pins live there)
    QRegularExpression subSymRe("\\(symbol\\s+\"([^\"]+)\"");
    QRegularExpressionMatchIterator ssi = subSymRe.globalMatch(symbolContent);
    int maxUnitSeen = qMax(1, def.unitCount());
    auto extractAt = [](const QString& src, const QString& key, int at) {
        const int s = findSExprStart(src, key, at);
        if (s < 0) return QString();
        return extractBalancedSExpr(src, s, nullptr);
    };
    while (ssi.hasNext()) {
        QRegularExpressionMatch match = ssi.next();
        int pos = match.capturedStart();
        QString subContent = extractAt(symbolContent, "symbol", pos);
        const QString subName = match.captured(1);

        int primUnit = 0;
        int primBodyStyle = 0;
        parseKiCadSubSymbolSuffix(subName, primUnit, primBodyStyle);
        if (primUnit > maxUnitSeen) maxUnitSeen = primUnit;

        auto applyScope = [&](SymbolPrimitive& prim) {
            prim.setUnit(primUnit);
            prim.setBodyStyle(primBodyStyle);
        };

        // Parse Polylines
        QRegularExpression polyRe("\\(polyline");
        QRegularExpressionMatchIterator polyMatch = polyRe.globalMatch(subContent);
        while (polyMatch.hasNext()) {
            int pPos = polyMatch.next().capturedStart();
            QString polyS = extractAt(subContent, "polyline", pPos);
            QList<QPointF> pts;
            QRegularExpression xyRe("\\(xy\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\)");
            QRegularExpressionMatchIterator xyi = xyRe.globalMatch(polyS);
            while (xyi.hasNext()) {
                QRegularExpressionMatch m = xyi.next();
                pts << QPointF(m.captured(1).toDouble() * KICAD_SCALE,
                               -m.captured(2).toDouble() * KICAD_SCALE); // Flip Y
            }
            const qreal lw = parseStrokeWidth(polyS, 1.5);
            if (pts.size() > 1) {
                for (int i = 0; i < pts.size() - 1; ++i) {
                    SymbolPrimitive p = SymbolPrimitive::createLine(pts[i], pts[i + 1]);
                    p.data["lineWidth"] = lw;
                    applyScope(p);
                    def.addPrimitive(p);
                }
            }
        }

        // Parse Rectangles
        QRegularExpression rectRe("\\(rectangle");
        QRegularExpressionMatchIterator rectMatch = rectRe.globalMatch(subContent);
        while (rectMatch.hasNext()) {
            int rPos = rectMatch.next().capturedStart();
            QString rectS = extractAt(subContent, "rectangle", rPos);
            QRegularExpression startEndRe("\\(start\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\)[\\s\\S]*?\\(end\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\)");
            QRegularExpressionMatch m = startEndRe.match(rectS);

            const QString fillType = parseFillType(rectS);
            const bool filled = (fillType == "background");
            const qreal lw = parseStrokeWidth(rectS, 1.5);

            if (m.hasMatch()) {
                qreal x1 = m.captured(1).toDouble() * KICAD_SCALE;
                qreal y1 = -m.captured(2).toDouble() * KICAD_SCALE;
                qreal x2 = m.captured(3).toDouble() * KICAD_SCALE;
                qreal y2 = -m.captured(4).toDouble() * KICAD_SCALE;
                SymbolPrimitive p = SymbolPrimitive::createRect(
                    QRectF(QPointF(x1, y1), QPointF(x2, y2)).normalized(), filled);
                p.data["lineWidth"] = lw;
                p.data["fillType"] = fillType;
                applyScope(p);
                def.addPrimitive(p);
            }
        }

        // Parse Circles
        QRegularExpression circRe("\\(circle");
        QRegularExpressionMatchIterator circMatch = circRe.globalMatch(subContent);
        while (circMatch.hasNext()) {
            int cPos = circMatch.next().capturedStart();
            QString circS = extractAt(subContent, "circle", cPos);
            QRegularExpression centerRe("\\(center\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\)[\\s\\S]*?\\(radius\\s+([\\-0-9.]+)\\)");
            QRegularExpressionMatch m = centerRe.match(circS);

            const QString fillType = parseFillType(circS);
            const bool filled = (fillType == "background");
            const qreal lw = parseStrokeWidth(circS, 1.5);

            if (m.hasMatch()) {
                qreal cx = m.captured(1).toDouble() * KICAD_SCALE;
                qreal cy = -m.captured(2).toDouble() * KICAD_SCALE;
                qreal r = m.captured(3).toDouble() * KICAD_SCALE;
                SymbolPrimitive p = SymbolPrimitive::createCircle(QPointF(cx, cy), r, filled);
                p.data["lineWidth"] = lw;
                p.data["fillType"] = fillType;
                applyScope(p);
                def.addPrimitive(p);
            }
        }

        // Parse Arcs
        QRegularExpression arcRe("\\(arc");
        QRegularExpressionMatchIterator arcMatch = arcRe.globalMatch(subContent);
        while (arcMatch.hasNext()) {
            int aPos = arcMatch.next().capturedStart();
            QString arcS = extractAt(subContent, "arc", aPos);

            QRegularExpression startRe("\\(start\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\)");
            QRegularExpression midRe("\\(mid\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\)");
            QRegularExpression endRe("\\(end\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\)");

            QRegularExpressionMatch mStart = startRe.match(arcS);
            QRegularExpressionMatch mMid = midRe.match(arcS);
            QRegularExpressionMatch mEnd = endRe.match(arcS);

            if (mStart.hasMatch() && mMid.hasMatch() && mEnd.hasMatch()) {
                qreal x1 = mStart.captured(1).toDouble() * KICAD_SCALE;
                qreal y1 = -mStart.captured(2).toDouble() * KICAD_SCALE;
                qreal x2 = mMid.captured(1).toDouble() * KICAD_SCALE;
                qreal y2 = -mMid.captured(2).toDouble() * KICAD_SCALE;
                qreal x3 = mEnd.captured(1).toDouble() * KICAD_SCALE;
                qreal y3 = -mEnd.captured(2).toDouble() * KICAD_SCALE;

                qreal D = 2 * (x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2));
                if (std::abs(D) > 1e-6) {
                    qreal cx = ((x1 * x1 + y1 * y1) * (y2 - y3) + (x2 * x2 + y2 * y2) * (y3 - y1) + (x3 * x3 + y3 * y3) * (y1 - y2)) / D;
                    qreal cy = ((x1 * x1 + y1 * y1) * (x3 - x2) + (x2 * x2 + y2 * y2) * (x1 - x3) + (x3 * x3 + y3 * y3) * (x2 - x1)) / D;
                    qreal r = std::hypot(x1 - cx, y1 - cy);

                    QRectF rect(cx - r, cy - r, 2 * r, 2 * r);

                    const qreal angStart = normDeg360(std::atan2(cy - y1, x1 - cx) * 180.0 / M_PI);
                    const qreal angMid = normDeg360(std::atan2(cy - y2, x2 - cx) * 180.0 / M_PI);
                    const qreal angEnd = normDeg360(std::atan2(cy - y3, x3 - cx) * 180.0 / M_PI);

                    const qreal ccwSE = ccwDelta(angStart, angEnd);
                    const qreal ccwSM = ccwDelta(angStart, angMid);
                    qreal spanDeg = 0.0;
                    // If mid is encountered going CCW start->end, keep positive span; otherwise CW.
                    if (ccwSM <= ccwSE + 1e-6) {
                        spanDeg = ccwSE;
                    } else {
                        spanDeg = -(360.0 - ccwSE);
                    }

                    SymbolPrimitive arcPrim;
                    arcPrim.type = SymbolPrimitive::Arc;
                    arcPrim.data["x"] = rect.x();
                    arcPrim.data["y"] = rect.y();
                    arcPrim.data["w"] = rect.width();
                    arcPrim.data["h"] = rect.height();
                    const int sa16 = qRound(angStart * 16.0);
                    const int sp16 = qRound(spanDeg * 16.0);
                    arcPrim.data["startAngle"] = sa16;
                    arcPrim.data["spanAngle"] = sp16;
                    // Keep legacy aliases for compatibility with older code paths.
                    arcPrim.data["start"] = sa16;
                    arcPrim.data["span"] = sp16;
                    arcPrim.data["lineWidth"] = parseStrokeWidth(arcS, 1.5);
                    applyScope(arcPrim);
                    def.addPrimitive(arcPrim);
                }
            }
        }

        // Parse Text
        QRegularExpression textRe("\\(text\\s+\"([^\"]*)\"");
        QRegularExpressionMatchIterator textMatch = textRe.globalMatch(subContent);
        while (textMatch.hasNext()) {
            QRegularExpressionMatch match = textMatch.next();
            int tPos = match.capturedStart();
            QString textS = extractAt(subContent, "text", tPos);

            QRegularExpression atRe("\\(at\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\s*([\\-0-9.]*)\\)");
            QRegularExpressionMatch mAt = atRe.match(textS);

            qreal textSize = 10.0;
            QRegularExpression sizeRe("\\(size\\s+([0-9.]+)\\s+([0-9.]+)\\)");
            QRegularExpressionMatch mSize = sizeRe.match(textS);
            qreal textMm = 1.27;
            if (mSize.hasMatch()) textMm = mSize.captured(1).toDouble();
            textSize = textMm * KICAD_SCALE;

            if (mAt.hasMatch()) {
                qreal x = mAt.captured(1).toDouble() * KICAD_SCALE;
                qreal y = -mAt.captured(2).toDouble() * KICAD_SCALE;
                QString txt = decodeKiCadEscapes(match.captured(1));
                QString hAlign, vAlign;
                qreal rotDeg = 0.0;
                parseKiCadTextStyle(textS, hAlign, vAlign, rotDeg);

                SymbolPrimitive textPrim;
                textPrim.type = SymbolPrimitive::Text;
                textPrim.data["x"] = x;
                textPrim.data["y"] = y;
                textPrim.data["text"] = txt;
                textPrim.data["size"] = textSize;
                textPrim.data["fontSize"] = qMax(1, qRound(textMm * KICAD_MM_TO_QT_PT));
                textPrim.data["hAlign"] = hAlign;
                textPrim.data["vAlign"] = vAlign;
                textPrim.data["rotation"] = rotDeg;
                applyScope(textPrim);
                def.addPrimitive(textPrim);
            }
        }

        // Parse Text Boxes (KiCad v7+)
        QRegularExpression textBoxRe("\\(text_box\\s+\"([^\"]*)\"");
        QRegularExpressionMatchIterator textBoxMatch = textBoxRe.globalMatch(subContent);
        while (textBoxMatch.hasNext()) {
            QRegularExpressionMatch match = textBoxMatch.next();
            int tbPos = match.capturedStart();
            QString tbS = extractAt(subContent, "text_box", tbPos);
            if (tbS.isEmpty()) continue;

            QRegularExpression startEndRe("\\(start\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\)[\\s\\S]*?\\(end\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\)");
            QRegularExpressionMatch m = startEndRe.match(tbS);
            if (!m.hasMatch()) continue;

            const qreal x1 = m.captured(1).toDouble() * KICAD_SCALE;
            const qreal y1 = -m.captured(2).toDouble() * KICAD_SCALE;
            const qreal x2 = m.captured(3).toDouble() * KICAD_SCALE;
            const qreal y2 = -m.captured(4).toDouble() * KICAD_SCALE;
            const QRectF box(QPointF(x1, y1), QPointF(x2, y2));
            const QPointF anchor = box.topLeft();

            QString txt = decodeKiCadEscapes(match.captured(1));
            QString hAlign, vAlign;
            qreal rotDeg = 0.0;
            parseKiCadTextStyle(tbS, hAlign, vAlign, rotDeg);

            qreal textSize = 10.0;
            QRegularExpression sizeRe("\\(size\\s+([0-9.]+)\\s+([0-9.]+)\\)");
            QRegularExpressionMatch mSize = sizeRe.match(tbS);
            qreal textMm = 1.27;
            if (mSize.hasMatch()) textMm = mSize.captured(1).toDouble();
            textSize = textMm * KICAD_SCALE;

            SymbolPrimitive textPrim;
            textPrim.type = SymbolPrimitive::Text;
            textPrim.data["x"] = anchor.x();
            textPrim.data["y"] = anchor.y();
            textPrim.data["text"] = txt;
            textPrim.data["size"] = textSize;
            textPrim.data["fontSize"] = qMax(1, qRound(textMm * KICAD_MM_TO_QT_PT));
            textPrim.data["hAlign"] = hAlign;
            textPrim.data["vAlign"] = vAlign;
            textPrim.data["rotation"] = rotDeg;
            textPrim.data["boxW"] = box.width();
            textPrim.data["boxH"] = box.height();
            applyScope(textPrim);
            def.addPrimitive(textPrim);
        }

        bool hidePinNames = false;
        int pnPos = subContent.indexOf("(pin_names");
        if (pnPos != -1) {
            QString pnS = extractAt(subContent, "pin_names", pnPos);
            if (pnS.contains("(hide yes)")) hidePinNames = true;
        }

        bool hidePinNumbers = false;
        int pnumPos = subContent.indexOf("(pin_numbers");
        if (pnumPos != -1) {
            QString pnumS = extractAt(subContent, "pin_numbers", pnumPos);
            if (pnumS.contains("(hide yes)")) hidePinNumbers = true;
        }

        // Parse Pins
        QRegularExpression pinRe("\\(pin\\s+[a-z_]+\\s+[a-z_]+");
        QRegularExpressionMatchIterator pinMatch = pinRe.globalMatch(subContent);
        while (pinMatch.hasNext()) {
            int pnPos = pinMatch.next().capturedStart();
            QString pinS = extractAt(subContent, "pin", pnPos);
            QRegularExpression headRe("^\\(pin\\s+([a-z_]+)\\s+([a-z_]+)");
            QRegularExpressionMatch mHead = headRe.match(pinS);

            QRegularExpression atRe("\\(at\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\s+([\\-0-9.]+)\\)");
            QRegularExpression nameRe("\\(name\\s+\"([^\"]*)\"([\\s\\S]*?)\\)");
            QRegularExpression numRe("\\(number\\s+\"([^\"]*)\"([\\s\\S]*?)\\)");
            QRegularExpression lenRe("\\(length\\s+([0-9.]+)\\)");

            QRegularExpressionMatch mAt = atRe.match(pinS);
            QRegularExpressionMatch mName = nameRe.match(pinS);
            QRegularExpressionMatch mNum = numRe.match(pinS);
            QRegularExpressionMatch mLen = lenRe.match(pinS);

            if (mAt.hasMatch()) {
                qreal x = mAt.captured(1).toDouble() * KICAD_SCALE;
                qreal y = -mAt.captured(2).toDouble() * KICAD_SCALE;
                qreal angle = mAt.captured(3).toDouble();
                qreal len = mLen.hasMatch() ? mLen.captured(1).toDouble() * KICAD_SCALE : 20.0;

                QString orient = "Right";
                if (angle == 180) orient = "Left";
                else if (angle == 90) orient = "Up";
                else if (angle == 270 || angle == -90) orient = "Down";

                QString pinNumStr = mNum.hasMatch() ? mNum.captured(1) : "0";
                QString pinNameStr = mName.hasMatch() ? mName.captured(1) : "";

                bool thisHideName = hidePinNames;
                if (mName.hasMatch() && mName.captured(2).contains("(hide yes)")) thisHideName = true;

                bool thisHideNum = hidePinNumbers;
                if (mNum.hasMatch() && mNum.captured(2).contains("(hide yes)")) thisHideNum = true;

                int pinNum = pinNumStr.toInt();

                SymbolPrimitive pin = SymbolPrimitive::createPin(QPointF(x, y), pinNum, pinNameStr, orient, len);
                pin.data["num"] = pinNumStr;
                pin.data["hideName"] = thisHideName;
                pin.data["hideNum"] = thisHideNum;
                if (mHead.hasMatch()) {
                    pin.data["electricalType"] = mapKiCadElectricalType(mHead.captured(1));
                    pin.data["pinShape"] = mapKiCadPinShape(mHead.captured(2));
                }
                if (pinS.contains("(hide yes)")) {
                    pin.data["visible"] = false;
                }
                applyScope(pin);
                def.addPrimitive(pin);
            }
        }
    }

    mergeStackedPins(def);
    def.setUnitCount(qMax(def.unitCount(), maxUnitSeen));
    visiting.remove(symbolName);
    return def;
}
}

QStringList KicadSymbolImporter::getSymbolNames(const QString& filePath) {
    QStringList names;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return names;

    QTextStream in(&file);
    QString content = in.readAll();
    
    int from = 0;
    while (true) {
        int start = findSExprStart(content, "symbol", from);
        if (start < 0) break;

        int end = -1;
        QString sym = extractBalancedSExpr(content, start, &end);
        if (sym.isEmpty()) break;

        QString name = parseSExprName(sym);
        if (name.isEmpty()) {
            from = std::max(start + 1, end + 1);
            continue;
        }

        // Exclude internal child symbols (they usually have _0_0 or similar suffixes)
        if (!name.contains(QRegularExpression("_[0-9]+_[0-9]+$"))) {
            names << name;
        }
        from = std::max(start + 1, end + 1);
    }

    names.removeDuplicates();
    return names;
}

SymbolDefinition KicadSymbolImporter::importSymbol(const QString& filePath, const QString& symbolName) {
    return importSymbolDetailed(filePath, symbolName).symbol;
}

KicadSymbolImporter::ImportResult KicadSymbolImporter::importSymbolDetailed(const QString& filePath, const QString& symbolName) {
    ImportResult out;
    out.symbol = SymbolDefinition(symbolName);
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return out;

    QTextStream in(&file);
    QString content = in.readAll();
    QSet<QString> visiting;
    out.symbol = importSymbolFromContent(content, symbolName, visiting, &out.footprintSource);
    out.detectedFootprint = out.symbol.defaultFootprint();
    return out;
}

QString KicadSymbolImporter::extractSExpr(const QString& content, const QString& key, int& from) {
    const int start = findSExprStart(content, key, from);
    if (start == -1) return QString();

    int end = -1;
    const QString expr = extractBalancedSExpr(content, start, &end);
    if (!expr.isEmpty() && end >= start) {
        from = end + 1;
    }
    return expr;
}
