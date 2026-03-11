#pragma once

#include "idocument_io.h"

class QGraphicsScene;

class ISchematicIOBridge : public IDocumentIO {
public:
    ~ISchematicIOBridge() override = default;

    virtual bool loadSchematic(QGraphicsScene* scene, const QString& filePath) = 0;
    virtual bool saveSchematic(QGraphicsScene* scene, const QString& filePath) = 0;
};
