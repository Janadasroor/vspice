#include "flux_script_panel.h"
#include "flux_code_editor.h"
#include "flux_completer.h"
#include <QGraphicsScene>
#include "flux/core/net_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QVariantMap>

namespace Flux {

ScriptPanel::ScriptPanel(QGraphicsScene* scene, NetManager* netManager, QWidget* parent)
    : QWidget(parent) {
    
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Toolbar
    QWidget* toolbar = new QWidget();
    toolbar->setStyleSheet("background: #2d2d30; border-bottom: 1px solid #3e3e42;");
    QHBoxLayout* toolLayout = new QHBoxLayout(toolbar);
    toolLayout->setContentsMargins(5, 5, 5, 5);

    QLabel* title = new QLabel("FluxScript Editor");
    title->setStyleSheet("color: #cccccc; font-weight: bold; font-size: 11px;");
    toolLayout->addWidget(title);
    toolLayout->addSpacing(20);

    // Debug Controls
    QPushButton* resumeBtn = new QPushButton("Resume");
    QPushButton* stepBtn = new QPushButton("Step");
    QPushButton* stopBtn = new QPushButton("Stop");
    
    QString debugBtnStyle = 
        "QPushButton { background: #3e3e42; color: #cccccc; border: none; padding: 4px 8px; border-radius: 2px; font-size: 11px; } "
        "QPushButton:hover { background: #4e4e52; } "
        "QPushButton:disabled { color: #555555; }";
    
    resumeBtn->setStyleSheet(debugBtnStyle);
    stepBtn->setStyleSheet(debugBtnStyle);
    stopBtn->setStyleSheet(debugBtnStyle);
    
    resumeBtn->setEnabled(false);
    stepBtn->setEnabled(false);
    stopBtn->setEnabled(false);

    toolLayout->addWidget(resumeBtn);
    toolLayout->addWidget(stepBtn);
    toolLayout->addWidget(stopBtn);
    
    toolLayout->addStretch();

    QPushButton* runBtn = new QPushButton("Run");
    runBtn->setStyleSheet(
        "QPushButton { "
        "background: #0e639c; color: white; border: none; padding: 4px 12px; border-radius: 2px; font-weight: bold; font-size: 11px; "
        "} "
        "QPushButton:hover { background: #1177bb; }"
    );
    toolLayout->addWidget(runBtn);

    layout->addWidget(toolbar);

    // Editor
    m_editor = new CodeEditor(scene, netManager, this);
    
    // Setup Completer
    FluxCompleter* completer = new FluxCompleter(this);
    completer->updateCompletions();
    m_editor->setCompleter(completer);
    
    layout->addWidget(m_editor);

    connect(runBtn, &QPushButton::clicked, [this]() {
        // Debugger::instance().start();
        m_editor->onRunRequested();
    });

    /* Legacy Debugger disabled
    connect(resumeBtn, &QPushButton::clicked, []() { Debugger::instance().resume(); });
    connect(stepBtn, &QPushButton::clicked, []() { Debugger::instance().step(); });
    connect(stopBtn, &QPushButton::clicked, []() { Debugger::instance().stop(); });

    connect(&Debugger::instance(), &Debugger::stateChanged, [=](DebugState state) {
        bool paused = (state == DebugState::Paused);
        bool stopped = (state == DebugState::Stopped);
        
        resumeBtn->setEnabled(paused);
        stepBtn->setEnabled(paused);
        stopBtn->setEnabled(!stopped);
        runBtn->setEnabled(stopped);
    });
    */
}

void ScriptPanel::setScript(const QString& code) {
    if (m_editor) {
        m_editor->setPlainText(code);
    }
}

} // namespace Flux
