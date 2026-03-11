#pragma once

#include "idocument_io.h"

class QGraphicsScene;

class IPCBIOBridge : public IDocumentIO {
public:
    ~IPCBIOBridge() override = default;

    virtual bool loadPCB(QGraphicsScene* scene, const QString& filePath) = 0;
    virtual bool savePCB(QGraphicsScene* scene, const QString& filePath) = 0;
};
