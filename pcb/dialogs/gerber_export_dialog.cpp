#include "gerber_export_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QMessageBox>
#include <QStandardPaths>
#include "../gerber/gerber_exporter.h"

GerberExportDialog::GerberExportDialog(QWidget* parent)
    : QDialog(parent) {
    setupUI();
    populateLayers();
}

GerberExportDialog::~GerberExportDialog() {}

void GerberExportDialog::setupUI() {
    setWindowTitle("Generate Gerber Files");
    resize(500, 600);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);

    // 1. Output Directory
    QGroupBox* dirGroup = new QGroupBox("Output Settings");
    QHBoxLayout* dirLayout = new QHBoxLayout(dirGroup);
    m_dirEdit = new QLineEdit();
    m_dirEdit->setText(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/Gerbers");
    QPushButton* browseBtn = new QPushButton("Browse...");
    connect(browseBtn, &QPushButton::clicked, this, &GerberExportDialog::onBrowse);
    dirLayout->addWidget(m_dirEdit);
    dirLayout->addWidget(browseBtn);
    mainLayout->addWidget(dirGroup);

    // 2. Layer Selection
    QGroupBox* layerGroup = new QGroupBox("Select Layers to Plot");
    QVBoxLayout* layerLayout = new QVBoxLayout(layerGroup);
    m_layerList = new QListWidget();
    m_layerList->setSelectionMode(QAbstractItemView::MultiSelection);
    layerLayout->addWidget(m_layerList);
    mainLayout->addWidget(layerGroup);

    // 3. Drill Options
    m_drillCheck = new QCheckBox("Generate Excellon Drill File (.drl)");
    m_drillCheck->setChecked(true);
    mainLayout->addWidget(m_drillCheck);

    // 4. Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* exportBtn = new QPushButton("Generate Files");
    QPushButton* cancelBtn = new QPushButton("Cancel");
    exportBtn->setStyleSheet("background-color: #007acc; color: white; font-weight: bold; padding: 8px;");
    
    connect(exportBtn, &QPushButton::clicked, this, &GerberExportDialog::onExport);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    
    btnLayout->addStretch();
    btnLayout->addWidget(exportBtn);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);
}

void GerberExportDialog::populateLayers() {
    for (const auto& layer : PCBLayerManager::instance().layers()) {
        if (layer.type() == PCBLayer::Copper || layer.type() == PCBLayer::Silkscreen || 
            layer.type() == PCBLayer::Soldermask || layer.type() == PCBLayer::EdgeCuts) {
            QListWidgetItem* item = new QListWidgetItem(layer.name());
            item->setData(Qt::UserRole, layer.id());
            m_layerList->addItem(item);
            // Auto-select common layers
            if (layer.type() != PCBLayer::UserDefined) item->setSelected(true);
        }
    }
}

void GerberExportDialog::onBrowse() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Output Directory", m_dirEdit->text());
    if (!dir.isEmpty()) m_dirEdit->setText(dir);
}

void GerberExportDialog::onExport() {
    QDir dir(m_dirEdit->text());
    if (!dir.exists()) dir.mkpath(".");

    accept();
}

QList<int> GerberExportDialog::selectedLayers() const {
    QList<int> ids;
    for (auto* item : m_layerList->selectedItems()) {
        ids.append(item->data(Qt::UserRole).toInt());
    }
    return ids;
}

QString GerberExportDialog::outputDirectory() const {
    return m_dirEdit->text();
}
