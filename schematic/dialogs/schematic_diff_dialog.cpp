#include "schematic_diff_dialog.h"
#include "../ui/schematic_diff_viewer.h"
#include "../analysis/schematic_diff_engine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

SchematicDiffDialog::SchematicDiffDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Visual Schematic Diff");
    resize(1200, 800);
    setupUI();
}

void SchematicDiffDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // Toolbar-like header
    QWidget* header = new QWidget(this);
    header->setStyleSheet("background-color: #2d2d30; color: #e0e0e0; border-bottom: 1px solid #3e3e42;");
    header->setFixedHeight(40);
    QHBoxLayout* hl = new QHBoxLayout(header);
    
    m_labelA = new QLabel("BASE VERSION", this);
    m_labelA->setStyleSheet("font-weight: bold; color: #ff6b6b;");
    
    m_labelB = new QLabel("NEW VERSION", this);
    m_labelB->setStyleSheet("font-weight: bold; color: #51cf66;");

    hl->addWidget(m_labelA, 1, Qt::AlignCenter);
    hl->addWidget(m_labelB, 1, Qt::AlignCenter);
    mainLayout->addWidget(header);

    // Viewer
    m_viewer = new SchematicDiffViewer(this);
    mainLayout->addWidget(m_viewer, 1);

    // Footer / Status
    QWidget* footer = new QWidget(this);
    footer->setStyleSheet("background-color: #252526; color: #cccccc; border-top: 1px solid #3e3e42;");
    QHBoxLayout* fl = new QHBoxLayout(footer);
    
    m_statusLabel = new QLabel("No differences found.", this);
    fl->addWidget(m_statusLabel);
    fl->addStretch();

    QPushButton* closeBtn = new QPushButton("Close", this);
    closeBtn->setStyleSheet("padding: 5px 20px;");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    fl->addWidget(closeBtn);

    mainLayout->addWidget(footer);
}

void SchematicDiffDialog::compare(const QJsonObject& jsonA, const QJsonObject& jsonB, 
                                 const QString& labelA, const QString& labelB) {
    m_labelA->setText(labelA.toUpper());
    m_labelB->setText(labelB.toUpper());
    
    m_viewer->setSchematics(jsonA, jsonB);
    
    auto diffs = SchematicDiffEngine::compare(jsonA, jsonB);
    int added = 0, removed = 0, modified = 0;
    for (const auto& d : diffs) {
        if (d.type == SchematicDiffItem::Added) added++;
        else if (d.type == SchematicDiffItem::Removed) removed++;
        else if (d.type == SchematicDiffItem::Modified) modified++;
    }

    m_statusLabel->setText(QString("Diff Summary: %1 Added, %2 Removed, %3 Modified")
        .arg(added).arg(removed).arg(modified));
}
