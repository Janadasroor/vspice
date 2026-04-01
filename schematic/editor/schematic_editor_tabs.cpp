// schematic_editor_tabs.cpp
// Tab enhancement features: context menu, shortcuts, modified indicators, reopen closed tab

#include "schematic_editor.h"
#include "schematic_view.h"
#include "schematic_page_item.h"
#include "schematic_item.h"
#include "../../core/config_manager.h"
#include <QTabBar>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>
#include <QScrollBar>
#include <QGraphicsView>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>

// ─── Tab Context Menu Setup ────────────────────────────────────────────────

void SchematicEditor::setupTabContextMenu() {
    QTabBar* tabBar = m_workspaceTabs->tabBar();
    if (!tabBar) return;

    tabBar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tabBar, &QTabBar::customContextMenuRequested,
            this, &SchematicEditor::showTabContextMenu);
}

void SchematicEditor::showTabContextMenu(const QPoint& pos) {
    QTabBar* tabBar = m_workspaceTabs->tabBar();
    if (!tabBar) return;

    int tabIndex = tabBar->tabAt(pos);
    if (tabIndex < 0) return;

    QWidget* tabWidget = m_workspaceTabs->widget(tabIndex);
    if (!tabWidget) return;

    QString tabText = m_workspaceTabs->tabText(tabIndex);
    QString filePath = tabWidget->property("filePath").toString();

    QMenu* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    // Tab info
    menu->addSection(tabText);

    // File operations
    QAction* duplicateAction = menu->addAction("Duplicate");
    duplicateAction->setShortcut(QKeySequence("Ctrl+Shift+D"));
    connect(duplicateAction, &QAction::triggered, this, [this, tabIndex]() {
        duplicateTab(tabIndex);
    });

    if (!filePath.isEmpty()) {
        QAction* copyPathAction = menu->addAction("Copy Path");
        connect(copyPathAction, &QAction::triggered, this, [this, tabIndex]() {
            copyTabPath(tabIndex);
        });

        QAction* revealAction = menu->addAction("Reveal in Explorer");
        connect(revealAction, &QAction::triggered, this, [this, tabIndex]() {
            revealInExplorer(tabIndex);
        });

        menu->addSeparator();
    }

    // Close operations
    QAction* closeAction = menu->addAction("Close");
    closeAction->setShortcut(QKeySequence("Ctrl+W"));
    connect(closeAction, &QAction::triggered, this, [this, tabIndex]() {
        closeTab(tabIndex);
    });

    if (m_workspaceTabs->count() > 1) {
        QAction* closeOthersAction = menu->addAction("Close Other Tabs");
        connect(closeOthersAction, &QAction::triggered, this, [this, tabIndex]() {
            closeOtherTabs(tabIndex);
        });

        QAction* closeRightAction = menu->addAction("Close Tabs to the Right");
        connect(closeRightAction, &QAction::triggered, this, [this, tabIndex]() {
            closeTabsToTheRight(tabIndex);
        });
    }

    // Reopen closed tab
    menu->addSeparator();
    QAction* reopenAction = menu->addAction("Reopen Closed Tab");
    reopenAction->setShortcut(QKeySequence("Ctrl+Shift+T"));
    reopenAction->setEnabled(!m_closedTabsHistory.isEmpty());
    connect(reopenAction, &QAction::triggered, this, &SchematicEditor::reopenClosedTab);

    menu->exec(tabBar->mapToGlobal(pos));
}

void SchematicEditor::duplicateTab(int index) {
    if (index < 0 || index >= m_workspaceTabs->count()) return;

    QWidget* sourceWidget = m_workspaceTabs->widget(index);
    auto* sourceView = qobject_cast<SchematicView*>(sourceWidget);
    if (!sourceView) {
        QMessageBox::warning(this, "Duplicate Tab",
            "Cannot duplicate this type of tab.");
        return;
    }

    QString sourcePath = sourceView->property("filePath").toString();
    QString newTabName = m_workspaceTabs->tabText(index) + " (Copy)";

    // Create new tab
    addSchematicTab(newTabName);
    QWidget* newWidget = m_workspaceTabs->widget(m_workspaceTabs->count() - 1);
    auto* newView = qobject_cast<SchematicView*>(newWidget);

    if (!newView) return;

    // Copy scene contents
    QGraphicsScene* sourceScene = sourceView->scene();
    QGraphicsScene* newScene = newView->scene();

    // Deep copy all items except page frame
    for (QGraphicsItem* item : sourceScene->items()) {
        if (dynamic_cast<SchematicPageItem*>(item)) continue; // Skip page frame

        // Use SchematicItem::clone() for deep copy
        auto* si = dynamic_cast<SchematicItem*>(item);
        if (si) {
            SchematicItem* clonedItem = si->clone();
            if (clonedItem) {
                newScene->addItem(clonedItem);
            }
        }
    }

    // Copy properties
    newView->setProperty("filePath", QString()); // New unsaved file
    newView->setNetManager(newView->netManager()); // Ensure net manager is set

    statusBar()->showMessage("Tab duplicated", 2000);
}

void SchematicEditor::closeOtherTabs(int index) {
    if (index < 0 || index >= m_workspaceTabs->count()) return;

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Close Other Tabs",
        "Close all tabs except the current one?",
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply != QMessageBox::Yes) return;

    // Collect tabs to close (in reverse order to avoid index shifting issues)
    QList<int> tabsToClose;
    for (int i = m_workspaceTabs->count() - 1; i >= 0; --i) {
        if (i != index) {
            tabsToClose.append(i);
        }
    }

    for (int tabIdx : tabsToClose) {
        closeTab(tabIdx);
    }
}

void SchematicEditor::closeTabsToTheRight(int index) {
    if (index < 0 || index >= m_workspaceTabs->count()) return;

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Close Tabs to the Right",
        "Close all tabs to the right of the current one?",
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply != QMessageBox::Yes) return;

    // Collect tabs to close (in reverse order)
    QList<int> tabsToClose;
    for (int i = m_workspaceTabs->count() - 1; i > index; --i) {
        tabsToClose.append(i);
    }

    for (int tabIdx : tabsToClose) {
        closeTab(tabIdx);
    }
}

void SchematicEditor::copyTabPath(int index) {
    if (index < 0 || index >= m_workspaceTabs->count()) return;

    QWidget* widget = m_workspaceTabs->widget(index);
    if (!widget) return;

    QString filePath = widget->property("filePath").toString();
    if (filePath.isEmpty()) {
        statusBar()->showMessage("Tab has no file path", 2000);
        return;
    }

    QClipboard* clipboard = QApplication::clipboard();
    clipboard->setText(filePath);
    statusBar()->showMessage("Path copied to clipboard", 2000);
}

void SchematicEditor::revealInExplorer(int index) {
    if (index < 0 || index >= m_workspaceTabs->count()) return;

    QWidget* widget = m_workspaceTabs->widget(index);
    if (!widget) return;

    QString filePath = widget->property("filePath").toString();
    if (filePath.isEmpty()) {
        QMessageBox::warning(this, "Reveal in Explorer", "Tab has no file path.");
        return;
    }

    QFileInfo fi(filePath);
    if (!fi.exists()) {
        QMessageBox::warning(this, "Reveal in Explorer", "File does not exist.");
        return;
    }

    // Cross-platform: open the containing directory and select the file
    QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
    statusBar()->showMessage("Opened folder: " + fi.absolutePath(), 3000);
}

// ─── Reopen Closed Tab ─────────────────────────────────────────────────────

void SchematicEditor::reopenClosedTab() {
    if (m_closedTabsHistory.isEmpty()) {
        statusBar()->showMessage("No closed tabs to reopen", 2000);
        return;
    }

    ClosedTabInfo info = m_closedTabsHistory.takeLast();

    if (!QFile::exists(info.filePath)) {
        QMessageBox::warning(this, "Reopen Closed Tab",
            QString("File no longer exists:\n%1").arg(info.filePath));
        return;
    }

    openFile(info.filePath);

    // Restore scroll position and zoom
    QWidget* currentWidget = m_workspaceTabs->currentWidget();
    if (auto* view = qobject_cast<SchematicView*>(currentWidget)) {
        view->horizontalScrollBar()->setValue(info.scrollX);
        view->verticalScrollBar()->setValue(info.scrollY);
        // Zoom level restoration would require additional implementation
    }

    statusBar()->showMessage("Reopened: " + QFileInfo(info.filePath).fileName(), 2000);
}

// ─── Tab Switching Shortcuts ───────────────────────────────────────────────

void SchematicEditor::switchToTab(int offset) {
    int currentIndex = m_workspaceTabs->currentIndex();
    int newIndex = currentIndex + offset;

    // Wrap around
    if (newIndex < 0) {
        newIndex = m_workspaceTabs->count() - 1;
    } else if (newIndex >= m_workspaceTabs->count()) {
        newIndex = 0;
    }

    if (newIndex >= 0 && newIndex < m_workspaceTabs->count()) {
        m_workspaceTabs->setCurrentIndex(newIndex);
    }
}

// ─── Modified Indicator ────────────────────────────────────────────────────

void SchematicEditor::updateTabModifiedIndicator(int index, bool modified) {
    if (index < 0 || index >= m_workspaceTabs->count()) return;

    QString baseText = m_workspaceTabs->tabText(index);

    // Remove existing indicator if present
    if (baseText.endsWith(" *")) {
        baseText = baseText.left(baseText.length() - 2);
    }

    // Add indicator if modified
    if (modified) {
        baseText += " *";
    }

    m_workspaceTabs->setTabText(index, baseText);
    m_workspaceTabs->setTabToolTip(index, modified ? "Modified" : "");
}

// ─── Tab Shortcuts Setup ───────────────────────────────────────────────────

void SchematicEditor::setupTabShortcuts() {
    // Ctrl+Tab: Next tab
    QAction* nextTabAction = new QAction("Next Tab", this);
    nextTabAction->setShortcut(QKeySequence("Ctrl+Tab"));
    connect(nextTabAction, &QAction::triggered, this, [this]() {
        switchToTab(1);
    });
    addAction(nextTabAction);

    // Ctrl+Shift+Tab: Previous tab
    QAction* prevTabAction = new QAction("Previous Tab", this);
    prevTabAction->setShortcut(QKeySequence("Ctrl+Shift+Tab"));
    connect(prevTabAction, &QAction::triggered, this, [this]() {
        switchToTab(-1);
    });
    addAction(prevTabAction);

    // Ctrl+W: Close current tab
    QAction* closeTabAction = new QAction("Close Tab", this);
    closeTabAction->setShortcut(QKeySequence("Ctrl+W"));
    connect(closeTabAction, &QAction::triggered, this, [this]() {
        int currentIndex = m_workspaceTabs->currentIndex();
        if (currentIndex >= 0) {
            closeTab(currentIndex);
        }
    });
    addAction(closeTabAction);

    // Ctrl+Shift+T: Reopen closed tab
    QAction* reopenTabAction = new QAction("Reopen Closed Tab", this);
    reopenTabAction->setShortcut(QKeySequence("Ctrl+Shift+T"));
    connect(reopenTabAction, &QAction::triggered, this, &SchematicEditor::reopenClosedTab);
    addAction(reopenTabAction);

    // Ctrl+Shift+D: Duplicate tab
    QAction* duplicateTabAction = new QAction("Duplicate Tab", this);
    duplicateTabAction->setShortcut(QKeySequence("Ctrl+Shift+D"));
    connect(duplicateTabAction, &QAction::triggered, this, [this]() {
        int currentIndex = m_workspaceTabs->currentIndex();
        if (currentIndex >= 0) {
            duplicateTab(currentIndex);
        }
    });
    addAction(duplicateTabAction);

    // Alt+1 through Alt+9: Jump to specific tab
    for (int i = 1; i <= 9; ++i) {
        QAction* jumpAction = new QAction(QString("Jump to Tab %1").arg(i), this);
        jumpAction->setShortcut(QKeySequence(QString("Alt+%1").arg(i)));
        connect(jumpAction, &QAction::triggered, this, [this, i]() {
            if (i <= m_workspaceTabs->count()) {
                m_workspaceTabs->setCurrentIndex(i - 1);
            }
        });
        addAction(jumpAction);
    }
}

// ─── Tab Bar Signals Setup ────────────────────────────────────────────────

void SchematicEditor::setupTabBarSignals() {
    QTabBar* tabBar = m_workspaceTabs->tabBar();
    if (!tabBar) return;

    // Install event filter for right-click context menu
    tabBar->installEventFilter(this);

    // Track tab modifications via undo stack clean/dirty state
    connect(m_undoStack, &QUndoStack::cleanChanged, this, [this](bool isClean) {
        int currentIndex = m_workspaceTabs->currentIndex();
        if (currentIndex >= 0) {
            updateTabModifiedIndicator(currentIndex, !isClean);
        }
    });

    // Update modified indicator when tab changes
    connect(m_workspaceTabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index >= 0) {
            QWidget* widget = m_workspaceTabs->widget(index);
            if (widget) {
                QString filePath = widget->property("filePath").toString();
                bool isModified = filePath.isEmpty() ? !m_undoStack->isClean() : m_isModified;
                updateTabModifiedIndicator(index, isModified);
            }
        }
    });
}
