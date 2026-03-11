#ifndef FLUX_SCRIPT_PANEL_H
#define FLUX_SCRIPT_PANEL_H

#include <QWidget>
#include "flux_code_editor.h"

class QGraphicsScene;
class NetManager;

namespace Flux {

class ScriptPanel : public QWidget {
    Q_OBJECT
public:
    ScriptPanel(QGraphicsScene* scene, NetManager* netManager, QWidget* parent = nullptr);
    void setScript(const QString& code);

private:
    CodeEditor* m_editor;
};

} // namespace Flux

#endif // FLUX_SCRIPT_PANEL_H
