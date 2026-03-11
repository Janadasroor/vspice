#include "symbol_field_editor_dialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QHeaderView>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMessageBox>
#include <QDir>
#include <QSet>
#include <algorithm>

SymbolFieldEditorDialog::SymbolFieldEditorDialog(const QString& rootPath, const QString& projectDir, QWidget* parent)
    : QDialog(parent), m_rootPath(rootPath), m_projectDir(projectDir), m_table(nullptr) {
    setupUI();
    scanProject();
    populateTable();
}

SymbolFieldEditorDialog::~SymbolFieldEditorDialog() {}

void SymbolFieldEditorDialog::setupUI() {
    setWindowTitle("Project Symbol Field Editor");
    resize(1180, 680);

    setStyleSheet(
        "QDialog { background-color: #1e1e1e; color: #ffffff; }"
        "QTableWidget { background-color: #121212; color: #e0e0e0; gridline-color: #333333; border: 1px solid #333333; selection-background-color: #094771; }"
        "QHeaderView::section { background-color: #2d2d30; color: #aaaaaa; padding: 6px; border: 1px solid #1e1e1e; font-weight: bold; font-size: 11px; }"
        "QPushButton { background-color: #3c3c3c; color: white; border: 1px solid #555; padding: 8px 15px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #4c4c4c; }"
        "QPushButton#saveBtn { background-color: #0d9488; border-color: #0d9488; }");

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(15, 15, 15, 15);
    layout->setSpacing(10);

    QLabel* title = new QLabel("Global Component Field Editor");
    title->setStyleSheet("font-size: 18px; font-weight: bold; color: #3b82f6;");
    layout->addWidget(title);

    m_summaryLabel = new QLabel();
    m_summaryLabel->setStyleSheet("color: #9aa0a6; font-size: 12px;");
    layout->addWidget(m_summaryLabel);

    m_table = new QTableWidget(this);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(false);
    layout->addWidget(m_table);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    QPushButton* cancelBtn = new QPushButton("Cancel", this);
    QPushButton* saveBtn = new QPushButton("Apply Project Changes", this);
    saveBtn->setObjectName("saveBtn");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(saveBtn, &QPushButton::clicked, this, &SymbolFieldEditorDialog::onSave);
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(saveBtn);
    layout->addLayout(btnLayout);
}

static bool isLikelyComponentJson(const QJsonObject& obj) {
    const QString type = obj.value("type").toString();
    static const QSet<QString> skipTypes = {
        "Wire", "Bus", "Junction", "NoConnect", "No Connect",
        "Label", "Net Label", "Global Label",
        "Text", "Shape", "Sheet", "HierarchicalPort", "Hierarchical Port",
        "Page", "SimulationOverlay", "WaveformMarker"
    };
    if (skipTypes.contains(type)) return false;
    if (type == "Power") return false;
    return obj.contains("reference") || obj.contains("value") ||
           obj.contains("manufacturer") || obj.contains("mpn");
}

void SymbolFieldEditorDialog::scanProject() {
    if (m_rootPath.isEmpty()) return;

    QSet<QString> visited;
    QList<QString> queue;
    queue.append(QFileInfo(m_rootPath).absoluteFilePath());

    while (!queue.isEmpty()) {
        const QString currentPath = queue.takeFirst();
        if (visited.contains(currentPath)) continue;
        visited.insert(currentPath);

        QFile file(currentPath);
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (!doc.isObject()) continue;

        QJsonObject root = doc.object();
        m_fileRoots[currentPath] = root;
        const QJsonArray items = root.value("items").toArray();
        const QString currentDir = QFileInfo(currentPath).absolutePath();

        for (int i = 0; i < items.size(); ++i) {
            const QJsonObject obj = items[i].toObject();
            if (obj.value("type").toString() == "Sheet") {
                const QString subFile = obj.value("fileName").toString().trimmed();
                if (subFile.isEmpty()) continue;
                const QString resolved = QFileInfo(QDir(currentDir).filePath(subFile)).absoluteFilePath();
                if (QFile::exists(resolved) && !visited.contains(resolved)) {
                    queue.append(resolved);
                }
                continue;
            }

            if (!isLikelyComponentJson(obj)) continue;

            ComponentData cd;
            cd.filePath = currentPath;
            cd.indexInFile = i;
            cd.type = obj.value("type").toString();
            cd.reference = obj.value("reference").toString();
            cd.value = obj.value("value").toString();
            cd.manufacturer = obj.value("manufacturer").toString();
            cd.mpn = obj.value("mpn").toString();
            cd.description = obj.value("description").toString();
            cd.originalReference = cd.reference;

            const QJsonObject customObj = obj.value("customFields").toObject();
            for (auto it = customObj.begin(); it != customObj.end(); ++it) {
                const QString key = it.key().trimmed();
                if (key.isEmpty()) continue;
                cd.customFields[key] = it.value().toString();
                if (!m_customFieldKeys.contains(key)) m_customFieldKeys.append(key);
            }

            m_components.append(cd);
        }
    }

    std::sort(m_components.begin(), m_components.end(), [](const ComponentData& a, const ComponentData& b) {
        if (a.reference == b.reference) return a.filePath < b.filePath;
        if (a.reference.isEmpty()) return false;
        if (b.reference.isEmpty()) return true;
        return a.reference < b.reference;
    });

    std::sort(m_customFieldKeys.begin(), m_customFieldKeys.end(), [](const QString& a, const QString& b) {
        return a.toLower() < b.toLower();
    });
}

void SymbolFieldEditorDialog::populateTable() {
    QStringList headers = {
        "Reference", "Value", "Manufacturer", "MPN", "Description", "Sheet", "Symbol"
    };
    headers.append(m_customFieldKeys);
    m_table->setColumnCount(headers.size());
    m_table->setHorizontalHeaderLabels(headers);
    m_table->setRowCount(m_components.size());

    for (int row = 0; row < m_components.size(); ++row) {
        const ComponentData& cd = m_components[row];
        m_table->setItem(row, ColReference, new QTableWidgetItem(cd.reference));
        m_table->setItem(row, ColValue, new QTableWidgetItem(cd.value));
        m_table->setItem(row, ColManufacturer, new QTableWidgetItem(cd.manufacturer));
        m_table->setItem(row, ColMpn, new QTableWidgetItem(cd.mpn));
        m_table->setItem(row, ColDescription, new QTableWidgetItem(cd.description));

        QTableWidgetItem* sheetItem = new QTableWidgetItem(QFileInfo(cd.filePath).fileName());
        sheetItem->setFlags(sheetItem->flags() & ~Qt::ItemIsEditable);
        sheetItem->setForeground(QBrush(QColor("#888888")));
        m_table->setItem(row, ColSheet, sheetItem);

        QTableWidgetItem* typeItem = new QTableWidgetItem(cd.type);
        typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
        typeItem->setForeground(QBrush(QColor("#888888")));
        m_table->setItem(row, ColType, typeItem);

        for (int i = 0; i < m_customFieldKeys.size(); ++i) {
            const QString key = m_customFieldKeys[i];
            m_table->setItem(row, ColBaseCount + i, new QTableWidgetItem(cd.customFields.value(key)));
        }
    }

    m_table->horizontalHeader()->setSectionResizeMode(ColReference, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColValue, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColManufacturer, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColMpn, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColDescription, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColSheet, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColType, QHeaderView::ResizeToContents);
    for (int col = ColBaseCount; col < m_table->columnCount(); ++col) {
        m_table->horizontalHeader()->setSectionResizeMode(col, QHeaderView::ResizeToContents);
    }

    if (m_summaryLabel) {
        m_summaryLabel->setText(QString("Loaded %1 component rows across %2 sheet file(s)")
            .arg(m_components.size()).arg(m_fileRoots.size()));
    }
}

QString SymbolFieldEditorDialog::cellText(int row, int col) const {
    if (!m_table) return QString();
    QTableWidgetItem* item = m_table->item(row, col);
    return item ? item->text().trimmed() : QString();
}

void SymbolFieldEditorDialog::onCellChanged(int row, int col) {
    Q_UNUSED(row);
    Q_UNUSED(col);
}

void SymbolFieldEditorDialog::onSave() {
    if (!m_table) {
        reject();
        return;
    }

    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (row >= m_components.size()) break;
        ComponentData& cd = m_components[row];
        QJsonObject& root = m_fileRoots[cd.filePath];
        QJsonArray items = root.value("items").toArray();
        if (cd.indexInFile < 0 || cd.indexInFile >= items.size()) continue;

        QJsonObject itemObj = items[cd.indexInFile].toObject();
        itemObj["reference"] = cellText(row, ColReference);
        itemObj["value"] = cellText(row, ColValue);
        itemObj["manufacturer"] = cellText(row, ColManufacturer);
        itemObj["mpn"] = cellText(row, ColMpn);
        itemObj["description"] = cellText(row, ColDescription);

        QJsonObject customObj = itemObj.value("customFields").toObject();
        for (int i = 0; i < m_customFieldKeys.size(); ++i) {
            const QString key = m_customFieldKeys[i];
            const QString value = cellText(row, ColBaseCount + i);
            if (value.isEmpty()) customObj.remove(key);
            else customObj[key] = value;
        }
        if (customObj.isEmpty()) itemObj.remove("customFields");
        else itemObj["customFields"] = customObj;

        items[cd.indexInFile] = itemObj;
        root["items"] = items;
    }

    int savedFiles = 0;
    QStringList failedFiles;
    for (auto it = m_fileRoots.begin(); it != m_fileRoots.end(); ++it) {
        QFile file(it.key());
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            failedFiles.append(QFileInfo(it.key()).fileName());
            continue;
        }
        file.write(QJsonDocument(it.value()).toJson(QJsonDocument::Indented));
        file.close();
        ++savedFiles;
    }

    if (!failedFiles.isEmpty()) {
        QMessageBox::warning(this, "Partial Save",
                             QString("Saved %1 file(s), failed: %2")
                                 .arg(savedFiles)
                                 .arg(failedFiles.join(", ")));
    } else {
        QMessageBox::information(this, "Saved",
                                 QString("Updated %1 component rows across %2 file(s).")
                                     .arg(m_components.size())
                                     .arg(savedFiles));
    }
    accept();
}
