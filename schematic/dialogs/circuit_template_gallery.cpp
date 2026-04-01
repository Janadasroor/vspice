#include "circuit_template_gallery.h"

#include "../io/schematic_file_io.h"
#include "../editor/schematic_editor.h"
#include "../../symbols/symbol_library.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>

CircuitTemplateGallery::CircuitTemplateGallery(const QString& projectDir, QWidget* parent)
    : QDialog(parent)
    , m_projectDir(projectDir)
    , m_categoryList(nullptr)
    , m_templateList(nullptr)
    , m_templateStack(nullptr)
    , m_thumbnailLabel(nullptr)
    , m_nameLabel(nullptr)
    , m_descriptionLabel(nullptr)
    , m_paramsTable(nullptr)
    , m_searchEdit(nullptr)
    , m_insertButton(nullptr)
    , m_saveAsButton(nullptr) {
    setupUi();
    loadTemplates();
}

CircuitTemplateGallery::~CircuitTemplateGallery() {
}

QString CircuitTemplateGallery::templatesDirectory() const {
    QString appDir = QApplication::applicationDirPath();
    QStringList paths = {
        appDir + "/../templates/circuits",
        appDir + "/templates/circuits",
        QDir::home().filePath(".viospice/templates/circuits")
    };
    
    for (const QString& path : paths) {
        if (QFile::exists(path)) return path;
    }
    
    return paths.first();
}

QString CircuitTemplateGallery::userTemplatesDirectory() const {
    QString userDir = QDir::home().filePath(".viospice/user_templates");
    QDir().mkpath(userDir);
    return userDir;
}

void CircuitTemplateGallery::setupUi() {
    setWindowTitle("Circuit Templates Gallery");
    resize(1000, 700);

    auto* mainLayout = new QHBoxLayout(this);

    auto* leftPanel = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Search templates...");
    m_searchEdit->setMinimumWidth(200);
    leftLayout->addWidget(m_searchEdit);
    
    m_categoryList = new QListWidget(this);
    m_categoryList->setMaximumWidth(200);
    leftLayout->addWidget(m_categoryList);
    
    mainLayout->addWidget(leftPanel);

    auto* centerPanel = new QWidget(this);
    auto* centerLayout = new QVBoxLayout(centerPanel);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    
    m_templateList = new QListWidget(this);
    centerLayout->addWidget(m_templateList);
    
    mainLayout->addWidget(centerPanel, 1);

    auto* rightPanel = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    
    m_thumbnailLabel = new QLabel(this);
    m_thumbnailLabel->setMinimumSize(300, 200);
    m_thumbnailLabel->setAlignment(Qt::AlignCenter);
    m_thumbnailLabel->setStyleSheet("background: #1e1e1e; border: 1px solid #333;");
    rightLayout->addWidget(m_thumbnailLabel);
    
    m_nameLabel = new QLabel("Select a template", this);
    m_nameLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #58a6ff;");
    rightLayout->addWidget(m_nameLabel);
    
    m_descriptionLabel = new QLabel("", this);
    m_descriptionLabel->setWordWrap(true);
    m_descriptionLabel->setStyleSheet("color: #8b949e;");
    rightLayout->addWidget(m_descriptionLabel);
    
    auto* paramsLabel = new QLabel("Parameters:", this);
    paramsLabel->setStyleSheet("font-weight: bold; margin-top: 10px;");
    rightLayout->addWidget(paramsLabel);
    
    m_paramsTable = new QTableWidget(0, 3, this);
    m_paramsTable->setHorizontalHeaderLabels({"Name", "Value", "Unit"});
    m_paramsTable->horizontalHeader()->setStretchLastSection(true);
    m_paramsTable->setMinimumHeight(150);
    rightLayout->addWidget(m_paramsTable, 1);
    
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_insertButton = buttonBox->button(QDialogButtonBox::Ok);
    m_insertButton->setText("Insert Template");
    m_insertButton->setEnabled(false);
    
    m_saveAsButton = new QPushButton("Save Current as Template", this);
    
    rightLayout->addWidget(m_saveAsButton);
    rightLayout->addWidget(buttonBox);
    
    mainLayout->addWidget(rightPanel, 1);

    connect(m_searchEdit, &QLineEdit::textChanged, this, &CircuitTemplateGallery::onSearchTextChanged);
    connect(m_categoryList, &QListWidget::currentItemChanged, this, &CircuitTemplateGallery::onCategorySelected);
    connect(m_templateList, &QListWidget::currentItemChanged, this, &CircuitTemplateGallery::onTemplateSelected);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &CircuitTemplateGallery::onInsertClicked);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_saveAsButton, &QPushButton::clicked, this, &CircuitTemplateGallery::onSaveAsTemplateClicked);
}

void CircuitTemplateGallery::loadTemplates() {
    m_templatesByCategory.clear();
    m_allTemplates.clear();
    
    QStringList templateDirs = {
        templatesDirectory(),
        userTemplatesDirectory()
    };
    
    for (const QString& dirPath : templateDirs) {
        QDir templatesDir(dirPath);
        if (!templatesDir.exists()) continue;
        
        QFileInfoList categories = templatesDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& catInfo : categories) {
            QString category = catInfo.filePath();
            QString categoryName = catInfo.fileName();
            
            QDir catDir(category);
            QStringList nameFilters = {"*.sch", "*.flxsch"};
            QFileInfoList files = catDir.entryInfoList(nameFilters, QDir::Files);
            for (const QFileInfo& fi : files) {
                QString schFile = fi.filePath();
                Template tpl = parseTemplateFromFile(schFile);
                tpl.category = categoryName;
                m_templatesByCategory[categoryName].append(tpl);
                m_allTemplates.append(tpl);
            }
        }
    }
    
    loadTemplateMetadata();
    populateCategories();
}

void CircuitTemplateGallery::loadTemplateMetadata() {
    QFile metaFile(templatesDirectory() + "/metadata.json");
    if (metaFile.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll());
        m_metadata = doc.object();
        metaFile.close();
    }
    
    QFile userMetaFile(userTemplatesDirectory() + "/metadata.json");
    if (userMetaFile.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(userMetaFile.readAll());
        QJsonObject userMeta = doc.object();
        for (auto it = userMeta.begin(); it != userMeta.end(); ++it) {
            m_metadata[it.key()] = it.value();
        }
        userMetaFile.close();
    }
}

CircuitTemplateGallery::Template CircuitTemplateGallery::parseTemplateFromFile(const QString& filePath) const {
    Template tpl;
    QFileInfo info(filePath);
    tpl.id = info.baseName();
    tpl.name = info.baseName().replace("_", " ").replace(0, 1, tpl.id[0].toUpper());
    tpl.filePath = filePath;
    tpl.thumbnailPath = filePath + ".png";
    tpl.hasSimulation = false;
    
    QJsonObject tplMeta = m_metadata.value(tpl.id).toObject();
    if (!tplMeta.isEmpty()) {
        tpl.name = tplMeta.value("name").toString(tpl.name);
        tpl.description = tplMeta.value("description").toString(tpl.description);
        QJsonArray tagsArray = tplMeta.value("tags").toArray();
        QStringList tagsList;
        for (const QJsonValue& tv : tagsArray) tagsList.append(tv.toString());
        tpl.tags = tagsList;
        
        QJsonArray params = tplMeta.value("parameters").toArray();
        for (const QJsonValue& pv : params) {
            QJsonObject p = pv.toObject();
            Parameter param;
            param.name = p.value("name").toString();
            param.defaultValue = p.value("default").toString();
            param.displayName = p.value("label").toString(param.name);
            param.unit = p.value("unit").toString();
            param.description = p.value("description").toString();
            tpl.parameters.append(param);
        }
        
        tpl.hasSimulation = tplMeta.value("hasSimulation").toBool(false);
        tpl.simulationType = tplMeta.value("simulationType").toString();
    }
    
    return tpl;
}

void CircuitTemplateGallery::populateCategories() {
    m_categoryList->clear();
    
    QListWidgetItem* allItem = new QListWidgetItem("All Templates", m_categoryList);
    allItem->setData(Qt::UserRole, "all");
    
    QStringList categories = m_templatesByCategory.keys();
    categories.sort();
    for (const QString& cat : categories) {
        QListWidgetItem* item = new QListWidgetItem(cat, m_categoryList);
        item->setData(Qt::UserRole, cat);
    }
    
    if (m_categoryList->count() > 0) {
        m_categoryList->setCurrentRow(0);
    }
}

void CircuitTemplateGallery::populateTemplatesForCategory(const QString& category) {
    m_templateList->clear();
    
    QList<Template> templates;
    if (category == "all" || category.isEmpty()) {
        templates = m_allTemplates;
    } else {
        templates = m_templatesByCategory.value(category);
    }
    
    for (const Template& tpl : templates) {
        QListWidgetItem* item = new QListWidgetItem(tpl.name, m_templateList);
        item->setData(Qt::UserRole, QVariant::fromValue(tpl));
    }
}

void CircuitTemplateGallery::populateSearchResults(const QString& query) {
    m_templateList->clear();
    
    if (query.isEmpty()) {
        populateTemplatesForCategory("all");
        return;
    }
    
    QString lowerQuery = query.toLower();
    for (const Template& tpl : m_allTemplates) {
        bool matches = tpl.name.toLower().contains(lowerQuery) ||
                       tpl.description.toLower().contains(lowerQuery) ||
                       tpl.tags.contains(lowerQuery);
        if (matches) {
            QListWidgetItem* item = new QListWidgetItem(tpl.name, m_templateList);
            item->setData(Qt::UserRole, QVariant::fromValue(tpl));
        }
    }
}

void CircuitTemplateGallery::onCategorySelected(QListWidgetItem* item) {
    if (!item) return;
    QString category = item->data(Qt::UserRole).toString();
    populateTemplatesForCategory(category);
}

void CircuitTemplateGallery::onTemplateSelected(QListWidgetItem* item) {
    if (!item) {
        m_selectedTemplate = Template();
        m_insertButton->setEnabled(false);
        return;
    }
    
    m_selectedTemplate = item->data(Qt::UserRole).value<Template>();
    m_nameLabel->setText(m_selectedTemplate.name);
    m_descriptionLabel->setText(m_selectedTemplate.description);
    
    if (!QFile::exists(m_selectedTemplate.thumbnailPath)) {
        renderThumbnail(m_selectedTemplate.filePath, m_selectedTemplate.thumbnailPath);
    }
    
    if (QFile::exists(m_selectedTemplate.thumbnailPath)) {
        QImage thumb(m_selectedTemplate.thumbnailPath);
        if (!thumb.isNull()) {
            m_thumbnailLabel->setPixmap(QPixmap::fromImage(thumb).scaled(300, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    } else {
        m_thumbnailLabel->setText("No preview\n(Unsaved template)");
        m_thumbnailLabel->setStyleSheet("background: #1e1e1e; border: 1px solid #333; color: #666;");
    }
    
    m_paramsTable->setRowCount(0);
    m_customParameters.clear();
    
    for (const Parameter& param : m_selectedTemplate.parameters) {
        int row = m_paramsTable->rowCount();
        m_paramsTable->insertRow(row);
        
        auto* nameItem = new QTableWidgetItem(param.displayName);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        nameItem->setData(Qt::UserRole, param.name);
        m_paramsTable->setItem(row, 0, nameItem);
        
        auto* valueItem = new QTableWidgetItem(param.defaultValue);
        valueItem->setData(Qt::UserRole, param.name);
        m_paramsTable->setItem(row, 1, valueItem);
        
        auto* unitItem = new QTableWidgetItem(param.unit);
        unitItem->setFlags(unitItem->flags() & ~Qt::ItemIsEditable);
        m_paramsTable->setItem(row, 2, unitItem);
    }
    
    m_insertButton->setEnabled(true);
}

void CircuitTemplateGallery::onSearchTextChanged(const QString& text) {
    populateSearchResults(text);
}

void CircuitTemplateGallery::onInsertClicked() {
    m_customParameters.clear();
    for (int row = 0; row < m_paramsTable->rowCount(); ++row) {
        QString name = m_paramsTable->item(row, 1)->data(Qt::UserRole).toString();
        QString value = m_paramsTable->item(row, 1)->text();
        if (!name.isEmpty() && !value.isEmpty()) {
            m_customParameters[name] = value;
        }
    }
    accept();
}

void CircuitTemplateGallery::onSaveAsTemplateClicked() {
    QString filePath = QFileDialog::getOpenFileName(this, "Select Schematic to Save as Template",
        m_projectDir, "Schematic Files (*.sch *.flxsch)");
    
    if (filePath.isEmpty()) return;
    
    saveUserTemplate(filePath);
}

void CircuitTemplateGallery::saveUserTemplate(const QString& sourceSchematic) {
    bool ok = false;
    QString name = QInputDialog::getText(this, "Save as Template",
        "Template Name:", QLineEdit::Normal, "", &ok);
    if (!ok || name.isEmpty()) return;
    
    QString id = name.toLower().replace(" ", "_");
    QString category = "user";
    
    QString destDir = userTemplatesDirectory() + "/" + category;
    QDir().mkpath(destDir);
    
    QString destPath = destDir + "/" + id + ".sch";
    
    if (QFile::exists(destPath)) {
        QMessageBox::warning(this, "Template Exists", "A template with this name already exists.");
        return;
    }
    
    if (!QFile::copy(sourceSchematic, destPath)) {
        QMessageBox::critical(this, "Error", "Failed to copy schematic to templates directory.");
        return;
    }
    
    QString thumbPath = destPath + ".png";
    renderThumbnail(sourceSchematic, thumbPath);
    
    QString metaPath = userTemplatesDirectory() + "/metadata.json";
    QJsonObject meta;
    
    QFile metaFile(metaPath);
    if (metaFile.open(QIODevice::ReadOnly)) {
        meta = QJsonDocument::fromJson(metaFile.readAll()).object();
        metaFile.close();
    }
    
    QJsonObject tplMeta;
    tplMeta["name"] = name;
    tplMeta["category"] = category;
    tplMeta["description"] = name + " (user-created template)";
    tplMeta["tags"] = QJsonArray{"user", "custom"};
    meta[id] = tplMeta;
    
    if (metaFile.open(QIODevice::WriteOnly)) {
        metaFile.write(QJsonDocument(meta).toJson(QJsonDocument::Indented));
        metaFile.close();
    }
    
    loadTemplates();
    populateCategories();
    
    QMessageBox::information(this, "Template Saved", "Template saved successfully!");
}

void CircuitTemplateGallery::renderThumbnail(const QString& schematicPath, const QString& thumbPath) {
    QGraphicsScene scene;
    QString pageSize;
    TitleBlockData dummyTB;
    
    if (!SchematicFileIO::loadSchematic(&scene, schematicPath, pageSize, dummyTB)) {
        return;
    }
    
    QRectF rect = scene.itemsBoundingRect();
    if (rect.isEmpty()) rect = QRectF(-50, -50, 100, 100);
    rect.adjust(-10, -10, 10, 10);
    
    const qreal scale = 0.5;
    QImage image(QSize(300, 200), QImage::Format_ARGB32);
    image.fill(QColor(30, 30, 34));
    
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    scene.render(&painter, QRectF(), rect);
    painter.end();
    
    image.save(thumbPath);
}

void CircuitTemplateGallery::onRefreshTemplates() {
    loadTemplates();
    populateCategories();
}