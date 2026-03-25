#include "ltspice_asc_importer.h"

#include "schematic_item_factory.h"
#include "schematic_item_registry.h"
#include "wire_item.h"
#include "net_label_item.h"
#include "power_item.h"
#include "schematic_text_item.h"
#include "schematic_spice_directive_item.h"
#include "hierarchical_port_item.h"
#include "schematic_shape_item.h"
#include "symbol_library.h"

#include <QFile>
#include <QGraphicsScene>
#include <QRegularExpression>
#include <QTextStream>

namespace {

struct PendingSymbol {
    QString rawName;
    QPointF pos;
    QString orientation;
    QMap<QString, QString> attrs;
};

struct OrientationInfo {
    qreal rotation = 0.0;
    bool mirrored = false;
};

QString takeToken(QStringView& view) {
    while (!view.isEmpty() && view.front().isSpace()) {
        view = view.sliced(1);
    }
    if (view.isEmpty()) return QString();
    int i = 0;
    while (i < view.size() && !view.at(i).isSpace()) ++i;
    const QString token = view.left(i).toString();
    view = (i < view.size()) ? view.sliced(i) : QStringView();
    return token;
}

QString unquote(const QString& text) {
    const QString t = text.trimmed();
    if (t.size() >= 2 && t.startsWith('"') && t.endsWith('"')) {
        return t.mid(1, t.size() - 2);
    }
    return t;
}

bool tryParseInt(const QString& token, int& out) {
    bool ok = false;
    const int v = token.toInt(&ok);
    if (!ok) return false;
    out = v;
    return true;
}

OrientationInfo parseOrientation(const QString& token) {
    OrientationInfo info;
    const QString t = token.trimmed().toUpper();
    if (t.startsWith('R')) {
        bool ok = false;
        const qreal rot = t.mid(1).toDouble(&ok);
        if (ok) info.rotation = rot;
    } else if (t.startsWith('M')) {
        info.mirrored = true;
        bool ok = false;
        const qreal rot = t.mid(1).toDouble(&ok);
        if (ok) info.rotation = rot;
    }
    return info;
}

QString symbolBaseName(QString raw) {
    raw = raw.trimmed();
    raw.replace('\\', '/');
    const int slash = raw.lastIndexOf('/');
    if (slash >= 0 && slash + 1 < raw.size()) {
        raw = raw.mid(slash + 1);
    }
    return raw.trimmed();
}

QString resolveLibrarySymbolCaseInsensitive(const QString& query) {
    const QString q = query.trimmed();
    if (q.isEmpty()) return QString();
    const QString qLower = q.toLower();
    for (SymbolLibrary* lib : SymbolLibraryManager::instance().libraries()) {
        if (!lib) continue;
        for (const QString& candidate : lib->symbolNames()) {
            if (candidate.compare(q, Qt::CaseInsensitive) == 0) {
                return candidate;
            }
            if (candidate.toLower() == qLower) {
                return candidate;
            }
        }
    }
    return QString();
}

QString resolveLtspiceSymbolToType(const QString& rawName) {
    static const QMap<QString, QString> aliases = {
        {"res", "Resistor"},
        {"rn55upright", "Resistor"},
        {"uprightpowerresistor", "Resistor"},
        {"cap", "Capacitor_NonPolar"},
        {"smcap", "Capacitor_NonPolar"},
        {"polcap", "Capacitor_Polarized"},
        {"ind", "Inductor"},
        {"ind2", "Inductor"},
        {"voltage", "voltage"},
        {"current", "current"},
        {"diode", "Diode"},
        {"smdiode", "Diode"},
        {"schottky", "Diode"},
        {"zener", "Diode"},
        {"varactor", "Diode"},
        {"tvsdiode", "Diode"},
        {"led", "LED"},
        {"sw", "sw"},
        {"npn", "npn"},
        {"npn2", "npn2"},
        {"npn3", "npn3"},
        {"npn4", "npn4"},
        {"pnp", "pnp"},
        {"pnp2", "pnp2"},
        {"pnp4", "pnp4"},
        {"nmos", "nmos"},
        {"nmos4", "nmos4"},
        {"pmos", "pmos"},
        {"pmos4", "pmos4"},
        {"njf", "njf"},
        {"pjf", "pjf"},
        {"mesfet", "mesfet"},
        {"bv", "bv"},
        {"bi", "bi"},
        {"e", "e"},
        {"e2", "e2"},
        {"g", "g"},
        {"g2", "g2"},
        {"f", "f"},
        {"h", "h"},
        {"tline", "tline"},
        {"ltline", "ltline"}
    };

    auto& factory = SchematicItemFactory::instance();

    auto tryResolve = [&](const QString& candidate) -> QString {
        if (candidate.isEmpty()) return QString();
        if (factory.isTypeRegistered(candidate)) return candidate;
        if (SymbolLibraryManager::instance().findSymbol(candidate)) return candidate;
        const QString libMatch = resolveLibrarySymbolCaseInsensitive(candidate);
        if (!libMatch.isEmpty()) return libMatch;
        return QString();
    };

    const QString raw = rawName.trimmed();
    const QString base = symbolBaseName(raw);
    const QString rawLower = raw.toLower();
    const QString baseLower = base.toLower();

    if (const QString resolved = tryResolve(raw); !resolved.isEmpty()) return resolved;
    if (const QString resolved = tryResolve(base); !resolved.isEmpty()) return resolved;

    if (aliases.contains(rawLower)) {
        if (const QString resolved = tryResolve(aliases.value(rawLower)); !resolved.isEmpty()) return resolved;
    }
    if (aliases.contains(baseLower)) {
        if (const QString resolved = tryResolve(aliases.value(baseLower)); !resolved.isEmpty()) return resolved;
    }

    if (const QString resolved = tryResolve(rawLower); !resolved.isEmpty()) return resolved;
    if (const QString resolved = tryResolve(baseLower); !resolved.isEmpty()) return resolved;

    const QString rawUpper = raw.toUpper();
    const QString baseUpper = base.toUpper();
    if (const QString resolved = tryResolve(rawUpper); !resolved.isEmpty()) return resolved;
    if (const QString resolved = tryResolve(baseUpper); !resolved.isEmpty()) return resolved;

    return QString();
}

void applyOrientation(SchematicItem* item, const QString& orientationToken) {
    if (!item) return;
    const OrientationInfo info = parseOrientation(orientationToken);
    item->setRotation(info.rotation);
    if (info.mirrored) {
        item->setMirroredY(true);
    }
}

void addWire(QGraphicsScene* scene, int x1, int y1, int x2, int y2) {
    if (!scene) return;
    auto* wire = new WireItem();
    wire->setPoints({QPointF(x1, y1), QPointF(x2, y2)});
    scene->addItem(wire);
}

void addFlag(QGraphicsScene* scene, int x, int y, const QString& netName) {
    if (!scene) return;
    const QString net = unquote(netName).trimmed();
    if (net == "0") {
        auto* gnd = new PowerItem(QPointF(x, y), PowerItem::GND);
        scene->addItem(gnd);
        return;
    }
    if (net.isEmpty()) return;
    auto* label = new NetLabelItem(QPointF(x, y), net);
    scene->addItem(label);
}

void addDataFlag(QGraphicsScene* scene, int x, int y, const QString& rawLabel) {
    if (!scene) return;
    const QString label = unquote(rawLabel);
    if (label.isEmpty()) return;
    auto* item = new NetLabelItem(QPointF(x, y), label);
    scene->addItem(item);
}

void addText(QGraphicsScene* scene,
             int x,
             int y,
             const QString& alignToken,
             const QString& rawPayload) {
    if (!scene) return;
    QString payload = rawPayload;
    if (payload.startsWith('!')) {
        auto* spice = new SchematicSpiceDirectiveItem(payload.mid(1), QPointF(x, y));
        scene->addItem(spice);
        return;
    }

    if (payload.startsWith(';')) {
        payload = payload.mid(1);
    }

    auto* text = new SchematicTextItem(payload, QPointF(x, y));
    const QString a = alignToken.toLower();
    if (a == "right") text->setAlignment(Qt::AlignRight);
    else if (a == "center") text->setAlignment(Qt::AlignHCenter);
    else text->setAlignment(Qt::AlignLeft);
    scene->addItem(text);
}

void addPort(QGraphicsScene* scene, int x, int y, const QString& dirToken, const QString& labelToken) {
    if (!scene) return;
    HierarchicalPortItem::PortType type = HierarchicalPortItem::Passive;
    const QString d = dirToken.toLower();
    if (d == "in") type = HierarchicalPortItem::Input;
    else if (d == "out") type = HierarchicalPortItem::Output;
    else if (d == "bidir") type = HierarchicalPortItem::Bidirectional;
    const QString label = labelToken.trimmed().isEmpty() ? QString("PORT") : unquote(labelToken).trimmed();
    auto* port = new HierarchicalPortItem(QPointF(x, y), label, type);
    scene->addItem(port);
}

void addRectOrCircle(QGraphicsScene* scene, bool circle, const QList<int>& nums, const QStringList& tokens) {
    if (!scene || nums.size() < 4) return;
    auto* shape = new SchematicShapeItem(
        circle ? SchematicShapeItem::Circle : SchematicShapeItem::Rectangle,
        QPointF(nums[0], nums[1]),
        QPointF(nums[2], nums[3]));
    if (!tokens.isEmpty()) {
        int width = 0;
        if (tryParseInt(tokens.last(), width) && width > 0) {
            QPen pen = shape->pen();
            pen.setWidthF(qMax(1, width));
            shape->setPen(pen);
        }
    }
    scene->addItem(shape);
}

void addLine(QGraphicsScene* scene, const QList<int>& nums, const QStringList& tokens) {
    if (!scene || nums.size() < 4) return;
    auto* shape = new SchematicShapeItem(SchematicShapeItem::Line,
                                         QPointF(nums[0], nums[1]),
                                         QPointF(nums[2], nums[3]));
    if (!tokens.isEmpty()) {
        int width = 0;
        if (tryParseInt(tokens.last(), width) && width > 0) {
            QPen pen = shape->pen();
            pen.setWidthF(qMax(1, width));
            shape->setPen(pen);
        }
    }
    scene->addItem(shape);
}

void addArcFallback(QGraphicsScene* scene, const QList<int>& nums) {
    if (!scene || nums.size() < 8) return;
    auto* shape = new SchematicShapeItem(SchematicShapeItem::Line,
                                         QPointF(nums[4], nums[5]),
                                         QPointF(nums[6], nums[7]));
    scene->addItem(shape);
}

void finalizePendingSymbol(QGraphicsScene* scene, const PendingSymbol& pending) {
    if (!scene || pending.rawName.trimmed().isEmpty()) return;

    const QString resolvedType = resolveLtspiceSymbolToType(pending.rawName);
    QJsonObject props;
    if (pending.attrs.contains("InstName")) {
        props["reference"] = pending.attrs.value("InstName");
    }

    QString value = pending.attrs.value("Value").trimmed();
    const QString value2 = pending.attrs.value("Value2").trimmed();
    if (value.startsWith('"') && value.endsWith('"')) {
        value = unquote(value);
    }
    if (value.isEmpty()) value = value2;
    if (!value.isEmpty()) props["value"] = value;

    SchematicItem* item = nullptr;
    auto& factory = SchematicItemFactory::instance();
    if (!resolvedType.isEmpty()) {
        item = factory.createItem(resolvedType, pending.pos, props);
    }

    if (!item) {
        QJsonObject fallbackProps = props;
        if (!fallbackProps.contains("value")) {
            fallbackProps["value"] = symbolBaseName(pending.rawName);
        }
        item = factory.createItem("IC", pending.pos, fallbackProps);
    }

    if (!item) return;

    applyOrientation(item, pending.orientation);

    for (auto it = pending.attrs.begin(); it != pending.attrs.end(); ++it) {
        const QString key = it.key();
        if (key == "InstName" || key == "Value" || key == "Value2") continue;
        item->setParamExpression(QString("ltspice.%1").arg(key), it.value());
    }

    scene->addItem(item);
}

QList<int> extractAllInts(const QStringList& tokens, int startIndex) {
    QList<int> values;
    for (int i = startIndex; i < tokens.size(); ++i) {
        int n = 0;
        if (tryParseInt(tokens[i], n)) values.append(n);
    }
    return values;
}

} // namespace

bool LtspiceAscImporter::importFile(QGraphicsScene* scene,
                                    const QString& filePath,
                                    QString& pageSize,
                                    QString* errorOut) {
    if (!scene) {
        if (errorOut) *errorOut = "Invalid scene";
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorOut) *errorOut = QString("Failed to open LTspice schematic: %1").arg(file.errorString());
        return false;
    }

    QTextStream stream(&file);
    stream.setAutoDetectUnicode(true);
    const QString content = stream.readAll();
    file.close();

    if (content.trimmed().isEmpty()) {
        if (errorOut) *errorOut = "LTspice schematic is empty";
        return false;
    }

    SchematicItemRegistry::registerBuiltInItems();
    scene->clear();

    PendingSymbol pending;
    bool hasPending = false;

    const QStringList lines = content.split(QRegularExpression("\\r?\\n"));
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) continue;

        QStringView view(line);
        const QString cmd = takeToken(view);
        if (cmd.isEmpty()) continue;

        if (cmd == "SYMATTR") {
            if (!hasPending) continue;
            const QString key = takeToken(view);
            const QString value = view.toString().trimmed();
            if (!key.isEmpty()) pending.attrs[key] = value;
            continue;
        }

        if (cmd == "WINDOW") {
            continue;
        }

        if (hasPending) {
            finalizePendingSymbol(scene, pending);
            pending = PendingSymbol();
            hasPending = false;
        }

        if (cmd.compare("Version", Qt::CaseInsensitive) == 0) {
            continue;
        }

        if (cmd.compare("SHEET", Qt::CaseInsensitive) == 0) {
            QStringView sv(view);
            const QString sheetIdx = takeToken(sv);
            const QString wTok = takeToken(sv);
            const QString hTok = takeToken(sv);
            Q_UNUSED(sheetIdx);
            int w = 0;
            int h = 0;
            if (tryParseInt(wTok, w) && tryParseInt(hTok, h)) {
                pageSize = QString("LTspice:%1x%2").arg(w).arg(h);
            } else if (pageSize.isEmpty()) {
                pageSize = "A4";
            }
            continue;
        }

        if (cmd == "WIRE") {
            const QStringList tokens = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (tokens.size() >= 5) {
                int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
                if (tryParseInt(tokens[1], x1) && tryParseInt(tokens[2], y1) &&
                    tryParseInt(tokens[3], x2) && tryParseInt(tokens[4], y2)) {
                    addWire(scene, x1, y1, x2, y2);
                }
            }
            continue;
        }

        if (cmd == "FLAG") {
            const QStringList tokens = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (tokens.size() >= 4) {
                int x = 0;
                int y = 0;
                if (tryParseInt(tokens[1], x) && tryParseInt(tokens[2], y)) {
                    addFlag(scene, x, y, tokens.mid(3).join(" "));
                }
            }
            continue;
        }

        if (cmd == "DATAFLAG") {
            const QStringList tokens = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (tokens.size() >= 4) {
                int x = 0;
                int y = 0;
                if (tryParseInt(tokens[1], x) && tryParseInt(tokens[2], y)) {
                    addDataFlag(scene, x, y, tokens.mid(3).join(" "));
                }
            }
            continue;
        }

        if (cmd == "IOPIN") {
            const QStringList tokens = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (tokens.size() >= 4) {
                int x = 0;
                int y = 0;
                if (tryParseInt(tokens[1], x) && tryParseInt(tokens[2], y)) {
                    const QString label = tokens.size() > 4 ? tokens.mid(4).join(" ") : QString();
                    addPort(scene, x, y, tokens[3], label);
                }
            }
            continue;
        }

        if (cmd == "TEXT") {
            QStringView sv(view);
            const QString xTok = takeToken(sv);
            const QString yTok = takeToken(sv);
            const QString alignTok = takeToken(sv);
            const QString sizeTok = takeToken(sv);
            Q_UNUSED(sizeTok);
            int x = 0;
            int y = 0;
            if (tryParseInt(xTok, x) && tryParseInt(yTok, y)) {
                addText(scene, x, y, alignTok, sv.toString().trimmed());
            }
            continue;
        }

        if (cmd == "SYMBOL") {
            QStringView sv(view);
            const QString rawName = takeToken(sv);
            const QString xTok = takeToken(sv);
            const QString yTok = takeToken(sv);
            const QString orientTok = takeToken(sv);
            int x = 0;
            int y = 0;
            if (!rawName.isEmpty() && tryParseInt(xTok, x) && tryParseInt(yTok, y)) {
                pending.rawName = rawName;
                pending.pos = QPointF(x, y);
                pending.orientation = orientTok;
                pending.attrs.clear();
                hasPending = true;
            }
            continue;
        }

        if (cmd == "RECTANGLE") {
            const QStringList tokens = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            addRectOrCircle(scene, false, extractAllInts(tokens, 1), tokens);
            continue;
        }

        if (cmd == "CIRCLE") {
            const QStringList tokens = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            addRectOrCircle(scene, true, extractAllInts(tokens, 1), tokens);
            continue;
        }

        if (cmd == "LINE") {
            const QStringList tokens = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            addLine(scene, extractAllInts(tokens, 1), tokens);
            continue;
        }

        if (cmd == "ARC") {
            const QStringList tokens = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            addArcFallback(scene, extractAllInts(tokens, 1));
            continue;
        }
    }

    if (hasPending) {
        finalizePendingSymbol(scene, pending);
    }

    if (pageSize.isEmpty()) pageSize = "A4";
    return true;
}
