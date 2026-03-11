#ifndef VISUAL_SPICE_MAPPER_DIALOG_H
#define VISUAL_SPICE_MAPPER_DIALOG_H

#include <QDialog>
#include <QMap>
#include <QStringList>
#include "../../symbols/models/symbol_definition.h"

using Flux::Model::SymbolDefinition;

class VisualSpiceMapperDialog : public QDialog {
    Q_OBJECT

public:
    explicit VisualSpiceMapperDialog(const SymbolDefinition& symbol, QWidget* parent = nullptr);
    ~VisualSpiceMapperDialog();

    QMap<int, QString> nodeMapping() const { return m_mapping; }
    QString spiceModelName() const;

private slots:
    void onAccept();

private:
    void setupUI();
    
    SymbolDefinition m_symbol;
    QMap<int, QString> m_mapping;
    
    class SpiceMappingWidget* m_mapperWidget;
    class QLineEdit* m_modelNameEdit;
};

// Internal widget for visual mapping
class SpiceMappingWidget : public QWidget {
    Q_OBJECT
public:
    explicit SpiceMappingWidget(const QStringList& pins, int nodeCount, QWidget* parent = nullptr);
    
    void setMapping(const QMap<int, QString>& mapping);
    QMap<int, QString> mapping() const { return m_mapping; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QStringList m_pins;
    int m_nodeCount;
    QMap<int, QString> m_mapping; // Node Index -> Pin Name
    
    struct PinPoint { QRectF rect; QString name; };
    struct NodePoint { QRectF rect; int index; };
    
    QList<PinPoint> m_pinPoints;
    QList<NodePoint> m_nodePoints;
    
    int m_activeNode = -1;
    QString m_activePin;
    bool m_isDragging = false;
    QPointF m_dragPos;

    void updateLayout();
};

#endif // VISUAL_SPICE_MAPPER_DIALOG_H
