#include "power_nets_manager_dialog.h"

#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

PowerNetsManagerDialog::PowerNetsManagerDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Power Nets Manager");
    resize(760, 420);

    auto* layout = new QVBoxLayout(this);
    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({"Net Name", "Power Symbols", "Files"});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    layout->addWidget(m_table);

    auto* buttonRow = new QHBoxLayout();
    m_refreshButton = new QPushButton("Refresh", this);
    m_renameButton = new QPushButton("Rename Net...", this);
    auto* closeButton = new QPushButton("Close", this);
    buttonRow->addWidget(m_refreshButton);
    buttonRow->addWidget(m_renameButton);
    buttonRow->addStretch();
    buttonRow->addWidget(closeButton);
    layout->addLayout(buttonRow);

    connect(m_refreshButton, &QPushButton::clicked, this, &PowerNetsManagerDialog::onRefreshClicked);
    connect(m_renameButton, &QPushButton::clicked, this, &PowerNetsManagerDialog::onRenameClicked);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

void PowerNetsManagerDialog::setRows(const QVector<PowerNetUsageRow>& rows) {
    m_table->setRowCount(0);
    m_table->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        m_table->setItem(i, 0, new QTableWidgetItem(row.netName));
        m_table->setItem(i, 1, new QTableWidgetItem(QString::number(row.symbolCount)));
        m_table->setItem(i, 2, new QTableWidgetItem(row.files.join(", ")));
    }
    if (!rows.isEmpty()) {
        m_table->selectRow(0);
    }
}

QString PowerNetsManagerDialog::selectedNetName() const {
    const int row = m_table->currentRow();
    if (row < 0) return {};
    if (auto* item = m_table->item(row, 0)) return item->text().trimmed();
    return {};
}

void PowerNetsManagerDialog::onRenameClicked() {
    const QString oldName = selectedNetName();
    if (oldName.isEmpty()) {
        QMessageBox::information(this, "Power Nets Manager", "Select a net first.");
        return;
    }

    bool ok = false;
    const QString newName = QInputDialog::getText(
        this, "Rename Power Net", "New net name:", QLineEdit::Normal, oldName, &ok).trimmed();
    if (!ok || newName.isEmpty() || newName == oldName) return;

    emit renameRequested(oldName, newName);
}

void PowerNetsManagerDialog::onRefreshClicked() {
    emit refreshRequested();
}
