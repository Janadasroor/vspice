#pragma once

#include <QIcon>
#include <QString>

class QKeyEvent;
class QMouseEvent;

class ITool {
public:
    virtual ~ITool() = default;

    virtual QString toolId() const = 0;
    virtual QString displayName() const = 0;
    virtual QIcon icon() const = 0;

    virtual void activate() = 0;
    virtual void deactivate() = 0;

    virtual bool onMousePress(QMouseEvent* event) = 0;
    virtual bool onMouseMove(QMouseEvent* event) = 0;
    virtual bool onMouseRelease(QMouseEvent* event) = 0;
    virtual bool onKeyPress(QKeyEvent* event) = 0;
};
