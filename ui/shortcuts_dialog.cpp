#include "shortcuts_dialog.h"
#include "theme_manager.h"
#include <QVBoxLayout>
#include <QLineEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QDialogButtonBox>
#include <QAction>

ShortcutsDialog::ShortcutsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Keyboard Shortcuts");
    setMinimumSize(650, 550);
    
    // Apply theme if available
    if (ThemeManager::theme()) {
        ThemeManager::theme()->applyToWidget(this);
    }
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);
    
    // Search bar
    m_searchBox = new QLineEdit(this);
    m_searchBox->setPlaceholderText("Search shortcuts...");
    mainLayout->addWidget(m_searchBox);
    
    // Table widget
    m_table = new QTableWidget(this);
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({"Action", "Shortcut"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    
    // Collect all shortcuts from parent window
    QWidget* window = parent ? parent->window() : this;
    QList<QAction*> allActions = window->findChildren<QAction*>();
    QMap<QString, QString> shortcuts;
    
    for (QAction* a : allActions) {
        if (!a->shortcut().isEmpty() && !a->text().isEmpty()) {
            QString text = a->text();
            text.remove('&'); // Remove menu accelerators
            
            // Skip internal actions
            if (text.startsWith("QTF_") || text.isEmpty()) continue;
            
            shortcuts.insert(text, a->shortcut().toString(QKeySequence::NativeText));
        }
    }
    
    // Sort alphabetically
    QStringList sortedActions = shortcuts.keys();
    sortedActions.sort();
    
    m_table->setRowCount(sortedActions.size());
    
    for (int i = 0; i < sortedActions.size(); ++i) {
        const QString& action = sortedActions[i];
        const QString& shortcut = shortcuts[action];
        
        QTableWidgetItem* actionItem = new QTableWidgetItem(action);
        QTableWidgetItem* shortcutItem = new QTableWidgetItem(shortcut);
        
        m_table->setItem(i, 0, actionItem);
        m_table->setItem(i, 1, shortcutItem);
    }
    
    mainLayout->addWidget(m_table);
    
    // Search functionality
    connect(m_searchBox, &QLineEdit::textChanged, this, &ShortcutsDialog::onSearchChanged);
    
    // Close button
    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::close);
    mainLayout->addWidget(buttonBox);
}

void ShortcutsDialog::onSearchChanged(const QString& text) {
    for (int i = 0; i < m_table->rowCount(); ++i) {
        bool match = m_table->item(i, 0)->text().contains(text, Qt::CaseInsensitive) ||
                    m_table->item(i, 1)->text().contains(text, Qt::CaseInsensitive);
        m_table->setRowHidden(i, !match);
    }
}
