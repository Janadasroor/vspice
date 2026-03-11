#include "find_replace_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDebug>

FindReplaceDialog::FindReplaceDialog(QWidget* parent) : QDialog(parent) {
    setupUI();
}

void FindReplaceDialog::setProjectContext(const QString& currentFile, const QString& projectDir) {
    m_currentFile = currentFile;
    m_projectDir = projectDir;
}

void FindReplaceDialog::setupUI() {
    setWindowTitle("Find and Replace");
    resize(600, 500);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QFormLayout* inputForm = new QFormLayout();
    m_findEdit = new QLineEdit();
    m_findEdit->setPlaceholderText("Search term...");
    inputForm->addRow("Find:", m_findEdit);

    m_replaceEdit = new QLineEdit();
    m_replaceEdit->setPlaceholderText("Replace with...");
    inputForm->addRow("Replace:", m_replaceEdit);

    m_scopeCombo = new QComboBox();
    m_scopeCombo->addItems({"Current Sheet", "Entire Project"});
    inputForm->addRow("Scope:", m_scopeCombo);

    mainLayout->addLayout(inputForm);

    QHBoxLayout* optsLayout = new QHBoxLayout();
    m_matchCase = new QCheckBox("Match Case");
    m_wholeWord = new QCheckBox("Whole Word");
    optsLayout->addWidget(m_matchCase);
    optsLayout->addWidget(m_wholeWord);
    optsLayout->addStretch();
    mainLayout->addLayout(optsLayout);

    QHBoxLayout* searchBtns = new QHBoxLayout();
    QPushButton* searchBtn = new QPushButton("Search");
    searchBtn->setDefault(true);
    connect(searchBtn, &QPushButton::clicked, this, &FindReplaceDialog::onSearch);
    searchBtns->addWidget(searchBtn);
    mainLayout->addLayout(searchBtns);

    mainLayout->addWidget(new QLabel("Results:"));
    m_resultsList = new QListWidget();
    connect(m_resultsList, &QListWidget::itemDoubleClicked, this, &FindReplaceDialog::onResultDoubleClicked);
    mainLayout->addWidget(m_resultsList);

    m_statusLabel = new QLabel("Ready.");
    mainLayout->addWidget(m_statusLabel);

    QHBoxLayout* actionBtns = new QHBoxLayout();
    QPushButton* replaceBtn = new QPushButton("Replace Selected");
    connect(replaceBtn, &QPushButton::clicked, this, &FindReplaceDialog::onReplaceSelected);
    QPushButton* replaceAllBtn = new QPushButton("Replace All");
    connect(replaceAllBtn, &QPushButton::clicked, this, &FindReplaceDialog::onReplaceAll);
    actionBtns->addWidget(replaceBtn);
    actionBtns->addWidget(replaceAllBtn);
    actionBtns->addStretch();
    QPushButton* closeBtn = new QPushButton("Close");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    actionBtns->addWidget(closeBtn);
    mainLayout->addLayout(actionBtns);
    
    setStyleSheet("QListWidget::item { padding: 4px; border-bottom: 1px solid #333; }");
}

void FindReplaceDialog::onSearch() {
    QString term = m_findEdit->text();
    if (term.isEmpty()) return;

    m_results.clear();
    m_resultsList->clear();
    m_statusLabel->setText("Searching...");

    if (m_scopeCombo->currentIndex() == 0) {
        // Current Sheet
        searchInFile(m_currentFile, term, "Current");
    } else {
        // Entire Project (recursive scan)
        performSearch(term, m_currentFile);
    }

    for (int i = 0; i < m_results.size(); ++i) {
        const auto& res = m_results[i];
        QListWidgetItem* item = new QListWidgetItem();
        item->setText(QString("%1: %2 [%3]").arg(res.label, res.value, res.sheetPath));
        item->setData(Qt::UserRole, i);
        m_resultsList->addItem(item);
    }

    m_statusLabel->setText(QString("Found %1 matches.").arg(m_results.size()));
}

void FindReplaceDialog::performSearch(const QString& term, const QString& rootFile) {
    QSet<QString> visited;
    QList<QPair<QString, QString>> queue; // {filepath, pathInHierarchy}
    queue.append({rootFile, "Root"});

    while (!queue.isEmpty()) {
        auto current = queue.takeFirst();
        QString absPath = QFileInfo(current.first).absoluteFilePath();
        if (visited.contains(absPath)) continue;
        visited.insert(absPath);

        searchInFile(absPath, term, current.second);

        // Find child sheets to recurse
        QFile file(absPath);
        if (file.open(QIODevice::ReadOnly)) {
            QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
            QJsonArray items = root["items"].toArray();
            for (int i = 0; i < items.size(); ++i) {
                QJsonObject obj = items[i].toObject();
                if (obj["type"].toString() == "Sheet") {
                    QString childFile = obj["fileName"].toString();
                    if (QFileInfo(childFile).isRelative()) {
                        childFile = QFileInfo(absPath).absolutePath() + "/" + childFile;
                    }
                    queue.append({childFile, current.second + "/" + obj["sheetName"].toString()});
                }
            }
        }
    }
}

void FindReplaceDialog::searchInFile(const QString& filePath, const QString& term, const QString& sheetPath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return;

    Qt::CaseSensitivity cs = m_matchCase->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;

    QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    QJsonArray items = root["items"].toArray();

    for (int i = 0; i < items.size(); ++i) {
        QJsonObject obj = items[i].toObject();
        QUuid id = QUuid::fromString(obj["id"].toString());
        
        auto check = [&](const QString& val, const QString& context, const QString& label) {
            if (val.contains(term, cs)) {
                SearchResult res;
                res.label = label;
                res.value = val;
                res.sheetPath = sheetPath;
                res.fileName = filePath;
                res.itemId = id;
                res.context = context;
                m_results.append(res);
            }
        };

        QString type = obj["type"].toString();
        QString ref = obj["reference"].toString();
        QString val = obj["value"].toString();
        QString name = obj["name"].toString();

        if (!ref.isEmpty()) check(ref, "Reference", ref + " (" + type + ")");
        if (!val.isEmpty()) check(val, "Value", ref + " (" + type + ")");
        if (type == "Label") check(obj["text"].toString(), "Text", "Text Label");
        if (type == "Net Label") check(val, "Net", "Net Label");
    }
}

void FindReplaceDialog::onResultDoubleClicked(QListWidgetItem* item) {
    int idx = item->data(Qt::UserRole).toInt();
    if (idx >= 0 && idx < m_results.size()) {
        emit navigateToResult(m_results[idx]);
    }
}

void FindReplaceDialog::onReplaceSelected() {
    QListWidgetItem* item = m_resultsList->currentItem();
    if (!item) return;

    int idx = item->data(Qt::UserRole).toInt();
    if (idx >= 0 && idx < m_results.size()) {
        emit replaceRequested(m_results[idx], m_replaceEdit->text());
        onSearch(); // Refresh
    }
}

void FindReplaceDialog::onReplaceAll() {
    QString newVal = m_replaceEdit->text();
    for (const auto& res : m_results) {
        emit replaceRequested(res, newVal);
    }
    onSearch(); // Refresh
}
