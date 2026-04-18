#include "mos_model_picker_dialog.h"

#include "../../simulator/bridge/model_library_manager.h"
#include "../../simulator/bridge/spice_model_search.h"
#include "theme_manager.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QVBoxLayout>

MosModelPickerDialog::MosModelPickerDialog(bool pmos, QWidget* parent)
    : QDialog(parent), m_pmos(pmos) {
    setWindowTitle(m_pmos ? "Pick PMOS Model" : "Pick NMOS Model");
    setModal(true);
    setMinimumSize(500, 400);

    auto* layout = new QVBoxLayout(this);

    auto* searchLayout = new QHBoxLayout();
    searchLayout->addWidget(new QLabel("Search:"));
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("Filter models by name...");
    searchLayout->addWidget(m_searchEdit);
    layout->addLayout(searchLayout);

    m_modelList = new QListWidget();
    layout->addWidget(m_modelList);

    m_detailLabel = new QLabel("Select a model to see details");
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
    connect(buttons, &QDialogButtonBox::accepted, this, &MosModelPickerDialog::applySelected);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_searchEdit, &QLineEdit::textChanged, this, &MosModelPickerDialog::filterModels);
    connect(m_modelList, &QListWidget::itemDoubleClicked, this, &MosModelPickerDialog::onModelSelected);
    connect(m_modelList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current) {
        if (current) {
            m_detailLabel->setText(current->data(Qt::UserRole + 1).toString());
        }
    });

    if (ThemeManager::theme()) {
        setStyleSheet(ThemeManager::theme()->widgetStylesheet());
    }

    loadModels();
}

void MosModelPickerDialog::loadModels() {
    const QString wantedType = m_pmos ? "PMOS" : "NMOS";

    // Use smart search with empty query to get all models sorted by name
    const auto results = SpiceModelSearch::search("", wantedType);

    for (const auto& scored : results) {
        const auto& mi = scored.info;
        QString params = mi.params.join(", ");
        if (params.length() > 120) params = params.left(117) + "...";

        auto* item = new QListWidgetItem(mi.name);
        item->setData(Qt::UserRole, mi.name);
        item->setData(
            Qt::UserRole + 1,
            QString("%1\nType: %2\nParams: %3\nLibrary: %4")
                .arg(mi.name, wantedType, params, QFileInfo(mi.libraryPath).fileName()));
        m_modelList->addItem(item);
    }

    if (m_modelList->count() > 0) {
        m_modelList->setCurrentRow(0);
    }
}

void MosModelPickerDialog::filterModels(const QString& text) {
    const QString wantedType = m_pmos ? "PMOS" : "NMOS";

    // Use smart search which handles semantic keywords
    const auto results = SpiceModelSearch::search(text, wantedType);

    // Hide all items first
    for (int i = 0; i < m_modelList->count(); ++i) {
        m_modelList->item(i)->setHidden(true);
    }

    // Build a set of matching model names (preserve order from smart search)
    QSet<QString> matchingNames;
    for (const auto& scored : results) {
        matchingNames.insert(scored.info.name);
    }

    // Show matching items in smart search order
    int showIndex = 0;
    for (const auto& scored : results) {
        for (int i = 0; i < m_modelList->count(); ++i) {
            auto* item = m_modelList->item(i);
            if (item->data(Qt::UserRole).toString() == scored.info.name) {
                item->setHidden(false);
                // Move to front in result order
                m_modelList->takeItem(i);
                m_modelList->insertItem(showIndex++, item);
                if (showIndex == 1) m_modelList->setCurrentItem(item);
                break;
            }
        }
    }

    // Update detail label
    auto* current = m_modelList->currentItem();
    if (current) {
        m_detailLabel->setText(current->data(Qt::UserRole + 1).toString());
    } else {
        m_detailLabel->setText(text.isEmpty() ? "Select a model to see details" : "No matching models");
    }
}

void MosModelPickerDialog::onModelSelected(QListWidgetItem* item) {
    if (!item) return;
    m_selectedModel = item->data(Qt::UserRole).toString();
    accept();
}

void MosModelPickerDialog::applySelected() {
    auto* item = m_modelList->currentItem();
    if (item) {
        m_selectedModel = item->data(Qt::UserRole).toString();
    }
    accept();
}

QString MosModelPickerDialog::selectedModel() const {
    return m_selectedModel;
}
