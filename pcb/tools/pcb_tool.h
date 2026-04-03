#ifndef PCBTOOL_H
#define PCBTOOL_H

#include <QObject>
#include <QPointF>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCursor>
#include <QString>

class PCBView;
class PCBItem;

class PCBTool : public QObject {
    Q_OBJECT

public:
    explicit PCBTool(const QString& name, QObject* parent = nullptr);
    virtual ~PCBTool() = default;

    QString name() const { return m_name; }

    // Tool lifecycle
    virtual void activate(PCBView* view) { m_view = view; }
    virtual void deactivate() { m_view = nullptr; }

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
    
    // Default properties for the tool itself
    virtual QMap<QString, QVariant> toolProperties() const { return {}; }
    virtual void setToolProperty(const QString& name, const QVariant& value) { Q_UNUSED(name); Q_UNUSED(value); }

    // Cursor
    virtual QCursor cursor() const { return QCursor(Qt::ArrowCursor); }

protected:
    PCBView* view() const { return m_view; }
    void setView(PCBView* view) { m_view = view; }

private:
    QString m_name;
    PCBView* m_view;
};

#endif // PCBTOOL_H
