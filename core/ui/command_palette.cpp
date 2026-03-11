#include "command_palette.h"
#include "../theme.h"
#include "../theme_manager.h"
#include <QKeyEvent>
#include <QPainter>
#include <QApplication>
#include <QScreen>
#include <QStyledItemDelegate>

class PaletteResultDelegate : public QStyledItemDelegate {
 public:
     void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
         QStyleOptionViewItem opt = option;
         initStyleOption(&opt, index);

         painter->save();
         
         bool selected = opt.state & QStyle::State_Selected;
         
         // Background
         if (selected) {
             painter->fillRect(opt.rect, QColor(60, 60, 65));
             painter->setPen(QColor(100, 160, 255));
             painter->drawRect(opt.rect.adjusted(0, 0, -1, -1));
         }

         QRect iconRect = opt.rect.adjusted(10, 10, -opt.rect.width() + 42, -10);
         QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
         if (!icon.isNull()) {
             icon.paint(painter, iconRect);
         }

         QString title = index.data(Qt::DisplayRole).toString();
         QString desc = index.data(Qt::UserRole + 1).toString();

         painter->setPen(selected ? Qt::white : QColor(220, 220, 220));
         QFont titleFont = painter->font();
         titleFont.setBold(true);
         titleFont.setPointSize(10);
         painter->setFont(titleFont);
         
         painter->drawText(opt.rect.adjusted(52, 8, -10, -25), Qt::AlignLeft, title);

         painter->setPen(QColor(150, 150, 150));
         QFont descFont = painter->font();
         descFont.setBold(false);
         descFont.setPointSize(8);
         painter->setFont(descFont);
         painter->drawText(opt.rect.adjusted(52, 28, -10, -5), Qt::AlignLeft, desc);

         painter->restore();
     }

     QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
         return QSize(option.rect.width(), 50);
     }
 };

 CommandPalette::CommandPalette(QWidget* parent)
     : QDialog(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint) {
     setupUi();
     applyTheme();
     
     setAttribute(Qt::WA_DeleteOnClose);
     installEventFilter(this);
 }

 CommandPalette::~CommandPalette() {}

 void CommandPalette::setupUi() {
     setAttribute(Qt::WA_TranslucentBackground);
     setFixedWidth(600);
     
     QVBoxLayout* mainLayout = new QVBoxLayout(this);
     mainLayout->setContentsMargins(10, 10, 10, 10);

     QWidget* container = new QWidget(this);
     container->setObjectName("container");
     QVBoxLayout* layout = new QVBoxLayout(container);
     layout->setContentsMargins(5, 5, 5, 5);
     layout->setSpacing(0);

     m_searchEdit = new QLineEdit(container);
     m_searchEdit->setObjectName("searchEdit");
     m_searchEdit->setPlaceholderText("Type a command or search for items...");
     m_searchEdit->setMinimumHeight(45);
     m_searchEdit->setStyleSheet("font-size: 14px; border: none; background: transparent; padding: 0 10px;");
     
     m_resultsList = new QListWidget(container);
     m_resultsList->setObjectName("resultsList");
     m_resultsList->setMinimumHeight(300);
     m_resultsList->setItemDelegate(new PaletteResultDelegate());
     m_resultsList->setStyleSheet("border: none; background: transparent;");
     m_resultsList->setFocusPolicy(Qt::NoFocus);

     layout->addWidget(m_searchEdit);
     
     QFrame* separator = new QFrame(container);
     separator->setFrameShape(QFrame::HLine);
     separator->setFrameShadow(QFrame::Plain);
     separator->setStyleSheet("background-color: #333333; max-height: 1px;");
     layout->addWidget(separator);
     
     layout->addWidget(m_resultsList);

     mainLayout->addWidget(container);

     // Drop shadow
     QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this);
     shadow->setBlurRadius(30);
     shadow->setColor(QColor(0, 0, 0, 180));
     shadow->setOffset(0, 10);
     container->setGraphicsEffect(shadow);

     connect(m_searchEdit, &QLineEdit::textChanged, this, &CommandPalette::onSearchTextChanged);
     connect(m_resultsList, &QListWidget::itemDoubleClicked, this, &CommandPalette::onItemSelected);
 }

 void CommandPalette::applyTheme() {
     PCBTheme* theme = ThemeManager::theme();
     QString containerStyle = QString(
         "QWidget#container {"
         "   background-color: #1e1e20;"
         "   border: 1px solid #333333;"
         "   border-radius: 8px;"
         "}"
         "QLineEdit {"
         "   color: white;"
         "}"
     );
     setStyleSheet(containerStyle);
 }

 void CommandPalette::addResult(const PaletteResult& result) {
     m_allResults.append(result);
 }

 void CommandPalette::clearResults() {
     m_allResults.clear();
     m_filteredResults.clear();
     m_resultsList->clear();
 }

 void CommandPalette::addAction(QAction* action) {
     if (!action || action->text().isEmpty()) return;
     
     PaletteResult res;
     res.title = action->text().remove("&");
     res.description = "Command (" + action->shortcut().toString() + ")";
     res.icon = action->icon();
     res.action = [action]() { action->trigger(); };
     addResult(res);
 }

void CommandPalette::setPlaceholderText(const QString& text) {
    m_searchEdit->setPlaceholderText(text);
}

void CommandPalette::onSearchTextChanged(const QString& text) {
    m_filteredResults.clear();
    m_resultsList->clear();

    if (text.isEmpty()) {
        m_filteredResults = m_allResults;
    } else {
        for (const auto& res : m_allResults) {
            if (res.title.contains(text, Qt::CaseInsensitive) || 
                res.description.contains(text, Qt::CaseInsensitive)) {
                m_filteredResults.append(res);
            }
        }
    }

    updateList();
    emit queryChanged(text);
}

void CommandPalette::updateList() {
    m_resultsList->clear();
    
    // Limit to top 10 for performance if list is huge
    int count = 0;
    const auto& listToShow = m_searchEdit->text().isEmpty() ? m_allResults : m_filteredResults;

    for (const auto& res : listToShow) {
        QListWidgetItem* item = new QListWidgetItem(m_resultsList);
        item->setText(res.title);
        item->setData(Qt::DecorationRole, res.icon);
        item->setData(Qt::UserRole, res.data);
        item->setData(Qt::UserRole + 1, res.description);
        m_resultsList->addItem(item);
        if (++count > 20) break;
    }

    if (m_resultsList->count() > 0) {
        m_resultsList->setCurrentRow(0);
    }
}

void CommandPalette::onItemSelected(QListWidgetItem* item) {
    if (!item) return;
    
    int row = m_resultsList->row(item);
    const auto& list = m_searchEdit->text().isEmpty() ? m_allResults : m_filteredResults;
    
    if (row >= 0 && row < list.size()) {
        const auto& res = list[row];
        if (res.action) res.action();
        emit resultSelected(res.data);
    }
    
    QDialog::accept();
}

void CommandPalette::accept() {
    if (m_resultsList->currentRow() >= 0) {
        onItemSelected(m_resultsList->currentItem());
    } else {
        QDialog::accept();
    }
}

bool CommandPalette::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Up) {
            int row = m_resultsList->currentRow();
            if (row > 0) m_resultsList->setCurrentRow(row - 1);
            return true;
        } else if (keyEvent->key() == Qt::Key_Down) {
            int row = m_resultsList->currentRow();
            if (row < m_resultsList->count() - 1) m_resultsList->setCurrentRow(row + 1);
            return true;
        } else if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            accept();
            return true;
        } else if (keyEvent->key() == Qt::Key_Escape) {
            reject();
            return true;
        }
    } else if (event->type() == QEvent::WindowDeactivate) {
        reject();
        return true;
    }
    return QDialog::eventFilter(obj, event);
}

void CommandPalette::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    
    // Center on screen
    QScreen* screen = QApplication::primaryScreen();
    if (parentWidget() && parentWidget()->window()) {
        screen = parentWidget()->window()->screen();
    }
    
    QRect screenGeometry = screen->geometry();
    int x = (screenGeometry.width() - width()) / 2;
    int y = screenGeometry.height() / 4;
    move(x, y);
    
    m_searchEdit->setFocus();
    updateList();
}
