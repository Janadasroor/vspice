#include "schematic_api.h"
#include "schematic_commands.h"
#include "../factories/schematic_item_factory.h"
#include "../io/schematic_file_io.h"
#include "../analysis/schematic_erc.h"
#include "../analysis/schematic_annotator.h"
#include "../../simulator/bridge/sim_schematic_bridge.h"
#include "../items/wire_item.h"
#include <QDebug>
#include <QFileInfo>

SchematicAPI::SchematicAPI(QGraphicsScene* scene, QUndoStack* undoStack, QObject* parent)
    : QObject(parent), m_scene(scene), m_undoStack(undoStack) {
}

int SchematicAPI::executeBatch(const QJsonArray& commands) {
    int count = 0;
    for (const auto& val : commands) {
        if (executeCommand(val.toObject())) {
            count++;
        }
    }
    return count;
}

bool SchematicAPI::executeCommand(const QJsonObject& command) {
    QString cmd = command["cmd"].toString();
    if (cmd == "addComponent") {
        QString type = command["type"].toString();
        double x = command["x"].toDouble();
        double y = command["y"].toDouble();
        QString ref = command["reference"].toString();
        QJsonObject props = command["properties"].toObject();
        return addComponent(type, QPointF(x, y), ref, props);
    } else if (cmd == "addWire") {
        QJsonArray ptsArr = command["points"].toArray();
        QList<QPointF> points;
        for (const auto& p : ptsArr) {
            QJsonObject ptObj = p.toObject();
            points << QPointF(ptObj["x"].toDouble(), ptObj["y"].toDouble());
        }
        return addWire(points);
    } else if (cmd == "connect") {
        QString ref1 = command["ref1"].toString();
        QString pin1 = command["pin1"].toString();
        QString ref2 = command["ref2"].toString();
        QString pin2 = command["pin2"].toString();
        return connect(ref1, pin1, ref2, pin2);
    } else if (cmd == "runERC") {
        QJsonArray results = runERC();
        if (results.isEmpty()) {
            qInfo() << "ERC: No issues found.";
        } else {
            qInfo() << "ERC: Found" << results.size() << "issues.";
            for (const auto& v : results) {
                QJsonObject obj = v.toObject();
                qInfo() << "  -" << obj["severity"].toString() << ":" << obj["message"].toString() << "at (" << obj["x"].toDouble() << "," << obj["y"].toDouble() << ")";
            }
        }
        return true;
    } else if (cmd == "annotate") {
        return annotate(command["resetAll"].toBool(false));
    } else if (cmd == "save") {
        return save(command["path"].toString());
    } else if (cmd == "load") {
        return load(command["path"].toString());
    } else if (cmd == "setProperty") {
        QString ref = command["reference"].toString();
        QString name = command["name"].toString();
        QVariant val = command["value"].toVariant();
        return setProperty(ref, name, val);
    }
    
    qWarning() << "Unknown API command:" << cmd;
    return false;
}

bool SchematicAPI::addComponent(const QString& type, const QPointF& pos, const QString& reference, const QJsonObject& properties) {
    auto& factory = SchematicItemFactory::instance();
    SchematicItem* item = factory.createItem(type, pos, properties);
    if (!item) {
        qWarning() << "Failed to create component of type:" << type;
        return false;
    }
    
    if (!reference.isEmpty()) {
        item->setReference(reference);
    }
    
    pushCommand(new AddItemCommand(m_scene, item));
    return true;
}

bool SchematicAPI::removeComponent(const QString& reference) {
    SchematicItem* item = findByReference(reference);
    if (!item) return false;
    
    pushCommand(new RemoveItemCommand(m_scene, {item}));
    return true;
}

bool SchematicAPI::setProperty(const QString& reference, const QString& name, const QVariant& value) {
    SchematicItem* item = findByReference(reference);
    if (!item) return false;
    
    QVariant oldValue;
    // Special handling for common properties
    if (name == "value") oldValue = item->value();
    else if (name == "reference") oldValue = item->reference();
    else if (name == "footprint") oldValue = item->footprint();
    else if (name == "name") oldValue = item->name();
    
    pushCommand(new ChangePropertyCommand(m_scene, item, name, oldValue, value));
    return true;
}

bool SchematicAPI::addWire(const QList<QPointF>& points) {
    if (points.size() < 2) return false;
    
    WireItem* wire = new WireItem(points.first(), points.last());
    wire->setPoints(points);
    
    pushCommand(new AddItemCommand(m_scene, wire));
    return true;
}

bool SchematicAPI::connect(const QString& ref1, const QString& pin1, const QString& ref2, const QString& pin2) {
    SchematicItem* item1 = findByReference(ref1);
    SchematicItem* item2 = findByReference(ref2);
    
    if (!item1 || !item2) return false;
    
    auto findPinPos = [](SchematicItem* item, const QString& pinSpec) -> QPointF {
        QList<QPointF> cpts = item->connectionPoints();
        bool isNumber;
        int idx = pinSpec.toInt(&isNumber) - 1; // 1-based to 0-based
        
        if (isNumber && idx >= 0 && idx < cpts.size()) {
            return item->mapToScene(cpts[idx]);
        }
        
        // Try to match by name
        for (int i = 0; i < cpts.size(); ++i) {
            if (item->pinName(i) == pinSpec) {
                return item->mapToScene(cpts[i]);
            }
        }
        
        return QPointF();
    };
    
    QPointF p1 = findPinPos(item1, pin1);
    QPointF p2 = findPinPos(item2, pin2);
    
    if (p1.isNull() || p2.isNull()) return false;
    
    // Simplest routing: L-shape
    QList<QPointF> points;
    points << p1;
    if (p1.x() != p2.x() && p1.y() != p2.y()) {
        points << QPointF(p2.x(), p1.y());
    }
    points << p2;
    
    return addWire(points);
}

QJsonArray SchematicAPI::runERC() {
    auto violations = SchematicERC::run(m_scene);
    QJsonArray result;
    for (const auto& v : violations) {
        QJsonObject obj;
        obj["severity"] = (v.severity == ERCViolation::Error) ? "Error" : 
                         (v.severity == ERCViolation::Critical) ? "Critical" : "Warning";
        obj["message"] = v.message;
        obj["x"] = v.position.x();
        obj["y"] = v.position.y();
        obj["net"] = v.netName;
        result.append(obj);
    }
    return result;
}

bool SchematicAPI::annotate(bool resetAll) {
    SchematicAnnotator::annotate(m_scene, resetAll);
    return true;
}

QJsonObject SchematicAPI::getNetlist() {
    SimNetlist netlist = SimSchematicBridge::buildNetlist(m_scene, nullptr);
    QJsonObject result;
    // ... basic netlist info
    return result;
}

bool SchematicAPI::load(const QString& path) {
    QString pageSize;
    TitleBlockData dummyTB;
    return SchematicFileIO::loadSchematic(m_scene, path, pageSize, dummyTB);
}

bool SchematicAPI::save(const QString& path) {
    return SchematicFileIO::saveSchematic(m_scene, path);
}

SchematicItem* SchematicAPI::findByReference(const QString& reference) const {
    for (auto* item : m_scene->items()) {
        if (auto* sItem = dynamic_cast<SchematicItem*>(item)) {
            if (sItem->reference() == reference) {
                return sItem;
            }
        }
    }
    return nullptr;
}

void SchematicAPI::pushCommand(QUndoCommand* cmd) {
    if (m_undoStack) {
        m_undoStack->push(cmd);
    } else {
        cmd->redo();
        // NOTE: In CLI mode, we might leak the command object if we don't delete it
        // but QUndoCommand ownership is tricky. For CLI we might just use redo and delete.
        delete cmd;
    }
}
