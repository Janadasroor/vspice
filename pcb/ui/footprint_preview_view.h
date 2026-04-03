#ifndef FOOTPRINT_PREVIEW_VIEW_H
#define FOOTPRINT_PREVIEW_VIEW_H

#include <QGraphicsView>
#include <QGraphicsScene>
#include "../../footprints/models/footprint_definition.h"

using Flux::Model::FootprintDefinition;

class FootprintPreviewView : public QGraphicsView {
    Q_OBJECT
public:
    explicit FootprintPreviewView(QWidget* parent = nullptr);
    void setFootprint(const FootprintDefinition& def);
    void clear();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    QGraphicsScene* m_scene;
};

#endif // FOOTPRINT_PREVIEW_VIEW_H
