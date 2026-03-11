// schematic_editor_theme.cpp
// Theme, grid, and page frame rendering for SchematicEditor

#include "schematic_editor.h"
#include "../ui/netlist_editor.h"
#include "theme_manager.h"
#include "schematic_page_item.h"
#include <QGraphicsLineItem>
#include <QStatusBar>
#include <QDate>
#include <QFileInfo>

void SchematicEditor::applyTheme() {
    PCBTheme* theme = ThemeManager::theme();
    theme->applyToWidget(this);

    // Modern status bar styling
    if (statusBar()) {
        statusBar()->setStyleSheet(theme->statusBarStylesheet());
    }

    // Tab Widget styling (Central workspace)
    if (m_workspaceTabs) {
        QString paneBg = theme->windowBackground().name();
        bool isLight = theme->type() == PCBTheme::Light;
        QString tabBg = isLight ? "#f1f3f5" : "#2d2d30";
        QString tabSelectedBg = isLight ? "#cbd5e1" : "#1a1a1c"; // Much darker
        QString tabText = theme->textSecondary().name();
        QString tabSelectedText = theme->accentColor().name();
        QString tabBorder = theme->panelBorder().name();

        m_workspaceTabs->setStyleSheet(QString(
            "QTabWidget::pane { border: none; background: %1; }"
            "QTabBar::tab { background: %2; color: %3; padding: 10px 20px; border-right: 1px solid %4; font-size: 11px; font-weight: 600; }"
            "QTabBar::tab:selected { "
            "   background: %5; color: %6; "
            "   border-top: 2px solid rgba(0,0,0,0.1); "
            "   border-left: 1px solid rgba(0,0,0,0.05); "
            "   border-bottom: none; "
            "   padding-top: 11px; " // Subtle shift down
            "}"
            "QTabBar::close-button { image: url(:/icons/tool_clear.svg); subcontrol-position: right; margin-right: 4px; }"
            "QTabBar::close-button:hover { background: rgba(255,255,255,0.1); border-radius: 2px; }"
        ).arg(paneBg, tabBg, tabText, tabBorder, tabSelectedBg, tabSelectedText));
    }

    // Apply specific toolbars and dock styling
    for (auto toolbar : findChildren<QToolBar*>()) {
        toolbar->setStyleSheet(theme->toolbarStylesheet());
    }
    for (auto dock : findChildren<QDockWidget*>()) {
        dock->setStyleSheet(theme->dockStylesheet());
        
        // Find scroll areas inside docks and update them
        for (auto scroll : dock->findChildren<QScrollArea*>()) {
            scroll->setStyleSheet(QString("QScrollArea { background-color: %1; border: none; }").arg(theme->panelBackground().name()));
            if (scroll->widget()) {
                scroll->widget()->setStyleSheet(QString("background-color: %1; border: none;").arg(theme->panelBackground().name()));
            }
        }
    }

    // Update all schematic views in all tabs
    for (auto* view : findChildren<SchematicView*>()) {
        QColor canvasBg = theme->canvasBackground();
        if (theme->type() == PCBTheme::Engineering) {
            canvasBg = QColor("#000000");
        }
        view->setBackgroundBrush(QBrush(canvasBg));
    }
    
    if (m_projectExplorer) {
        // Since it's a QWidget subclass with its own applyTheme()
        // we can find it and trigger or let its stylesheet handle it.
        // Forcing a stylesheet refresh is safest.
        QMetaObject::invokeMethod(m_projectExplorer, "applyTheme");
    }

    // Update all netlist editors
    for (auto* netEd : findChildren<NetlistEditor*>()) {
        netEd->applyTheme();
    }

    updateGrid();
    updatePageFrame();
}

void SchematicEditor::updateGrid() {
    // Grid is now drawn directly in SchematicView::drawBackground() for performance.
    // Just clean up any legacy grid items that might still be in the scene.
    QList<QGraphicsItem*> items = m_scene->items();
    for (QGraphicsItem* item : items) {
        if (item->data(0).toString() == "grid") {
            m_scene->removeItem(item);
            delete item;
        }
    }
}

void SchematicEditor::updatePageFrame() {
    if (!m_scene) return;

    QMap<QString, QSize> sizes;
    sizes["A4"]     = QSize(2520, 1782);
    sizes["A3"]     = QSize(3564, 2520);
    sizes["A2"]     = QSize(5040, 3564);
    sizes["Letter"] = QSize(2400, 1850);
    sizes["Legal"]  = QSize(3000, 1850);

    QSize pageSize = sizes.value(m_currentPageSize, QSize(2520, 1782));

    qreal sceneMargin = 500;
    qreal halfW = pageSize.width()  / 2.0 + sceneMargin;
    qreal halfH = pageSize.height() / 2.0 + sceneMargin;
    m_scene->setSceneRect(-halfW, -halfH, halfW * 2, halfH * 2);

    // Auto-fill project name from file if not set
    if (m_titleBlock.projectName.isEmpty() && !m_currentFilePath.isEmpty()) {
        QFileInfo fi(m_currentFilePath);
        m_titleBlock.projectName = fi.completeBaseName();
    }
    if (m_titleBlock.date.isEmpty())
        m_titleBlock.date = QDate::currentDate().toString("yyyy-MM-dd");

    m_titleBlock.sheetNumber = QString::number(qMax(1, m_navigationStack.size() + 1));

    if (!m_pageFrame) {
        m_pageFrame = new SchematicPageItem(pageSize, m_titleBlock.projectName);
        m_pageFrame->setPageSizeName(m_currentPageSize.isEmpty() ? "A4" : m_currentPageSize);
        m_pageFrame->setTitleBlock(m_titleBlock);
        m_scene->addItem(m_pageFrame);
    } else {
        m_pageFrame->setPageSize(pageSize);
        m_pageFrame->setPageSizeName(m_currentPageSize.isEmpty() ? "A4" : m_currentPageSize);
        m_pageFrame->setTitleBlock(m_titleBlock);
    }
}
