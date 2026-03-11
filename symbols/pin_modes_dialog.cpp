#include "pin_modes_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QComboBox>
#include <QPushButton>

PinModesDialog::PinModesDialog(const QJsonArray& modes, QWidget* parent) : QDialog(parent) {
    setWindowTitle("Edit Pin Modes / Alternates");
    resize(500, 400);
    
    auto* layout = new QVBoxLayout(this);
    
    m_table = new QTableWidget(0, 2);
    m_table->setHorizontalHeaderLabels({"Mode Name", "Electrical Type"});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(m_table);
    
    for (int i = 0; i < modes.size(); ++i) {
        addModeRow(modes[i].toObject());
    }
    
    auto* btnLayout = new QHBoxLayout();
    auto* addBtn = new QPushButton("Add Mode");
    auto* delBtn = new QPushButton("Remove Selected");
    btnLayout->addWidget(addBtn);
    btnLayout->addWidget(delBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);
    
    auto* okCancel = new QHBoxLayout();
    auto* okBtn = new QPushButton("Save");
    auto* cancelBtn = new QPushButton("Cancel");
    okCancel->addStretch();
    okCancel->addWidget(okBtn);
    okCancel->addWidget(cancelBtn);
    layout->addLayout(okCancel);
    
    connect(addBtn, &QPushButton::clicked, this, [this](){ addModeRow(); });
    connect(delBtn, &QPushButton::clicked, this, [this](){
        m_table->removeRow(m_table->currentRow());
    });
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    
    setStyleSheet("background-color: #1e1e1e; color: #ccc; border: 1px solid #333;");
}

QJsonArray PinModesDialog::results() const {
    QJsonArray arr;
    for (int i = 0; i < m_table->rowCount(); ++i) {
        QJsonObject obj;
        if (auto* item = m_table->item(i, 0)) {
            obj["name"] = item->text();
        }
        auto* combo = qobject_cast<QComboBox*>(m_table->cellWidget(i, 1));
        obj["type"] = combo ? combo->currentText() : "Passive";
        arr.append(obj);
    }
    return arr;
}

void PinModesDialog::addModeRow(const QJsonObject& data) {
    int row = m_table->rowCount();
    m_table->insertRow(row);
    
    m_table->setItem(row, 0, new QTableWidgetItem(data["name"].toString()));
    
    auto* combo = new QComboBox();
    combo->addItems({"Input", "Output", "Bidirectional", "Tri-state", "Passive", "Free", "Unspecified", "Power Input", "Power Output", "Open Collector", "Open Emitter"});
    combo->setCurrentText(data["type"].toString("Passive"));
    m_table->setCellWidget(row, 1, combo);
}
