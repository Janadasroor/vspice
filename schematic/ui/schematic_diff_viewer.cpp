#include "schematic_diff_viewer.h"
#include "../io/schematic_file_io.h"
#include "../items/schematic_item.h"
#include <QHBoxLayout>
#include <QGraphicsRectItem>
#include <QScrollBar>

SchematicDiffViewer::SchematicDiffViewer(QWidget* parent) : QWidget(parent) {
    m_sceneA = new QGraphicsScene(this);
    m_sceneB = new QGraphicsScene(this);
    setupUI();
}

void SchematicDiffViewer::setupUI() {
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    m_viewA = new QGraphicsView(m_sceneA);
    m_viewB = new QGraphicsView(m_sceneB);

    m_viewA->setRenderHint(QPainter::Antialiasing);
    m_viewB->setRenderHint(QPainter::Antialiasing);
    
    // Modern Dark Theme for Diff
    m_viewA->setBackgroundBrush(QColor(30, 30, 35));
    m_viewB->setBackgroundBrush(QColor(30, 30, 35));

    layout->addWidget(m_viewA);
    layout->addWidget(m_viewB);

    // Sync views
    connect(m_viewA->horizontalScrollBar(), &QScrollBar::valueChanged, 
            m_viewB->horizontalScrollBar(), &QScrollBar::setValue);
    connect(m_viewA->verticalScrollBar(), &QScrollBar::valueChanged, 
            m_viewB->verticalScrollBar(), &QScrollBar::setValue);
}

void SchematicDiffViewer::setSchematics(const QJsonObject& jsonA, const QJsonObject& jsonB) {
    m_jsonA = jsonA;
    m_jsonB = jsonB;
    m_diffs = SchematicDiffEngine::compare(jsonA, jsonB);
    
    populateScenes();
    highlightDifferences();
}

void SchematicDiffViewer::populateScenes() {
    m_sceneA->clear();
    m_sceneB->clear();

    SchematicFileIO::loadSchematicFromJson(m_sceneA, m_jsonA);
    SchematicFileIO::loadSchematicFromJson(m_sceneB, m_jsonB);
    
    // Dim everything slightly to make highlights pop
    for (QGraphicsItem* item : m_sceneA->items()) item->setOpacity(0.6);
    for (QGraphicsItem* item : m_sceneB->items()) item->setOpacity(0.6);
}

void SchematicDiffViewer::highlightDifferences() {
    for (const auto& diff : m_diffs) {
        if (diff.type == SchematicDiffItem::Added) {
            // Highlight in B (Green)
            QRectF br;
            for (QGraphicsItem* gi : m_sceneB->items()) {
                if (auto* si = dynamic_cast<SchematicItem*>(gi)) {
                    if (si->id() == diff.itemId) {
                        si->setOpacity(1.0);
                        br = si->sceneBoundingRect();
                        break;
                    }
                }
            }
            auto* highlight = m_sceneB->addRect(br.adjusted(-5,-5,5,5), QPen(Qt::transparent), QBrush(QColor(0, 255, 0, 40)));
            highlight->setZValue(-1);
        }
        else if (diff.type == SchematicDiffItem::Removed) {
            // Highlight in A (Red)
            QRectF br;
            for (QGraphicsItem* gi : m_sceneA->items()) {
                if (auto* si = dynamic_cast<SchematicItem*>(gi)) {
                    if (si->id() == diff.itemId) {
                        si->setOpacity(1.0);
                        br = si->sceneBoundingRect();
                        break;
                    }
                }
            }
            auto* highlight = m_sceneA->addRect(br.adjusted(-5,-5,5,5), QPen(Qt::transparent), QBrush(QColor(255, 0, 0, 40)));
            highlight->setZValue(-1);
        }
        else if (diff.type == SchematicDiffItem::Modified) {
            // Highlight in both (Orange)
            for (QGraphicsItem* gi : m_sceneA->items()) {
                if (auto* si = dynamic_cast<SchematicItem*>(gi)) {
                    if (si->id() == diff.itemId) {
                        si->setOpacity(1.0);
                        m_sceneA->addRect(si->sceneBoundingRect().adjusted(-5,-5,5,5), QPen(QColor(255, 165, 0)), QBrush(QColor(255, 165, 0, 40)))->setZValue(-1);
                        break;
                    }
                }
            }
            for (QGraphicsItem* gi : m_sceneB->items()) {
                if (auto* si = dynamic_cast<SchematicItem*>(gi)) {
                    if (si->id() == diff.itemId) {
                        si->setOpacity(1.0);
                        m_sceneB->addRect(si->sceneBoundingRect().adjusted(-5,-5,5,5), QPen(QColor(255, 165, 0)), QBrush(QColor(255, 165, 0, 40)))->setZValue(-1);
                        break;
                    }
                }
            }
        }
    }
}
