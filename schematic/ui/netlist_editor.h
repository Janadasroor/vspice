#ifndef NETLIST_EDITOR_H
#define NETLIST_EDITOR_H

#include <QWidget>
#include <QPlainTextEdit>
#include <QToolBar>
#include <QStatusBar>
#include <QTemporaryFile>
#include "spice_highlighter.h"

class NetlistEditor : public QWidget {
    Q_OBJECT
public:
    explicit NetlistEditor(QWidget* parent = nullptr);
    ~NetlistEditor();

    void setNetlist(const QString& netlist);
    void loadFile(const QString& path);
    QString netlist() const;
    void applyTheme();

private slots:
    void onRun();
    void onClearLog();
    void onSaveAs();
    void onOutputReceived(const QString& msg);
    void onSimulationFinished();

private:
    void setupUI();

    QPlainTextEdit* m_editor;
    QPlainTextEdit* m_logArea;
    SpiceHighlighter* m_highlighter;
    QToolBar* m_toolbar;
    QTemporaryFile* m_activeTempFile;
    
    QString m_currentFilePath;
};

#endif // NETLIST_EDITOR_H
