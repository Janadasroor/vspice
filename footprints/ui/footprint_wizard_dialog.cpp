#include "footprint_wizard_dialog.h"
#include "../footprint_editor.h"
#include "../footprint_library.h"
#include "items/footprint_primitive_item.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QGraphicsItem>
#include <algorithm>

using namespace Flux::Model;
using namespace Flux::Item;

FootprintWizardDialog::FootprintWizardDialog(FootprintEditor* editor, QWidget* parent)
    : QDialog(parent), m_editor(editor)
{
    setWindowTitle("Footprint Wizard");
    resize(850, 600);
    setupUI();
    populatePackageList();
    updatePreview();
}

FootprintWizardDialog::~FootprintWizardDialog() = default;

void FootprintWizardDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);

    // === Main content: Left panel (packages + params), Right panel (preview) ===
    QHBoxLayout* contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(15);

    // --- Left panel ---
    QWidget* leftPanel = new QWidget();
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    // Package list
    QGroupBox* pkgGroup = new QGroupBox("Standard Packages");
    QVBoxLayout* pkgLayout = new QVBoxLayout(pkgGroup);
    m_packageList = new QListWidget();
    m_packageList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_packageList->setMaximumHeight(200);
    pkgLayout->addWidget(m_packageList);
    leftLayout->addWidget(pkgGroup);

    // Parameters form
    QGroupBox* paramGroup = new QGroupBox("Parameters");
    QFormLayout* formLayout = new QFormLayout(paramGroup);
    formLayout->setSpacing(6);

    m_nameEdit = new QLineEdit();
    m_nameEdit->setPlaceholderText("Auto-generated from package");
    formLayout->addRow("Name:", m_nameEdit);

    m_pinCountSpin = new QSpinBox();
    m_pinCountSpin->setRange(1, 500);
    m_pinCountSpin->setValue(8);
    formLayout->addRow("Pin Count:", m_pinCountSpin);

    m_pitchSpin = new QDoubleSpinBox();
    m_pitchSpin->setRange(0.1, 10.0);
    m_pitchSpin->setSingleStep(0.05);
    m_pitchSpin->setSuffix(" mm");
    m_pitchSpin->setValue(2.54);
    formLayout->addRow("Pitch:", m_pitchSpin);

    m_padWidthSpin = new QDoubleSpinBox();
    m_padWidthSpin->setRange(0.1, 5.0);
    m_padWidthSpin->setSingleStep(0.05);
    m_padWidthSpin->setSuffix(" mm");
    m_padWidthSpin->setValue(1.0);
    formLayout->addRow("Pad Width:", m_padWidthSpin);

    m_padHeightSpin = new QDoubleSpinBox();
    m_padHeightSpin->setRange(0.1, 5.0);
    m_padHeightSpin->setSingleStep(0.05);
    m_padHeightSpin->setSuffix(" mm");
    m_padHeightSpin->setValue(1.0);
    formLayout->addRow("Pad Height:", m_padHeightSpin);

    m_bodyWidthSpin = new QDoubleSpinBox();
    m_bodyWidthSpin->setRange(0.5, 50.0);
    m_bodyWidthSpin->setSingleStep(0.1);
    m_bodyWidthSpin->setSuffix(" mm");
    m_bodyWidthSpin->setValue(6.35);
    formLayout->addRow("Body Width:", m_bodyWidthSpin);

    m_bodyHeightSpin = new QDoubleSpinBox();
    m_bodyHeightSpin->setRange(0.5, 50.0);
    m_bodyHeightSpin->setSingleStep(0.1);
    m_bodyHeightSpin->setSuffix(" mm");
    m_bodyHeightSpin->setValue(6.35);
    formLayout->addRow("Body Height:", m_bodyHeightSpin);

    m_rowSpanSpin = new QDoubleSpinBox();
    m_rowSpanSpin->setRange(1.0, 50.0);
    m_rowSpanSpin->setSingleStep(0.1);
    m_rowSpanSpin->setSuffix(" mm");
    m_rowSpanSpin->setValue(7.62);
    formLayout->addRow("Row Span:", m_rowSpanSpin);

    m_drillSizeSpin = new QDoubleSpinBox();
    m_drillSizeSpin->setRange(0.1, 3.0);
    m_drillSizeSpin->setSingleStep(0.05);
    m_drillSizeSpin->setSuffix(" mm");
    m_drillSizeSpin->setValue(0.8);
    formLayout->addRow("Drill Size:", m_drillSizeSpin);

    m_courtyardExtraSpin = new QDoubleSpinBox();
    m_courtyardExtraSpin->setRange(0.0, 5.0);
    m_courtyardExtraSpin->setSingleStep(0.05);
    m_courtyardExtraSpin->setSuffix(" mm");
    m_courtyardExtraSpin->setValue(0.5);
    formLayout->addRow("Courtyard Extra:", m_courtyardExtraSpin);

    leftLayout->addWidget(paramGroup);

    // Connect parameter changes
    auto connectParam = [this]() { onParamChanged(); };
    connect(m_pinCountSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, connectParam);
    connect(m_pitchSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, connectParam);
    connect(m_padWidthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, connectParam);
    connect(m_padHeightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, connectParam);
    connect(m_bodyWidthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, connectParam);
    connect(m_bodyHeightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, connectParam);
    connect(m_rowSpanSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, connectParam);
    connect(m_drillSizeSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, connectParam);
    connect(m_courtyardExtraSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, connectParam);
    connect(m_nameEdit, &QLineEdit::textEdited, this, connectParam);

    contentLayout->addWidget(leftPanel, 1);

    // --- Right panel: Preview ---
    QGroupBox* previewGroup = new QGroupBox("Preview");
    QVBoxLayout* previewLayout = new QVBoxLayout(previewGroup);

    m_previewScene = new QGraphicsScene(this);
    m_previewView = new QGraphicsView(m_previewScene);
    m_previewView->setRenderHint(QPainter::Antialiasing);
    m_previewView->setMinimumSize(350, 350);
    m_previewView->scale(15, -15); // Flip Y for PCB coords
    previewLayout->addWidget(m_previewView);

    m_summaryLabel = new QLabel();
    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->setStyleSheet("padding: 8px; background: palette(alternate-base); border-radius: 4px;");
    previewLayout->addWidget(m_summaryLabel);

    contentLayout->addWidget(previewGroup, 1);

    mainLayout->addLayout(contentLayout);

    // === Buttons ===
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    m_generateBtn = new QPushButton("🔧 Generate");
    m_generateBtn->setStyleSheet("background-color: #17a2b8; color: white; font-weight: bold; padding: 8px;");
    m_saveBtn = new QPushButton("💾 Save to Library");
    m_saveBtn->setStyleSheet("background-color: #28a745; color: white; font-weight: bold; padding: 8px;");
    m_closeBtn = new QPushButton("Close");

    btnLayout->addWidget(m_generateBtn);
    btnLayout->addWidget(m_saveBtn);
    btnLayout->addWidget(m_closeBtn);
    mainLayout->addLayout(btnLayout);

    // Connect buttons
    connect(m_packageList, &QListWidget::currentRowChanged, this, &FootprintWizardDialog::onPackageSelected);
    connect(m_generateBtn, &QPushButton::clicked, this, &FootprintWizardDialog::onGenerate);
    connect(m_saveBtn, &QPushButton::clicked, this, &FootprintWizardDialog::onSaveToLibrary);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void FootprintWizardDialog::populatePackageList() {
    QStringList packages = FootprintWizard::getSupportedPackages();

    // Group by category
    QMap<QString, QStringList> grouped;
    for (const QString& pkg : packages) {
        QString cat = FootprintWizard::getCategory(pkg);
        grouped[cat].append(pkg);
    }

    for (auto it = grouped.begin(); it != grouped.end(); ++it) {
        // Category header (non-selectable)
        auto* catItem = new QListWidgetItem(QString("  ── %1 ──").arg(it.key()));
        catItem->setFlags(Qt::NoItemFlags);
        m_packageList->addItem(catItem);

        for (const QString& pkg : it.value()) {
            auto* pkgItem = new QListWidgetItem(pkg);
            pkgItem->setData(Qt::UserRole, pkg);
            m_packageList->addItem(pkgItem);
        }
    }

    // Select first real package
    for (int i = 0; i < m_packageList->count(); ++i) {
        auto* item = m_packageList->item(i);
        if (item->flags() & Qt::ItemIsSelectable) {
            m_packageList->setCurrentRow(i);
            break;
        }
    }
}

void FootprintWizardDialog::onPackageSelected(int index) {
    Q_UNUSED(index);

    auto* currentItem = m_packageList->currentItem();
    if (!currentItem || !(currentItem->flags() & Qt::ItemIsSelectable)) return;

    QString pkg = currentItem->data(Qt::UserRole).toString();
    if (pkg.isEmpty()) return;

    // Load predefined params
    FootprintWizard::WizardParams p = FootprintWizard::getPredefined(pkg);

    // Update form fields
    m_nameEdit->setText(pkg.toLower().replace(" ", "_"));
    m_pinCountSpin->setValue(p.pinCount);
    m_pitchSpin->setValue(p.pitch);
    m_padWidthSpin->setValue(p.padWidth);
    m_padHeightSpin->setValue(p.padHeight);
    m_bodyWidthSpin->setValue(p.bodyWidth);
    m_bodyHeightSpin->setValue(p.bodyHeight);
    m_rowSpanSpin->setValue(p.rowSpan);
    m_drillSizeSpin->setValue(p.drillSize);
    m_courtyardExtraSpin->setValue(p.courtyardExtra);

    updatePreview();
}

void FootprintWizardDialog::onParamChanged() {
    updatePreview();
}

void FootprintWizardDialog::updatePreview() {
    // Clear previous preview
    for (auto* item : m_previewItems) {
        m_previewScene->removeItem(item);
        delete item;
    }
    m_previewItems.clear();

    FootprintWizard::WizardParams params = collectParams();
    FootprintDefinition def = FootprintWizard::generate(params);

    // Render primitives to preview scene
    for (const auto& prim : def.primitives()) {
        QGraphicsItem* visual = nullptr;

        switch (prim.type) {
            case FootprintPrimitive::Pad: {
                visual = new FootprintPadItem(prim);
                break;
            }
            case FootprintPrimitive::Line: {
                visual = new FootprintLineItem(prim);
                break;
            }
            case FootprintPrimitive::Rect: {
                visual = new FootprintRectItem(prim);
                break;
            }
            case FootprintPrimitive::Circle: {
                visual = new FootprintCircleItem(prim);
                break;
            }
            case FootprintPrimitive::Arc: {
                visual = new FootprintArcItem(prim);
                break;
            }
            case FootprintPrimitive::Text: {
                visual = new FootprintTextItem(prim);
                break;
            }
            default: break;
        }

        if (visual) {
            m_previewScene->addItem(visual);
            m_previewItems.append(visual);
        }
    }

    // Fit view
    m_previewView->fitInView(m_previewScene->itemsBoundingRect().adjusted(-1, -1, 1, 1), Qt::KeepAspectRatio);

    // Update summary
    int padCount = 0;
    int primCount = def.primitives().size();
    for (const auto& prim : def.primitives()) {
        if (prim.type == FootprintPrimitive::Pad) padCount++;
    }

    m_summaryLabel->setText(
        QString("<b>%1</b><br>"
                "Pads: %2 | Primitives: %3<br>"
                "Category: %4 | Classification: %5")
            .arg(def.name(), QString::number(padCount), QString::number(primCount),
                 def.category(), def.classification())
    );
}

void FootprintWizardDialog::onGenerate() {
    FootprintWizard::WizardParams params = collectParams();
    FootprintDefinition def = FootprintWizard::generate(params);

    if (m_editor) {
        m_editor->setFootprintDefinition(def);
    }

    QMessageBox::information(this, "Generated",
        QString("Footprint '%1' generated with %2 pads and %3 primitives.\n\n"
                "Click 'Save to Library' to save it.")
            .arg(def.name())
            .arg(QString::number(std::count_if(def.primitives().cbegin(), def.primitives().cend(),
                [](const FootprintPrimitive& p) { return p.type == FootprintPrimitive::Pad; })))
            .arg(def.primitives().size()));
}

void FootprintWizardDialog::onSaveToLibrary() {
    if (!m_editor) {
        QMessageBox::warning(this, "No Editor", "No footprint editor instance available.");
        return;
    }

    // Generate and load into editor first
    FootprintWizard::WizardParams params = collectParams();
    FootprintDefinition def = FootprintWizard::generate(params);
    m_editor->setFootprintDefinition(def);

    // Now save via editor's save mechanism
    QString fpName = def.name();
    if (fpName.isEmpty()) {
        QMessageBox::warning(this, "No Name", "Please specify a footprint name.");
        return;
    }

    // Use the library manager to save
    auto& libMgr = FootprintLibraryManager::instance();
    QString category = def.category().isEmpty() ? "Wizard" : def.category();

    // Find or create library for this category
    FootprintLibrary* lib = nullptr;
    for (auto* l : libMgr.libraries()) {
        if (l->name() == category) {
            lib = l;
            break;
        }
    }
    if (!lib) {
        lib = libMgr.createLibrary(category);
    }

    if (lib && lib->saveFootprint(def)) {
        QMessageBox::information(this, "Saved",
            QString("Footprint '%1' saved to library '%2'.").arg(fpName, category));
    } else {
        QMessageBox::warning(this, "Save Failed",
            QString("Could not save footprint '%1'.").arg(fpName));
    }
}

FootprintWizard::WizardParams FootprintWizardDialog::collectParams() {
    FootprintWizard::WizardParams p;

    auto* currentItem = m_packageList->currentItem();
    if (currentItem && (currentItem->flags() & Qt::ItemIsSelectable)) {
        p.packageType = currentItem->data(Qt::UserRole).toString();
    } else {
        p.packageType = "Custom";
    }

    p.pinCount = m_pinCountSpin->value();
    p.pitch = m_pitchSpin->value();
    p.padWidth = m_padWidthSpin->value();
    p.padHeight = m_padHeightSpin->value();
    p.bodyWidth = m_bodyWidthSpin->value();
    p.bodyHeight = m_bodyHeightSpin->value();
    p.rowSpan = m_rowSpanSpin->value();
    p.drillSize = m_drillSizeSpin->value();
    p.courtyardExtra = m_courtyardExtraSpin->value();
    p.classification = FootprintWizard::getCategory(p.packageType) == "Through-Hole" ? "Through-Hole" : "SMD";
    p.category = FootprintWizard::getCategory(p.packageType);

    return p;
}
