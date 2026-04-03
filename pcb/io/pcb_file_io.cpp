#include "pcb_file_io.h"
#include "core/diagnostics/runtime_diagnostics.h"
#include "../factories/pcb_item_factory.h"
#include "../items/pcb_item.h"
#include "../items/trace_item.h"
#include "../items/via_item.h"
#include "../items/pad_item.h"
#include "../items/component_item.h"
#include "../items/copper_pour_item.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QFileInfo>
#include <QDebug>
#include <QDir>

using namespace Flux::Model;

QString PCBFileIO::s_lastError;

// --- CLEAN HEADLESS IO ---

bool PCBFileIO::saveBoard(const BoardModel* board, const QString& filePath) {
    if (!board) {
        s_lastError = "Invalid board model pointer";
        return false;
    }

    QJsonObject root = board->toJson();
    QJsonDocument doc(root);
    
    QFile file(filePath);
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        s_lastError = QString("Failed to open file for writing: %1").arg(file.errorString());
        return false;
    }

    qint64 bytesWritten = file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    if (bytesWritten < 0) {
        s_lastError = "Failed to write data to disk";
        return false;
    }

    return true;
}

bool PCBFileIO::loadBoard(BoardModel* board, const QString& filePath) {
    if (!board) {
        s_lastError = "Invalid board model pointer";
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        s_lastError = QString("Failed to open file: %1").arg(file.errorString());
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        s_lastError = QString("JSON parse error: %1").arg(parseError.errorString());
        return false;
    }

    board->fromJson(doc.object());
    return true;
}

// --- CONVERSION LOGIC ---

BoardModel* PCBFileIO::sceneToModel(QGraphicsScene* scene) {
    if (!scene) return nullptr;

    BoardModel* board = new BoardModel();
    board->setName("Scene Export");

    for (QGraphicsItem* qItem : scene->items()) {
        // Only process top-level items to avoid double-serializing pads
        if (qItem->parentItem() != nullptr) continue;

        if (TraceItem* item = dynamic_cast<TraceItem*>(qItem)) {
            TraceModel* tm = new TraceModel();
            tm->fromJson(item->toJson());
            board->addTrace(tm);
        } else if (ViaItem* item = dynamic_cast<ViaItem*>(qItem)) {
            ViaModel* vm = new ViaModel();
            vm->fromJson(item->toJson());
            board->addVia(vm);
        } else if (PadItem* item = dynamic_cast<PadItem*>(qItem)) {
            PadModel* pm = new PadModel();
            pm->fromJson(item->toJson());
            board->addPad(pm);
        } else if (ComponentItem* item = dynamic_cast<ComponentItem*>(qItem)) {
            ComponentModel* cm = new ComponentModel();
            cm->fromJson(item->toJson());
            board->addComponent(cm);
        } else if (CopperPourItem* item = dynamic_cast<CopperPourItem*>(qItem)) {
            CopperPourModel* cpm = new CopperPourModel();
            cpm->fromJson(item->toJson());
            board->addCopperPour(cpm);
        }
    }
    return board;
}

#include "../analysis/pcb_ratsnest_manager.h"

void PCBFileIO::modelToScene(const BoardModel* board, QGraphicsScene* scene) {
    if (!board || !scene) return;

    // Clear ratsnest manager first so it doesn't hold dangling pointers to items about to be deleted by scene->clear()
    PCBRatsnestManager::instance().clearRatsnest();
    scene->clear();

    for (auto* tm : board->traces()) {
        TraceModel* clone = new TraceModel();
        clone->fromJson(tm->toJson());
        TraceItem* item = new TraceItem(clone);
        item->setOwned(true); // Ensure item knows it owns the model now
        scene->addItem(item);
    }
    for (auto* vm : board->vias()) {
        ViaModel* clone = new ViaModel();
        clone->fromJson(vm->toJson());
        ViaItem* item = new ViaItem(clone);
        item->setOwned(true);
        scene->addItem(item);
    }
    for (auto* pm : board->pads()) {
        PadModel* clone = new PadModel();
        clone->fromJson(pm->toJson());
        PadItem* item = new PadItem(clone);
        item->setOwned(true);
        scene->addItem(item);
    }
    for (auto* cm : board->components()) {
        ComponentModel* clone = new ComponentModel();
        clone->fromJson(cm->toJson());
        ComponentItem* item = new ComponentItem(clone);
        item->setOwned(true);
        scene->addItem(item);
    }
    for (auto* cpm : board->copperPours()) {
        CopperPourModel* clone = new CopperPourModel();
        clone->fromJson(cpm->toJson());
        CopperPourItem* item = new CopperPourItem(clone);
        item->setOwned(true);
        scene->addItem(item);
    }
    
    // Regenerate ratsnest and copper pours once all items are back in the scene
    PCBRatsnestManager::instance().update();
    for (auto* item : scene->items()) {
        if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
            pour->rebuild();
            pour->update();
        }
    }
}

// --- LEGACY/UI COMPATIBILITY (Refactored to use headless IO internally) ---

bool PCBFileIO::savePCB(QGraphicsScene* scene, const QString& filePath) {
    BoardModel* board = sceneToModel(scene);
    bool result = saveBoard(board, filePath);
    delete board;
    return result;
}

bool PCBFileIO::loadPCB(QGraphicsScene* scene, const QString& filePath) {
    BoardModel board;
    if (loadBoard(&board, filePath)) {
        modelToScene(&board, scene);
        return true;
    }
    return false;
}

QJsonObject PCBFileIO::serializeSceneToJson(QGraphicsScene* scene) {
    BoardModel* board = sceneToModel(scene);
    QJsonObject root = board->toJson();
    delete board;
    return root;
}

QString PCBFileIO::lastError() {
    return s_lastError;
}

QJsonArray PCBFileIO::serializeItems(QGraphicsScene* scene) {
    // Deprecated but kept for compatibility
    return serializeSceneToJson(scene)["items"].toArray();
}

bool PCBFileIO::deserializeItems(QGraphicsScene* scene, const QJsonArray& itemsArray) {
    // Deprecated but kept for compatibility
    QJsonObject root;
    root["items"] = itemsArray;
    BoardModel board;
    board.fromJson(root);
    modelToScene(&board, scene);
    return true;
}
