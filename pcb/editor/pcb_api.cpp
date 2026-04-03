#include "pcb_api.h"
#include "pcb_commands.h"
#include "../factories/pcb_item_factory.h"
#include "../io/pcb_file_io.h"
#include "../drc/pcb_drc.h"
#include "../items/trace_item.h"
#include "../items/via_item.h"
#include "../items/component_item.h"
#include <QDebug>
#include <QFileInfo>
#include <QJsonDocument>

PCBAPI::PCBAPI(QGraphicsScene* scene, QUndoStack* undoStack, QObject* parent)
    : QObject(parent), m_scene(scene), m_undoStack(undoStack) {
}

int PCBAPI::executeBatch(const QJsonArray& commands) {
    int count = 0;
    for (const auto& val : commands) {
        if (executeCommand(val.toObject())) {
            count++;
        }
    }
    return count;
}

bool PCBAPI::executeCommand(const QJsonObject& command) {
    QString cmd = command["cmd"].toString();
    if (cmd == "addComponent") {
        QString footprint = command["footprint"].toString();
        double x = command["x"].toDouble();
        double y = command["y"].toDouble();
        QString ref = command["reference"].toString();
        QJsonObject props = command["properties"].toObject();
        return addComponent(footprint, QPointF(x, y), ref, props);
    } else if (cmd == "addTrace") {
        QJsonArray ptsArr = command["points"].toArray();
        QList<QPointF> points;
        for (const auto& p : ptsArr) {
            QJsonObject ptObj = p.toObject();
            points << QPointF(ptObj["x"].toDouble(), ptObj["y"].toDouble());
        }
        double width = command["width"].toDouble(0.2);
        int layer = command["layer"].toInt(0);
        return addTrace(points, width, layer);
    } else if (cmd == "addVia") {
        double x = command["x"].toDouble();
        double y = command["y"].toDouble();
        int fromLayer = command["fromLayer"].toInt(0);
        int toLayer = command["toLayer"].toInt(1);
        return addVia(QPointF(x, y), fromLayer, toLayer);
    } else if (cmd == "runDRC") {
        runDRC();
        return true;
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
    
    qWarning() << "Unknown PCB API command:" << cmd;
    return false;
}

bool PCBAPI::addComponent(const QString& footprint, const QPointF& pos, const QString& reference, const QJsonObject& properties) {
    auto& factory = PCBItemFactory::instance();
    PCBItem* item = factory.createItem("Component", pos, properties);
    if (!item) {
        qWarning() << "Failed to create PCB component";
        return false;
    }
    
    if (auto* comp = dynamic_cast<ComponentItem*>(item)) {
        if (!footprint.isEmpty()) comp->setComponentType(footprint);
        if (!reference.isEmpty()) comp->model()->setName(reference);
    }
    
    pushCommand(new PCBAddItemCommand(m_scene, item));
    return true;
}

bool PCBAPI::removeComponent(const QString& reference) {
    PCBItem* item = findByReference(reference);
    if (!item) return false;
    
    pushCommand(new PCBRemoveItemCommand(m_scene, {item}));
    return true;
}

bool PCBAPI::setProperty(const QString& reference, const QString& name, const QVariant& value) {
    PCBItem* item = findByReference(reference);
    if (!item) return false;
    
    QVariant oldValue;
    pushCommand(new PCBPropertyCommand(m_scene, item, name, oldValue, value));
    return true;
}

bool PCBAPI::addTrace(const QList<QPointF>& points, double width, int layer) {
    if (points.size() < 2) return false;
    
    if (m_undoStack) m_undoStack->beginMacro("Add Multi-segment Trace");
    
    for (int i = 0; i < points.size() - 1; ++i) {
        TraceItem* trace = new TraceItem(points[i], points[i+1], width);
        trace->setLayer(layer);
        pushCommand(new PCBAddItemCommand(m_scene, trace));
    }
    
    if (m_undoStack) m_undoStack->endMacro();
    
    return true;
}

bool PCBAPI::addVia(const QPointF& pos, int fromLayer, int toLayer) {
    ViaItem* via = new ViaItem(pos);
    via->setStartLayer(fromLayer);
    via->setEndLayer(toLayer);
    
    pushCommand(new PCBAddItemCommand(m_scene, via));
    return true;
}

QJsonArray PCBAPI::runDRC() {
    PCBDRC drc;
    drc.runFullCheck(m_scene);
    auto violations = drc.violations();
    QJsonArray result;
    for (const auto& v : violations) {
        QJsonObject obj;
        obj["severity"] = "Error";
        obj["message"] = v.message();
        obj["x"] = v.location().x();
        obj["y"] = v.location().y();
        result.append(obj);
        
        qInfo() << "DRC:" << v.message() << "at (" << v.location().x() << "," << v.location().y() << ")";
    }
    return result;
}

bool PCBAPI::load(const QString& path) {
    return PCBFileIO::loadPCB(m_scene, path);
}

bool PCBAPI::save(const QString& path) {
    return PCBFileIO::savePCB(m_scene, path);
}

PCBItem* PCBAPI::findByReference(const QString& reference) const {
    for (auto* item : m_scene->items()) {
        if (auto* pItem = dynamic_cast<PCBItem*>(item)) {
            if (auto* comp = dynamic_cast<ComponentItem*>(pItem)) {
                if (comp->model()->name() == reference) {
                    return pItem;
                }
            }
        }
    }
    return nullptr;
}

void PCBAPI::pushCommand(QUndoCommand* cmd) {
    if (m_undoStack) {
        m_undoStack->push(cmd);
    } else {
        cmd->redo();
        delete cmd;
    }
}
