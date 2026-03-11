#ifndef CSV_VIEWER_H
#define CSV_VIEWER_H

#include <QMainWindow>
#include <QTableWidget>
#include <QString>

class CsvViewer : public QMainWindow {
    Q_OBJECT
public:
    explicit CsvViewer(QWidget* parent = nullptr);
    void loadFile(const QString& filePath);

private:
    QTableWidget* m_table;
};

#endif // CSV_VIEWER_H
