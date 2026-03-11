#ifndef SMARTSIGNALITEM_H
#define SMARTSIGNALITEM_H

#include "schematic_item.h"
#include <QStringList>

/**
 * @brief A programmable signal block defined by embedded Python logic.
 */
class SmartSignalItem : public SchematicItem {
public:
    SmartSignalItem(QPointF pos = QPointF(0, 0), QGraphicsItem* parent = nullptr);

    // SchematicItem interface
    QString itemTypeName() const override { return "SmartSignalBlock"; }
    ItemType itemType() const override { return SchematicItem::SmartSignalType; }
    QString referencePrefix() const override { return "SB"; }
    
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    
    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

    QList<QPointF> connectionPoints() const override;
    QString pinName(int index) const override;
    
    // Logic specific properties
    QString pythonCode() const { return m_pythonCode; }
    void setPythonCode(const QString& code) { 
        m_pythonCode = code; 
        QString ds = docstring();
        if (!ds.isEmpty()) setToolTip(ds);
        else setToolTip("Smart Signal Block: " + name());
        update(); 
    }

    QStringList inputPins() const { return m_inputPins; }
    void setInputPins(const QStringList& pins);

    QStringList outputPins() const { return m_outputPins; }
    void setOutputPins(const QStringList& pins);

    QMap<QString, double> parameters() const { return m_parameters; }
    void setParameter(const QString& name, double value) { m_parameters[name] = value; update(); }
    void setParameters(const QMap<QString, double>& params) { m_parameters = params; update(); }

    struct TestCase {
        double time;
        QMap<QString, double> inputs;
        QMap<QString, double> expectedOutputs;
        QString name;
    };
    QList<TestCase> testCases() const { return m_testCases; }
    void setTestCases(const QList<TestCase>& cases) { m_testCases = cases; }

    struct Snapshot {
        QString name;
        QString code;
        QString timestamp;
    };
    QList<Snapshot> snapshots() const { return m_snapshots; }
    void setSnapshots(const QList<Snapshot>& snapshots) { m_snapshots = snapshots; }

    void setPreviewData(const QVector<QPointF>& points) { m_previewPoints = points; update(); }

    QString docstring() const;

private:
    void updateSize();

    QString m_pythonCode;
    QStringList m_inputPins;
    QStringList m_outputPins;
    QMap<QString, double> m_parameters;
    QList<TestCase> m_testCases;
    QList<Snapshot> m_snapshots;
    QSizeF m_size;
    QVector<QPointF> m_previewPoints;
};

#endif // SMARTSIGNALITEM_H
