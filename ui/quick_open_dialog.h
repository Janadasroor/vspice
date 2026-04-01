#ifndef QUICK_OPEN_DIALOG_H
#define QUICK_OPEN_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>
#include <QStringList>
#include <QTimer>

/**
 * VS Code-style quick open dialog for searching and opening recent files.
 * Triggered with Ctrl+B (or Ctrl+P style).
 */
class QuickOpenDialog : public QDialog {
    Q_OBJECT

public:
    explicit QuickOpenDialog(QWidget* parent = nullptr);
    ~QuickOpenDialog();

    // Set the list of recent files to search through
    void setRecentFiles(const QStringList& files);

    // Add a single file to the list (for dynamic updates)
    void addRecentFile(const QString& file);

    // Show the dialog with focus on search box
    void showEvent(QShowEvent* event) override;

    // Hide without closing (for reuse)
    void hideEvent(QHideEvent* event) override;

signals:
    // Emitted when user selects a file (presses Enter)
    void fileSelected(const QString& filePath);

    // Emitted when dialog is closed
    void dialogClosed();

protected:
    // Handle keyboard navigation
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void onSearchTextChanged(const QString& text);
    void onFileDoubleClicked(QListWidgetItem* item);
    void onFileClicked(QListWidgetItem* item);
    void selectAndOpenCurrent();

private:
    void updateFileList(const QString& filter = QString());
    QString getFileName(const QString& filePath) const;
    QString getFileDirectory(const QString& filePath) const;
    void highlightMatch(QListWidgetItem* item, const QString& fileName, const QString& filter);

    QLineEdit* m_searchBox;
    QListWidget* m_fileList;
    QVBoxLayout* m_layout;

    QStringList m_recentFiles;
    QStringList m_filteredFiles;

    int m_maxResults;
    bool m_selectOnShow;
};

#endif // QUICK_OPEN_DIALOG_H
