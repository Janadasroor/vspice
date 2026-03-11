#ifndef SCHEMATICTOOL_H
#define SCHEMATICTOOL_H

#include <QObject>
#include <QPointF>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCursor>
#include <QCursor>
#include <QString>

class SchematicView;
class SchematicItem;

class SchematicTool : public QObject {
    Q_OBJECT

public:
    explicit SchematicTool(const QString& name, QObject* parent = nullptr);
    virtual ~SchematicTool() = default;

    QString name() const { return m_name; }

    // Snapping
    virtual QPointF snapPoint(QPointF scenePos);

    // Tool lifecycle
    virtual void activate(SchematicView* view);
    virtual void deactivate();

    // Event handling
    virtual void mousePressEvent(QMouseEvent* event) { Q_UNUSED(event) }
    virtual void mouseMoveEvent(QMouseEvent* event) { Q_UNUSED(event) }
    virtual void mouseReleaseEvent(QMouseEvent* event) { Q_UNUSED(event) }
    virtual void mouseDoubleClickEvent(QMouseEvent* event) { Q_UNUSED(event) }
    virtual void wheelEvent(QWheelEvent* event) { Q_UNUSED(event) }
    virtual void keyPressEvent(QKeyEvent* event) { Q_UNUSED(event) }
    virtual void keyReleaseEvent(QKeyEvent* event) { Q_UNUSED(event) }

    // Tool properties
    virtual QString tooltip() const { return m_name; }
    virtual QString iconName() const { return QString(); }
    virtual bool isSelectable() const { return false; }

    // Cursor
    virtual QCursor cursor() const { return QCursor(Qt::ArrowCursor); }

    // Tool Properties
    virtual QMap<QString, QVariant> toolProperties() const { return {}; }
    virtual void setToolProperty(const QString& name, const QVariant& value) { Q_UNUSED(name) Q_UNUSED(value) }

protected:
    SchematicView* view() const { return m_view; }
    void setView(SchematicView* view) { m_view = view; }

private:
    QString m_name;
    SchematicView* m_view;
};

#endif // SCHEMATICTOOL_H
