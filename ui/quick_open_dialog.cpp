// quick_open_dialog.cpp
// VS Code-style quick open dialog for searching and opening recent files

#include "quick_open_dialog.h"
#include <QKeyEvent>
#include <QFileInfo>
#include <QDir>
#include <QPainter>
#include <QApplication>
#include <QScreen>

QuickOpenDialog::QuickOpenDialog(QWidget* parent)
    : QDialog(parent)
    , m_searchBox(nullptr)
    , m_fileList(nullptr)
    , m_layout(nullptr)
    , m_maxResults(50)
    , m_selectOnShow(true)
{
    setWindowTitle("Quick Open");
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowSystemMenuHint);
    setWindowModality(Qt::ApplicationModal);

    // Set size (similar to VS Code's quick open)
    resize(600, 400);

    // Center on parent or screen
    if (parent) {
        move(parent->geometry().center() - rect().center());
    } else {
        QScreen* screen = QApplication::primaryScreen();
        if (screen) {
            QRect screenGeometry = screen->geometry();
            move(screenGeometry.center() - rect().center());
        }
    }

    // Create layout
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    // Search box
    m_searchBox = new QLineEdit(this);
    m_searchBox->setPlaceholderText("Search files... (type to filter)");
    m_searchBox->setClearButtonEnabled(true);
    m_searchBox->setStyleSheet(
        "QLineEdit {"
        "    padding: 12px 16px;"
        "    font-size: 14px;"
        "    border: none;"
        "    border-bottom: 1px solid #e0e0e0;"
        "    background: #ffffff;"
        "    border-radius: 8px 8px 0 0;"
        "}"
        "QLineEdit:focus {"
        "    background: #f8f9fa;"
        "}"
    );
    m_layout->addWidget(m_searchBox);

    // File list
    m_fileList = new QListWidget(this);
    m_fileList->setFrameShape(QListWidget::NoFrame);
    m_fileList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_fileList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fileList->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fileList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_fileList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_fileList->setStyleSheet(
        "QListWidget {"
        "    border: none;"
        "    background: #ffffff;"
        "    border-radius: 0 0 8px 8px;"
        "}"
        "QListWidget::item {"
        "    padding: 8px 16px;"
        "    border-bottom: 1px solid #f0f0f0;"
        "}"
        "QListWidget::item:selected {"
        "    background: #007acc;"
        "    color: #ffffff;"
        "}"
        "QListWidget::item:hover {"
        "    background: #e8f4fc;"
        "}"
    );
    m_layout->addWidget(m_fileList);

    // Connect signals
    connect(m_searchBox, &QLineEdit::textChanged, this, &QuickOpenDialog::onSearchTextChanged);
    connect(m_fileList, &QListWidget::itemDoubleClicked, this, &QuickOpenDialog::onFileDoubleClicked);
    connect(m_fileList, &QListWidget::itemClicked, this, &QuickOpenDialog::onFileClicked);

    // Install event filter for keyboard navigation
    m_searchBox->installEventFilter(this);
    m_fileList->installEventFilter(this);
}

QuickOpenDialog::~QuickOpenDialog() {
}

void QuickOpenDialog::setRecentFiles(const QStringList& files) {
    m_recentFiles = files;
    updateFileList();
}

void QuickOpenDialog::addRecentFile(const QString& file) {
    if (!m_recentFiles.contains(file)) {
        m_recentFiles.prepend(file);
        // Trim to max
        while (m_recentFiles.size() > m_maxResults * 2) {
            m_recentFiles.removeLast();
        }
    }
    updateFileList();
}

void QuickOpenDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    m_searchBox->clear();
    m_searchBox->setFocus();
    updateFileList();
    if (m_selectOnShow && m_fileList->count() > 0) {
        m_fileList->setCurrentRow(0);
    }
}

void QuickOpenDialog::hideEvent(QHideEvent* event) {
    QDialog::hideEvent(event);
    emit dialogClosed();
}

bool QuickOpenDialog::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_searchBox || watched == m_fileList) {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

            switch (keyEvent->key()) {
                case Qt::Key_Down:
                    if (m_fileList->currentRow() < m_fileList->count() - 1) {
                        m_fileList->setCurrentRow(m_fileList->currentRow() + 1);
                        m_fileList->scrollToItem(m_fileList->currentItem());
                    }
                    return true;

                case Qt::Key_Up:
                    if (m_fileList->currentRow() > 0) {
                        m_fileList->setCurrentRow(m_fileList->currentRow() - 1);
                        m_fileList->scrollToItem(m_fileList->currentItem());
                    }
                    return true;

                case Qt::Key_Return:
                case Qt::Key_Enter:
                    selectAndOpenCurrent();
                    return true;

                case Qt::Key_Escape:
                    reject();
                    return true;

                case Qt::Key_Tab:
                    // Switch focus between search and list
                    if (watched == m_searchBox) {
                        m_fileList->setFocus();
                    } else {
                        m_searchBox->setFocus();
                    }
                    return true;
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}

void QuickOpenDialog::keyPressEvent(QKeyEvent* event) {
    // Handle escape at dialog level
    if (event->key() == Qt::Key_Escape) {
        reject();
        return;
    }
    // Pass other keys to search box
    if (event->key() != Qt::Key_Up && event->key() != Qt::Key_Down &&
        event->key() != Qt::Key_Return && event->key() != Qt::Key_Enter) {
        m_searchBox->setFocus();
        QApplication::sendEvent(m_searchBox, event);
        return;
    }
    QDialog::keyPressEvent(event);
}

void QuickOpenDialog::onSearchTextChanged(const QString& text) {
    updateFileList(text);
}

void QuickOpenDialog::updateFileList(const QString& filter) {
    m_fileList->clear();
    m_filteredFiles.clear();

    QString filterLower = filter.toLower();
    int count = 0;

    for (const QString& file : m_recentFiles) {
        if (count >= m_maxResults) break;

        QString fileName = getFileName(file);
        QString dirName = getFileDirectory(file);

        // Filter matching
        bool matches = true;
        if (!filterLower.isEmpty()) {
            matches = fileName.toLower().contains(filterLower) ||
                      dirName.toLower().contains(filterLower) ||
                      file.toLower().contains(filterLower);
        }

        if (matches) {
            m_filteredFiles.append(file);

            auto* item = new QListWidgetItem();
            item->setData(Qt::DisplayRole, fileName);
            item->setData(Qt::UserRole + 1, dirName); // Store directory
            item->setData(Qt::UserRole + 2, file);    // Store full path

            // Highlight matches
            if (!filter.isEmpty()) {
                highlightMatch(item, fileName, filter);
            }

            // Add tooltip with full path
            item->setToolTip(file);

            m_fileList->addItem(item);
            count++;
        }
    }

    // Select first item if available
    if (m_fileList->count() > 0 && m_selectOnShow) {
        m_fileList->setCurrentRow(0);
    }
}

void QuickOpenDialog::onFileDoubleClicked(QListWidgetItem* item) {
    QString filePath = item->data(Qt::UserRole + 2).toString();
    if (!filePath.isEmpty()) {
        emit fileSelected(filePath);
        accept();
    }
}

void QuickOpenDialog::onFileClicked(QListWidgetItem* item) {
    // Optional: preview file on click
    Q_UNUSED(item);
}

void QuickOpenDialog::selectAndOpenCurrent() {
    QListWidgetItem* item = m_fileList->currentItem();
    if (item) {
        QString filePath = item->data(Qt::UserRole + 2).toString();
        if (!filePath.isEmpty()) {
            emit fileSelected(filePath);
            accept();
        }
    } else if (m_fileList->count() > 0) {
        // If nothing selected but items exist, select first
        m_fileList->setCurrentRow(0);
        selectAndOpenCurrent();
    }
}

QString QuickOpenDialog::getFileName(const QString& filePath) const {
    QFileInfo fi(filePath);
    return fi.fileName();
}

QString QuickOpenDialog::getFileDirectory(const QString& filePath) const {
    QFileInfo fi(filePath);
    return fi.absolutePath();
}

void QuickOpenDialog::highlightMatch(QListWidgetItem* item, const QString& fileName, const QString& filter) {
    // Simple implementation - could be enhanced with HTML formatting
    // For now, the search naturally highlights by filtering
    Q_UNUSED(item);
    Q_UNUSED(fileName);
    Q_UNUSED(filter);
}
