#include "bom_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>

BOMDialog::BOMDialog(const ECOPackage& pkg, QWidget* parent)
    : QDialog(parent) {
    m_bom = BOMManager::generateFromECO(pkg);
    setupUI();
    populateTable();
}

BOMDialog::~BOMDialog() {}

void BOMDialog::setupUI() {
    setWindowTitle("Bill of Materials (BOM)");
    resize(850, 550);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);

    m_table = new QTableWidget();
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({"Qty", "Reference", "Value", "Footprint", "Manufacturer / MPN"});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setStyleSheet("QTableWidget { background-color: #1e1e1e; color: #dcdcdc; }");
    mainLayout->addWidget(m_table);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    
    QPushButton* csvBtn = new QPushButton("Export CSV...");
    QPushButton* htmlBtn = new QPushButton("Export HTML...");
    QPushButton* closeBtn = new QPushButton("Close");
    
    connect(csvBtn, &QPushButton::clicked, this, &BOMDialog::onExportCSV);
    connect(htmlBtn, &QPushButton::clicked, this, &BOMDialog::onExportHTML);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    btnLayout->addWidget(csvBtn);
    btnLayout->addWidget(htmlBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    mainLayout->addLayout(btnLayout);
}

void BOMDialog::populateTable() {
    m_table->setRowCount(m_bom.size());
    for (int i = 0; i < m_bom.size(); ++i) {
        const auto& entry = m_bom[i];
        m_table->setItem(i, 0, new QTableWidgetItem(QString::number(entry.quantity)));
        m_table->setItem(i, 1, new QTableWidgetItem(entry.references.join(", ")));
        m_table->setItem(i, 2, new QTableWidgetItem(entry.value));
        m_table->setItem(i, 3, new QTableWidgetItem(entry.footprint));
        
        QString mfg = entry.manufacturer;
        if (!entry.mpn.isEmpty()) mfg += " (" + entry.mpn + ")";
        m_table->setItem(i, 4, new QTableWidgetItem(mfg));
    }
    m_table->resizeColumnsToContents();
}

void BOMDialog::onExportCSV() {
    QString path = QFileDialog::getSaveFileName(this, "Export BOM", "BOM.csv", "CSV Files (*.csv)");
    if (!path.isEmpty()) {
        if (BOMManager::exportCSV(path, m_bom)) {
            QMessageBox::information(this, "Export", "BOM exported successfully to CSV.");
        }
    }
}

void BOMDialog::onExportHTML() {
    QString path = QFileDialog::getSaveFileName(this, "Export BOM", "BOM.html", "HTML Files (*.html)");
    if (!path.isEmpty()) {
        if (BOMManager::exportHTML(path, m_bom)) {
            QMessageBox::information(this, "Export", "BOM exported successfully to HTML.");
        }
    }
}
