#include "debugger.h"

Debugger& Debugger::instance() {
    static Debugger inst;
    return inst;
}

Debugger::Debugger(QObject* parent) : QObject(parent) {}

void Debugger::setBreakpoint(int line) {
    if (!m_breakpoints.contains(line)) {
        m_breakpoints.insert(line);
        emit breakpointsChanged();
    }
}

void Debugger::removeBreakpoint(int line) {
    if (m_breakpoints.remove(line)) {
        emit breakpointsChanged();
    }
}

bool Debugger::hasBreakpoint(int line) const {
    return m_breakpoints.contains(line);
}

void Debugger::clearBreakpoints() {
    m_breakpoints.clear();
    emit breakpointsChanged();
}

void Debugger::setState(DebugState s) {
    if (m_state != s) {
        m_state = s;
        emit stateChanged(m_state);
    }
}

void Debugger::setActiveLine(int line) {
    if (m_activeLine != line) {
        m_activeLine = line;
        emit activeLineChanged(m_activeLine);
    }
}

void Debugger::step() {
    emit stepRequested();
}

void Debugger::resume() {
    setState(DebugState::Running);
}

void Debugger::stop() {
        setState(DebugState::Stopped);
        setActiveLine(-1);
    }
    
