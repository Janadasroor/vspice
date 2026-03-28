#include "subcircuit_picker_dialog.h"

#include "../../simulator/bridge/model_library_manager.h"
#include "../../core/theme_manager.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QVBoxLayout>

SubcircuitPickerDialog::SubcircuitPickerDialog(const QString& currentModel, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Pick Subcircuit");
    setModal(true);
    setMinimumSize(560, 420);

    auto* layout = new QVBoxLayout(this);

    auto* searchLayout = new QHBoxLayout();
    searchLayout->addWidget(new QLabel("Search:"));
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("Filter subcircuits by name, pin count, or library...");
    searchLayout->addWidget(m_searchEdit);
    layout->addLayout(searchLayout);

    m_modelList = new QListWidget();
    m_modelList->setAlternatingRowColors(true);
    {
        QPalette listPal = m_modelList->palette();
        listPal.setColor(QPalette::HighlightedText, listPal.color(QPalette::Text));
        m_modelList->setPalette(listPal);
    }
    layout->addWidget(m_modelList);

    m_detailLabel = new QLabel("Select a subcircuit to see details");
    {
        QFont detailFont = m_detailLabel->font();
        detailFont.setPointSizeF(qMax(8.0, detailFont.pointSizeF() - 1.0));
        m_detailLabel->setFont(detailFont);
        QPalette pal = m_detailLabel->palette();
        pal.setColor(QPalette::WindowText, palette().color(QPalette::PlaceholderText));
        m_detailLabel->setPalette(pal);
    }
    m_detailLabel->setWordWrap(true);
    layout->addWidget(m_detailLabel);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &SubcircuitPickerDialog::applySelected);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_searchEdit, &QLineEdit::textChanged, this, &SubcircuitPickerDialog::filterModels);
    connect(m_modelList, &QListWidget::itemDoubleClicked, this, &SubcircuitPickerDialog::onModelSelected);
    connect(m_modelList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current) {
        if (current) m_detailLabel->setText(current->data(Qt::UserRole + 1).toString());
    });

    if (ThemeManager::theme()) {
        setStyleSheet(ThemeManager::theme()->widgetStylesheet());
    }

    loadModels(currentModel);
}

void SubcircuitPickerDialog::loadModels(const QString& currentModel) {
    const auto allModels = ModelLibraryManager::instance().allModels();
    int selectedRow = -1;

    for (const auto& mi : allModels) {
        if (mi.type.compare("Subcircuit", Qt::CaseInsensitive) != 0) continue;
        if (mi.name.trimmed().isEmpty()) continue;

        const QString libraryFile = QFileInfo(mi.libraryPath).fileName();
        QString displayText = mi.name;
        QStringList summary;
        if (!mi.description.trimmed().isEmpty()) summary << mi.description.trimmed();
        if (!libraryFile.isEmpty()) summary << libraryFile;
        if (!summary.isEmpty()) displayText += QString("  (%1)").arg(summary.join(", "));

        auto* item = new QListWidgetItem(displayText);
        item->setData(Qt::UserRole, mi.name);
        item->setData(Qt::UserRole + 1,
                      QString("%1\nType: %2\n%3\nLibrary: %4")
                          .arg(mi.name, mi.type,
                               mi.description.trimmed().isEmpty() ? QString("No description") : mi.description.trimmed(),
                               mi.libraryPath));
        item->setToolTip(item->data(Qt::UserRole + 1).toString());
        m_modelList->addItem(item);

        if (mi.name.compare(currentModel, Qt::CaseInsensitive) == 0) {
            selectedRow = m_modelList->count() - 1;
        }
    }

    if (selectedRow >= 0) {
        m_modelList->setCurrentRow(selectedRow);
    } else if (m_modelList->count() > 0) {
        m_modelList->setCurrentRow(0);
    }
}

void SubcircuitPickerDialog::filterModels(const QString& text) {
    for (int i = 0; i < m_modelList->count(); ++i) {
        auto* item = m_modelList->item(i);
        const QString haystack = item->text() + "\n" + item->data(Qt::UserRole + 1).toString();
        const bool match = text.isEmpty() || haystack.contains(text, Qt::CaseInsensitive);
        item->setHidden(!match);
    }
}

void SubcircuitPickerDialog::onModelSelected(QListWidgetItem* item) {
    if (!item) return;
    m_selectedModel = item->data(Qt::UserRole).toString();
    accept();
}

void SubcircuitPickerDialog::applySelected() {
    auto* item = m_modelList->currentItem();
    if (item) m_selectedModel = item->data(Qt::UserRole).toString();
    accept();
}

QString SubcircuitPickerDialog::selectedModel() const {
    return m_selectedModel;
}
