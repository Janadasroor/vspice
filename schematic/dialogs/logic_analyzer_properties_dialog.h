#ifndef LOGIC_ANALYZER_PROPERTIES_DIALOG_H
#define LOGIC_ANALYZER_PROPERTIES_DIALOG_H

#include "smart_properties_dialog.h"
#include "../items/logic_analyzer_item.h"

class LogicAnalyzerPropertiesDialog : public SmartPropertiesDialog {
    Q_OBJECT

public:
    LogicAnalyzerPropertiesDialog(LogicAnalyzerItem* item, QUndoStack* undoStack = nullptr, QGraphicsScene* scene = nullptr, QWidget* parent = nullptr);
    
protected:
    void onApply() override;
    void applyPreview() override;

private:
    LogicAnalyzerItem* m_item;
};

#endif // LOGIC_ANALYZER_PROPERTIES_DIALOG_H
