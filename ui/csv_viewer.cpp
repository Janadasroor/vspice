#include "csv_viewer.h"
#include "../core/theme_manager.h"
#include <QFile>
#include <QTextStream>
#include <QHeaderView>
#include <QMessageBox>

CsvViewer::CsvViewer(QWidget* parent) : QMainWindow(parent) {
    m_table = new QTableWidget(this);
    setCentralWidget(m_table);
    resize(800, 600);

    PCBTheme* theme = ThemeManager::theme();
    if (theme) {
        theme->applyToWidget(this);
    }
}

void CsvViewer::loadFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Error", "Failed to open file: " + filePath);
        return;
    }

    setWindowTitle("CSV Viewer - " + filePath);

    QTextStream in(&file);
    bool firstLine = true;
    int rowCount = 0;

    m_table->setRowCount(0);
    m_table->setColumnCount(0);

    while (!in.atEnd()) {
        QString line = in.readLine();
        // A simple CSV split, ignoring quotes for simplicity
        QStringList fields = line.split(',');

        if (firstLine) {
            m_table->setColumnCount(fields.size());
            m_table->setHorizontalHeaderLabels(fields);
            firstLine = false;
        } else {
            m_table->insertRow(rowCount);
            for (int i = 0; i < fields.size(); ++i) {
                m_table->setItem(rowCount, i, new QTableWidgetItem(fields[i].trimmed()));
            }
            rowCount++;
        }
    }

    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    file.close();
}
