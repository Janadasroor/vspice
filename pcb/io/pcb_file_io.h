#ifndef PCBFILEIO_H
#define PCBFILEIO_H

#include <QString>
#include <QGraphicsScene>
#include <QJsonObject>
#include <QJsonArray>
#include "../models/board_model.h"

class PCBFileIO {
public:
    // --- Headless Model-Based IO (Clean Architecture) ---
    static bool saveBoard(const Flux::Model::BoardModel* board, const QString& filePath);
    static bool loadBoard(Flux::Model::BoardModel* board, const QString& filePath);

    // --- UI-Based IO (Legacy/Convenience) ---
    static bool savePCB(QGraphicsScene* scene, const QString& filePath);
    static bool loadPCB(QGraphicsScene* scene, const QString& filePath);
    
    // --- Conversion Utilities ---
    static Flux::Model::BoardModel* sceneToModel(QGraphicsScene* scene);
    static void modelToScene(const Flux::Model::BoardModel* board, QGraphicsScene* scene);

    static QJsonObject serializeSceneToJson(QGraphicsScene* scene);
    static QString lastError();

private:
    static QJsonArray serializeItems(QGraphicsScene* scene);
    static bool deserializeItems(QGraphicsScene* scene, const QJsonArray& itemsArray);
    
    static QString s_lastError;
    static const int FILE_FORMAT_VERSION = 1;
};

#endif // PCBFILEIO_H
