#include "diode_model_picker_dialog.h"
#include "../items/schematic_item.h"
#include "../../simulator/bridge/model_library_manager.h"
#include "../../simulator/bridge/spice_model_search.h"
#include "theme_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QDir>

namespace {
// Map diode type name to .lib file suffix
QString libSuffix(const QString& type) {
    QString t = type.toLower();
    if (t == "silicon" || t == "diode") return "silicon";
    if (t == "schottky")    return "schottky";
    if (t == "zener")       return "zener";
    if (t == "led")         return "led";
    if (t == "varactor")    return "varactor";
    if (t == "rectifier")   return "rectifier";
    if (t == "tvs")         return "tvs";
    if (t == "fastrecovery" || t == "fast_recovery") return "fast_recovery";
    if (t == "switching")   return "switching";
    return t;
}

QString displayName(const QString& type) {
    QString t = type.toLower();
    if (t == "silicon")  return "Silicon";
    if (t == "schottky") return "Schottky";
    if (t == "zener")    return "Zener";
    if (t == "led")      return "LED";
    if (t == "varactor") return "Varactor";
    if (t == "rectifier") return "Rectifier";
    if (t == "tvs")      return "TVS";
    if (t == "fastrecovery" || t == "fast_recovery") return "Fast Recovery";
    if (t == "switching") return "Switching";
    return type;
}
}

DiodeModelPickerDialog::DiodeModelPickerDialog(SchematicItem* item, const QString& diodeType, QWidget* parent)
    : QDialog(parent), m_item(item), m_diodeType(diodeType) {
    setWindowTitle(QString("Pick %1 Model").arg(displayName(diodeType)));
    setModal(true);
    setMinimumSize(500, 400);

    auto* layout = new QVBoxLayout(this);

    // Search
    auto* searchLayout = new QHBoxLayout();
    searchLayout->addWidget(new QLabel("Search:"));
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("Filter models by name...");
    searchLayout->addWidget(m_searchEdit);
    layout->addLayout(searchLayout);

    // Model list
    m_modelList = new QListWidget();
    layout->addWidget(m_modelList);

    // Detail label
    m_detailLabel = new QLabel("Select a model to see parameters");
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

    // Buttons
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &DiodeModelPickerDialog::applySelected);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_searchEdit, &QLineEdit::textChanged, this, &DiodeModelPickerDialog::filterModels);
    connect(m_modelList, &QListWidget::itemDoubleClicked, this, &DiodeModelPickerDialog::onModelSelected);
    connect(m_modelList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current) {
        if (current) m_detailLabel->setText(current->data(Qt::UserRole + 1).toString());
    });

    if (ThemeManager::theme()) {
        setStyleSheet(ThemeManager::theme()->widgetStylesheet());
    }

    loadModels();
}

void DiodeModelPickerDialog::loadModels() {
    const QString suffix = libSuffix(m_diodeType);
    const auto allModels = ModelLibraryManager::instance().allModels();
    bool foundAny = false;

    for (const auto& mi : allModels) {
        if (mi.type != "Diode") continue;

        // Filter by library file name
        const QString libName = QFileInfo(mi.libraryPath).fileName().toLower();
        if (!libName.contains(suffix)) continue;

        foundAny = true;
        QString params = mi.params.join(", ");
        if (params.length() > 80) params = params.left(77) + "...";

        auto* item = new QListWidgetItem(mi.name);
        item->setData(Qt::UserRole, mi.name);
        item->setData(Qt::UserRole + 1, QString("%1\nType: %2\nParams: %3\nLibrary: %4")
            .arg(mi.name, displayName(m_diodeType), params, QFileInfo(mi.libraryPath).fileName()));
        m_modelList->addItem(item);
    }

    // If library-based filtering found nothing, fall back to all diode models
    if (!foundAny) {
        for (const auto& mi : allModels) {
            if (mi.type != "Diode") continue;
            auto* item = new QListWidgetItem(mi.name);
            item->setData(Qt::UserRole, mi.name);
            item->setData(Qt::UserRole + 1, QString("%1\nParams: %2")
                .arg(mi.name, mi.params.join(", ")));
            m_modelList->addItem(item);
        }
    }

    if (m_modelList->count() > 0) {
        m_modelList->setCurrentRow(0);
    }
}

void DiodeModelPickerDialog::filterModels(const QString& text) {
    const auto results = SpiceModelSearch::search(text, "Diode");

    for (int i = 0; i < m_modelList->count(); ++i) {
        m_modelList->item(i)->setHidden(true);
    }

    QSet<QString> matchingNames;
    for (const auto& scored : results) matchingNames.insert(scored.info.name);

    int showIndex = 0;
    for (const auto& scored : results) {
        for (int i = 0; i < m_modelList->count(); ++i) {
            auto* item = m_modelList->item(i);
            if (item->data(Qt::UserRole).toString() == scored.info.name) {
                item->setHidden(false);
                m_modelList->takeItem(i);
                m_modelList->insertItem(showIndex++, item);
                if (showIndex == 1) m_modelList->setCurrentItem(item);
                break;
            }
        }
    }

    auto* current = m_modelList->currentItem();
    if (current) {
        m_detailLabel->setText(current->data(Qt::UserRole + 1).toString());
    } else {
        m_detailLabel->setText(text.isEmpty() ? "Select a model to see details" : "No matching models");
    }
}

void DiodeModelPickerDialog::onModelSelected(QListWidgetItem* item) {
    if (item) {
        m_selectedModel = item->data(Qt::UserRole).toString();
        accept();
    }
}

void DiodeModelPickerDialog::applySelected() {
    auto* item = m_modelList->currentItem();
    if (item) {
        m_selectedModel = item->data(Qt::UserRole).toString();
    }
    accept();
}

QString DiodeModelPickerDialog::selectedModel() const {
    return m_selectedModel;
}
