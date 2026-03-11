#include "pin_table_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QHeaderView>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QSet>

PinTableDialog::PinTableDialog(const QList<SymbolPrimitive>& pins, QWidget* parent)
    : QDialog(parent), m_pins(pins) {
    setWindowTitle("Pin Table - Batch Edit");
    resize(800, 500);
    auto* layout = new QVBoxLayout(this);
    
    m_table = new QTableWidget(pins.size(), 10, this);
    m_table->setHorizontalHeaderLabels({"#", "Name", "Number", "Orientation", "Type", "SwapGrp", "JumperGrp", "Vis", "Stacked", "Alternates"});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    for (int i = 0; i < pins.size(); ++i) {
        const auto& p = pins[i];
        m_table->setItem(i, 0, new QTableWidgetItem(QString::number(i)));
        m_table->item(i, 0)->setFlags(m_table->item(i, 0)->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 1, new QTableWidgetItem(p.data.value("name").toString()));
        m_table->setItem(i, 2, new QTableWidgetItem(QString::number(p.data.value("number").toInt())));
        m_table->setItem(i, 3, new QTableWidgetItem(p.data.value("orientation").toString("Right")));
        m_table->setItem(i, 4, new QTableWidgetItem(p.data.value("electricalType").toString("Passive")));
        m_table->setItem(i, 5, new QTableWidgetItem(QString::number(p.data.value("swapGroup").toInt(0))));
        m_table->setItem(i, 6, new QTableWidgetItem(QString::number(p.data.value("jumperGroup").toInt(0))));
        auto* visItem = new QTableWidgetItem();
        visItem->setCheckState(p.data.value("visible").toBool(true) ? Qt::Checked : Qt::Unchecked);
        m_table->setItem(i, 7, visItem);
        m_table->setItem(i, 8, new QTableWidgetItem(p.data.value("stackedNumbers").toString()));
        m_table->setItem(i, 9, new QTableWidgetItem(p.data.value("alternateNames").toString()));
    }

    layout->addWidget(m_table);

    auto* topBtnBox = new QHBoxLayout();
    auto* importBtn = new QPushButton("Import CSV...");
    auto* exportBtn = new QPushButton("Export CSV...");
    topBtnBox->addWidget(importBtn);
    topBtnBox->addWidget(exportBtn);
    topBtnBox->addStretch();
    layout->insertLayout(1, topBtnBox);

    auto* btnBox = new QHBoxLayout();
    auto* okBtn = new QPushButton("Apply Changes");
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    auto* cancelBtn = new QPushButton("Cancel");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnBox->addStretch();
    btnBox->addWidget(okBtn);
    btnBox->addWidget(cancelBtn);
    layout->addLayout(btnBox);

    connect(importBtn, &QPushButton::clicked, this, &PinTableDialog::onImportCSV);
    connect(exportBtn, &QPushButton::clicked, this, &PinTableDialog::onExportCSV);
    connect(m_table, &QTableWidget::cellChanged, this, &PinTableDialog::validate);
    
    validate(); // Initial check
}

void PinTableDialog::validate() {
    QSet<QString> names;
    QSet<QString> numbers;
    QSet<QString> dupNames;
    QSet<QString> dupNumbers;

    for (int i = 0; i < m_table->rowCount(); ++i) {
        QString name = m_table->item(i, 1)->text();
        QString num = m_table->item(i, 2)->text();

        if (names.contains(name)) dupNames.insert(name);
        else names.insert(name);

        if (numbers.contains(num)) dupNumbers.insert(num);
        else numbers.insert(num);
    }

    for (int i = 0; i < m_table->rowCount(); ++i) {
        auto* nameItem = m_table->item(i, 1);
        auto* numItem = m_table->item(i, 2);
        
        if (dupNames.contains(nameItem->text())) nameItem->setBackground(QBrush(QColor(255, 50, 50, 100)));
        else nameItem->setBackground(Qt::NoBrush);

        if (dupNumbers.contains(numItem->text())) numItem->setBackground(QBrush(QColor(255, 50, 50, 100)));
        else numItem->setBackground(Qt::NoBrush);
    }
}

void PinTableDialog::onImportCSV() {
    QString path = QFileDialog::getOpenFileName(this, "Import Pins", "", "CSV Files (*.csv);;TSV Files (*.tsv);;All Files (*.*)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QTextStream in(&file);
    QStringList lines;
    while (!in.atEnd()) lines.append(in.readLine());

    if (lines.isEmpty()) return;

    // Expecting CSV format: Name, Number, Type, Orientation, ...
    m_table->blockSignals(true);
    m_table->setRowCount(0);
    for (const QString& line : lines) {
        QStringList fields = line.split(path.endsWith(".tsv") ? '\t' : ',');
        if (fields.size() < 2) continue;

        int r = m_table->rowCount();
        m_table->insertRow(r);
        m_table->setItem(r, 0, new QTableWidgetItem(QString::number(r)));
        m_table->item(r, 0)->setFlags(m_table->item(r, 0)->flags() & ~Qt::ItemIsEditable);
        
        m_table->setItem(r, 1, new QTableWidgetItem(fields.value(0))); // Name
        m_table->setItem(r, 2, new QTableWidgetItem(fields.value(1))); // Num
        m_table->setItem(r, 3, new QTableWidgetItem(fields.value(3, "Right"))); // Ori
        m_table->setItem(r, 4, new QTableWidgetItem(fields.value(2, "Passive"))); // Type
        // ... fill other defaults
        for (int c = 5; c < 10; ++c) m_table->setItem(r, c, new QTableWidgetItem(""));
    }
    m_table->blockSignals(false);
    validate();
}

void PinTableDialog::onExportCSV() {
    QString path = QFileDialog::getSaveFileName(this, "Export Pins", "", "CSV Files (*.csv)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    QTextStream out(&file);
    for (int i = 0; i < m_table->rowCount(); ++i) {
        QStringList row;
        for (int j = 1; j < m_table->columnCount(); ++j) {
            if (auto* it = m_table->item(i, j)) row.append(it->text());
            else row.append("");
        }
        out << row.join(",") << "\n";
    }
}

QList<QMap<QString, QVariant>> PinTableDialog::results() const {
    QList<QMap<QString, QVariant>> res;
    for (int i = 0; i < m_table->rowCount(); ++i) {
        QMap<QString, QVariant> m;
        m["name"] = m_table->item(i, 1)->text();
        m["number"] = m_table->item(i, 2)->text().toInt();
        m["orientation"] = m_table->item(i, 3)->text();
        m["electricalType"] = m_table->item(i, 4)->text();
        m["swapGroup"] = m_table->item(i, 5)->text().toInt();
        m["jumperGroup"] = m_table->item(i, 6)->text().toInt();
        m["visible"] = (m_table->item(i, 7)->checkState() == Qt::Checked);
        m["stackedNumbers"] = m_table->item(i, 8)->text();
        m["alternateNames"] = m_table->item(i, 9)->text();
        res.append(m);
    }
    return res;
}
