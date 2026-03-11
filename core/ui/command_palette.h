#ifndef COMMAND_PALETTE_H
#define COMMAND_PALETTE_H

#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>
#include <QGraphicsEffect>
#include <QAction>
#include <QIcon>
#include <QVariant>
#include <functional>

struct PaletteResult {
    QString title;
    QString description;
    QIcon icon;
    QVariant data;
    std::function<void()> action;
};

class CommandPalette : public QDialog {
    Q_OBJECT

public:
    explicit CommandPalette(QWidget *parent = nullptr);
    ~CommandPalette();

    void addResult(const PaletteResult& result);
    void clearResults();

    void setPlaceholderText(const QString& text);

    // Helper to add QActions automatically
    void addAction(QAction* action);

signals:
    void resultSelected(const QVariant& data);
    void queryChanged(const QString& query);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void onSearchTextChanged(const QString& text);
    void onItemSelected(QListWidgetItem* item);
    void accept() override;

private:
    void updateList();
    void setupUi();
    void applyTheme();

    QLineEdit* m_searchEdit;
    QListWidget* m_resultsList;
    QList<PaletteResult> m_allResults;
    QList<PaletteResult> m_filteredResults;
};

#endif // COMMAND_PALETTE_H
