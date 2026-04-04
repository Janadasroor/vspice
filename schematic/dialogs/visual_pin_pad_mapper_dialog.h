#ifndef VISUAL_PIN_PAD_MAPPER_DIALOG_H
#define VISUAL_PIN_PAD_MAPPER_DIALOG_H

#include <QDialog>
#include <QMap>
#include <QStringList>
#include <QWidget>
#include <QLabel>
#include <QPushButton>

class SchematicItem;

namespace Flux {
namespace Model {
class FootprintDefinition;
}
}

class PinPadMapperWidget : public QWidget {
    Q_OBJECT
public:
    explicit PinPadMapperWidget(QWidget* parent = nullptr);

    void setPinsAndPads(const QStringList& pins, const QStringList& pads);
    void setMapping(const QMap<QString, QString>& mapping);
    QMap<QString, QString> mapping() const { return m_mapping; }
    void clearMapping();
    void autoMap();

signals:
    void mappingChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    struct PinNode {
        QString name;
        QRectF rect;
    };

    QStringList m_pins;
    QStringList m_pads;
    QMap<QString, QString> m_mapping; // pinName -> padName
    QList<PinNode> m_pinNodes;
    QList<PinNode> m_padNodes;

    bool m_dragging = false;
    QString m_activePin;
    QPointF m_dragPos;

    void updateLayout();
    void assignPinToPad(const QString& pinName, const QString& padName);
};

class VisualPinPadMapperDialog : public QDialog {
    Q_OBJECT
public:
    explicit VisualPinPadMapperDialog(SchematicItem* item, const QString& footprintName, QWidget* parent = nullptr);

private slots:
    void onAutoMap();
    void onClear();
    void onAccept();
    void updateStatus();

private:
    void buildPinsAndPads();
    QStringList sortedPadNames(const Flux::Model::FootprintDefinition& def) const;

    SchematicItem* m_item = nullptr;
    QString m_footprintName;
    QStringList m_pinNames;
    QStringList m_padNames;

    PinPadMapperWidget* m_mapper = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_applyButton = nullptr;
};

#endif // VISUAL_PIN_PAD_MAPPER_DIALOG_H
