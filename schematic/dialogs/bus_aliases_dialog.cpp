#include "bus_aliases_dialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>

BusAliasesDialog::BusAliasesDialog(const QMap<QString, QList<QString>>& aliases, QWidget* parent)
    : QDialog(parent), m_aliases(aliases) {
    setWindowTitle("Bus Aliases");
    resize(780, 420);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    QLabel* hint = new QLabel("Define aliases like SPI_BUS = MOSI,MISO,SCK,CS. Use labels such as SPI_BUS[0] or SPI_BUS.MOSI.");
    hint->setWordWrap(true);
    layout->addWidget(hint);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({"Alias", "Signals (comma-separated)"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(m_table);

    QHBoxLayout* rowButtons = new QHBoxLayout();
    QPushButton* addBtn = new QPushButton("Add Alias", this);
    QPushButton* delBtn = new QPushButton("Remove Selected", this);
    connect(addBtn, &QPushButton::clicked, this, &BusAliasesDialog::onAddRow);
    connect(delBtn, &QPushButton::clicked, this, &BusAliasesDialog::onRemoveRow);
    rowButtons->addWidget(addBtn);
    rowButtons->addWidget(delBtn);
    rowButtons->addStretch();
    layout->addLayout(rowButtons);

    QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &BusAliasesDialog::onAccept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(bb);

    populate();
}

void BusAliasesDialog::populate() {
    m_table->setRowCount(0);
    int row = 0;
    for (auto it = m_aliases.begin(); it != m_aliases.end(); ++it) {
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(it.key()));
        m_table->setItem(row, 1, new QTableWidgetItem(it.value().join(",")));
        ++row;
    }
}

void BusAliasesDialog::onAddRow() {
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->setItem(row, 0, new QTableWidgetItem("BUS_ALIAS"));
    m_table->setItem(row, 1, new QTableWidgetItem("SIG0,SIG1"));
    m_table->setCurrentCell(row, 0);
    m_table->editItem(m_table->item(row, 0));
}

void BusAliasesDialog::onRemoveRow() {
    const int row = m_table->currentRow();
    if (row >= 0) m_table->removeRow(row);
}

void BusAliasesDialog::onAccept() {
    QMap<QString, QList<QString>> parsed;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const QString alias = (m_table->item(row, 0) ? m_table->item(row, 0)->text().trimmed() : QString());
        if (alias.isEmpty()) continue;
        const QString membersText = (m_table->item(row, 1) ? m_table->item(row, 1)->text() : QString());
        QStringList members;
        for (const QString& part : membersText.split(',', Qt::SkipEmptyParts)) {
            const QString token = part.trimmed();
            if (!token.isEmpty()) members.append(token);
        }
        if (members.isEmpty()) {
            QMessageBox::warning(this, "Invalid Alias", QString("Alias '%1' has no signals.").arg(alias));
            return;
        }
        parsed[alias] = members;
    }
    m_aliases = parsed;
    accept();
}
