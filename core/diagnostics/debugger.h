#ifndef DEBUGGER_H
#define DEBUGGER_H

#include <QObject>
#include <QSet>
#include <QMap>
#include <QString>

enum class DebugState {
    Stopped,
    Running,
    Paused
};

class Debugger : public QObject {
    Q_OBJECT
public:
    static Debugger& instance();

    void setBreakpoint(int line);
    void removeBreakpoint(int line);
    bool hasBreakpoint(int line) const;
    void clearBreakpoints();
    const QSet<int>& breakpoints() const { return m_breakpoints; }

    DebugState state() const { return m_state; }
    void setState(DebugState s);

    int activeLine() const { return m_activeLine; }
    void setActiveLine(int line);

    void step();
    void resume();
    void stop();

signals:
    void stateChanged(DebugState newState);
    void breakpointsChanged();
    void activeLineChanged(int line);
    void stepRequested();

private:
    Debugger(QObject* parent = nullptr);
    
    QSet<int> m_breakpoints;
    DebugState m_state = DebugState::Stopped;
    int m_activeLine = -1;
};

#endif // DEBUGGER_H
