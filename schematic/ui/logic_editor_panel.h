#ifndef LOGIC_EDITOR_PANEL_H
#define LOGIC_EDITOR_PANEL_H

#include <QMainWindow>
#include <QString>
#include <QGraphicsScene>
#include "../../core/diagnostics/debugger.h"
class NetManager;
class QLineEdit;
class QPushButton;
class QLabel;

namespace Flux {
    class CodeEditor;
}

class GeminiPanel;
class SmartSignalItem;

/**
 * @brief A standalone Mini-IDE for editing programmable logic blocks.
 */
class LogicEditorPanel : public QMainWindow {
    Q_OBJECT
public:
    explicit LogicEditorPanel(QGraphicsScene* scene, NetManager* netManager, QWidget* parent = nullptr);

    void setScene(QGraphicsScene* scene, class NetManager* netManager);
    void setTargetBlock(SmartSignalItem* item);
    bool isActive() const { return m_targetBlock != nullptr; }
    void flushEdits();

    /**
     * @brief Refreshes the list of available blocks in the project explorer.
     */
    void refreshExplorer();

protected:
    void closeEvent(class QCloseEvent* event) override;

signals:
    void closed();

private slots:
    void onCodeChanged();
    void onAiPromptReturn();
    void onApplyClicked();
    void updatePreview();
    void runLinter();
    void onLinterResult(const QString& output);
    void onExplorerItemClicked(class QListWidgetItem* item);
    void onTemplateDoubleClicked(class QListWidgetItem* item);
    void onPythonGenerated(const QString& code);
    void onBakeClicked();
    
    // Debugger Slots
    void onDebugStep();
    void onDebugResume();
    void onDebugStop();
    void onDebuggerStateChanged(DebugState state);
    void onDebuggerActiveLineChanged(int line);

    // Testing Slots
    void addTestCase();
    void removeTestCase();
    void runTests();
    
    // Snapshot Slots
    void onCaptureSnapshot();
    void onSnapshotDoubleClicked(class QListWidgetItem* item);
    void refreshSnapshots();

    // Pin Manager Slots
    void addInputPin();
    void addOutputPin();
    void removeSelectedPin();
    void onPinsUpdated();
    void onVisualPinsChanged();

private:
    void setupUi();
    void setupMenus();
    void createShortcuts();
    void saveCurrentToBlock();
    void updateEditorKeywords();
    void updateParametersTab();
    void refreshTemplates();

    QGraphicsScene* m_scene;
    NetManager* m_netManager;
    SmartSignalItem* m_targetBlock = nullptr;

    class QTabWidget* m_tabs;
    Flux::CodeEditor* m_editor;
    class MiniScopeWidget* m_scope;
    class QListWidget* m_explorerList;
    class QListWidget* m_templateList;
    class QDockWidget* m_templateDock;
    
    // Pin Manager Widgets
    class VisualPinMapper* m_pinMapper;
    
    // Parameter Manager Widgets
    class QWidget* m_paramsTab;
    class QVBoxLayout* m_paramsLayout;
    
    // Testing Widgets
    class QTableWidget* m_testTable;
    QPushButton* m_runTestsBtn;
    
    // Snapshot Widgets
    class QListWidget* m_snapList;
    QPushButton* m_snapBtn;
    
    class QTextEdit* m_console;
    class QTimer* m_previewTimer;
    
    GeminiPanel* m_geminiPanel;
    class QDockWidget* m_aiDock;
    
    QPushButton* m_applyBtn;
    QPushButton* m_bakeBtn;
    
    QPushButton* m_stepBtn;
    QPushButton* m_resumeBtn;
    QPushButton* m_stopBtn;
    
    QLabel* m_statusLabel;
};

#endif // LOGIC_EDITOR_PANEL_H
