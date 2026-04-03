#ifndef GERBER_VIEWER_WINDOW_H
#define GERBER_VIEWER_WINDOW_H

#include <QMainWindow>
#include <QListWidget>
#include <QTabWidget>
#include "gerber_view.h"
#include "gerber_3d_view.h"

/**
 * @brief Main window for the Gerber Viewer application
 */
class GerberViewerWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit GerberViewerWindow(QWidget* parent = nullptr);
    ~GerberViewerWindow();

private slots:
    void onOpenFiles();
    void onClearAll();
    void onLayerToggled(QListWidgetItem* item);

private:
    void setupUI();
    void addGerberFile(const QString& path);

    GerberView* m_view;
    Gerber3DView* m_3dView;
    QTabWidget* m_tabWidget;
    QListWidget* m_layerList;
    QList<GerberLayer*> m_loadedLayers;
};

#endif // GERBER_VIEWER_WINDOW_H