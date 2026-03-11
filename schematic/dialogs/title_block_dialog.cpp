// title_block_dialog.cpp
// Professional dialog for editing all schematic title block metadata fields.

#include "title_block_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QFileDialog>
#include <QPixmap>
#include <QFrame>
#include <QDialogButtonBox>
#include <QScrollArea>
#include <QDate>

// ─────────────────────────────────────────────────────────────────────────────

TitleBlockDialog::TitleBlockDialog(const TitleBlockData& current,
                                   int totalSheets,
                                   QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Edit Title Block");
    setMinimumWidth(560);
    setModal(true);
    setupUi(current, totalSheets);
}

void TitleBlockDialog::setupUi(const TitleBlockData& d, int totalSheets) {
    auto* root = new QVBoxLayout(this);
    root->setSpacing(12);
    root->setContentsMargins(16, 16, 16, 12);

    // ── Style ──────────────────────────────────────────────────────────────
    setStyleSheet(R"(
        QDialog {
            background-color: #1a1b26;
            color: #c0caf5;
        }
        QGroupBox {
            background-color: #1e2030;
            border: 1px solid #2a2e42;
            border-radius: 6px;
            margin-top: 14px;
            color: #7aa2f7;
            font-weight: bold;
            font-size: 11px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 6px;
        }
        QLabel {
            color: #9aa5ce;
            font-size: 12px;
        }
        QLineEdit, QSpinBox {
            background-color: #24283b;
            border: 1px solid #3b4261;
            border-radius: 4px;
            color: #c0caf5;
            padding: 5px 8px;
            font-size: 12px;
            selection-background-color: #3d59a1;
        }
        QLineEdit:focus, QSpinBox:focus {
            border: 1px solid #7aa2f7;
        }
        QPushButton {
            background-color: #3b4261;
            border: 1px solid #4b527a;
            border-radius: 4px;
            color: #c0caf5;
            padding: 5px 14px;
            font-size: 12px;
        }
        QPushButton:hover {
            background-color: #414868;
            border-color: #7aa2f7;
        }
        QPushButton#acceptBtn {
            background-color: #3d59a1;
            border-color: #7aa2f7;
            color: #e0e5ff;
            font-weight: bold;
        }
        QPushButton#acceptBtn:hover {
            background-color: #4e6abf;
        }
    )");

    // ── Header ─────────────────────────────────────────────────────────────
    auto* hdr = new QLabel("📋  Title Block Editor", this);
    hdr->setStyleSheet("font-size: 16px; font-weight: bold; color: #7aa2f7; padding-bottom: 4px;");
    root->addWidget(hdr);

    auto* hdivider = new QFrame();
    hdivider->setFrameShape(QFrame::HLine);
    hdivider->setStyleSheet("background: #2a2e42;");
    root->addWidget(hdivider);

    // ── Scroll area for all fields ─────────────────────────────────────────
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("background: transparent;");

    auto* content = new QWidget();
    auto* vl = new QVBoxLayout(content);
    vl->setSpacing(10);

    // ── Group: Project Info ────────────────────────────────────────────────
    auto* grpProject = new QGroupBox("Project Information", content);
    auto* fl1 = new QFormLayout(grpProject);
    fl1->setSpacing(8);
    fl1->setContentsMargins(12, 18, 12, 12);

    m_projectName = new QLineEdit(d.projectName);
    m_projectName->setPlaceholderText("e.g. My Awesome PCB");
    fl1->addRow("Project Name:", m_projectName);

    m_sheetName = new QLineEdit(d.sheetName);
    m_sheetName->setPlaceholderText("e.g. Power Supply");
    fl1->addRow("Sheet Name:", m_sheetName);

    m_description = new QLineEdit(d.description);
    m_description->setPlaceholderText("Brief description of this sheet");
    fl1->addRow("Description:", m_description);

    vl->addWidget(grpProject);

    // ── Group: Organization ───────────────────────────────────────────────
    auto* grpOrg = new QGroupBox("Organization", content);
    auto* fl2 = new QFormLayout(grpOrg);
    fl2->setSpacing(8);
    fl2->setContentsMargins(12, 18, 12, 12);

    m_company = new QLineEdit(d.company);
    m_company->setPlaceholderText("e.g. Acme Electronics Ltd.");
    fl2->addRow("Company:", m_company);

    m_designer = new QLineEdit(d.designer);
    m_designer->setPlaceholderText("e.g. Jane Smith");
    fl2->addRow("Designed By:", m_designer);

    vl->addWidget(grpOrg);

    // ── Group: Revision & Sheet ────────────────────────────────────────────
    auto* grpRev = new QGroupBox("Revision & Sheet Numbering", content);
    auto* fl3 = new QFormLayout(grpRev);
    fl3->setSpacing(8);
    fl3->setContentsMargins(12, 18, 12, 12);

    m_revision = new QLineEdit(d.revision.isEmpty() ? "A" : d.revision);
    m_revision->setPlaceholderText("e.g. A, B, 1.0");
    m_revision->setMaximumWidth(120);
    fl3->addRow("Revision:", m_revision);

    m_date = new QLineEdit(d.date.isEmpty() ? QDate::currentDate().toString("yyyy-MM-dd") : d.date);
    m_date->setPlaceholderText("yyyy-MM-dd");
    m_date->setMaximumWidth(160);
    fl3->addRow("Date:", m_date);

    auto* sheetRow = new QHBoxLayout();
    m_sheetNumber = new QSpinBox();
    m_sheetNumber->setRange(1, 999);
    m_sheetNumber->setValue(d.sheetNumber.toInt() > 0 ? d.sheetNumber.toInt() : 1);
    m_sheetNumber->setMaximumWidth(80);

    m_sheetTotal = new QSpinBox();
    m_sheetTotal->setRange(1, 999);
    m_sheetTotal->setValue(d.sheetTotal.toInt() > 0 ? d.sheetTotal.toInt() : totalSheets);
    m_sheetTotal->setMaximumWidth(80);

    sheetRow->addWidget(m_sheetNumber);
    sheetRow->addWidget(new QLabel("  of  "));
    sheetRow->addWidget(m_sheetTotal);
    sheetRow->addStretch();
    fl3->addRow("Sheet Number:", sheetRow);

    vl->addWidget(grpRev);

    // ── Group: Logo ────────────────────────────────────────────────────────
    auto* grpLogo = new QGroupBox("Company Logo", content);
    auto* logoVL = new QVBoxLayout(grpLogo);
    logoVL->setSpacing(8);
    logoVL->setContentsMargins(12, 18, 12, 12);

    m_logoPreview = new QLabel("No logo selected");
    m_logoPreview->setFixedHeight(70);
    m_logoPreview->setAlignment(Qt::AlignCenter);
    m_logoPreview->setStyleSheet("border: 1px dashed #3b4261; border-radius: 4px; color: #565f89;");
    logoVL->addWidget(m_logoPreview);

    auto* logoRow = new QHBoxLayout();
    m_logoPath = new QLineEdit(d.logoPath);
    m_logoPath->setPlaceholderText("Path to PNG/SVG logo (optional)");
    m_logoPath->setReadOnly(true);
    logoRow->addWidget(m_logoPath, 1);

    auto* browseBtn = new QPushButton("Browse…");
    connect(browseBtn, &QPushButton::clicked, this, &TitleBlockDialog::browseLogo);
    logoRow->addWidget(browseBtn);

    auto* clearBtn = new QPushButton("Clear");
    connect(clearBtn, &QPushButton::clicked, this, &TitleBlockDialog::clearLogo);
    logoRow->addWidget(clearBtn);

    logoVL->addLayout(logoRow);
    vl->addWidget(grpLogo);

    // Load current logo preview
    if (!d.logoPath.isEmpty()) {
        QPixmap px(d.logoPath);
        if (!px.isNull())
            m_logoPreview->setPixmap(px.scaledToHeight(66, Qt::SmoothTransformation));
    }

    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    // ── Buttons ────────────────────────────────────────────────────────────
    auto* divider = new QFrame();
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet("background: #2a2e42;");
    root->addWidget(divider);

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();

    auto* cancelBtn = new QPushButton("Cancel");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(cancelBtn);

    auto* okBtn = new QPushButton("Apply");
    okBtn->setObjectName("acceptBtn");
    okBtn->setDefault(true);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(okBtn);

    root->addLayout(btnRow);
}

void TitleBlockDialog::browseLogo() {
    QString path = QFileDialog::getOpenFileName(
        this, "Select Logo Image", QString(),
        "Images (*.png *.jpg *.jpeg *.svg *.bmp)");
    if (path.isEmpty()) return;
    m_logoPath->setText(path);
    QPixmap px(path);
    if (!px.isNull())
        m_logoPreview->setPixmap(px.scaledToHeight(66, Qt::SmoothTransformation));
}

void TitleBlockDialog::clearLogo() {
    m_logoPath->clear();
    m_logoPreview->setPixmap(QPixmap());
    m_logoPreview->setText("No logo selected");
}

TitleBlockData TitleBlockDialog::result() const {
    TitleBlockData d;
    d.projectName = m_projectName->text().trimmed();
    d.sheetName   = m_sheetName->text().trimmed();
    d.description = m_description->text().trimmed();
    d.company     = m_company->text().trimmed();
    d.designer    = m_designer->text().trimmed();
    d.revision    = m_revision->text().trimmed();
    d.date        = m_date->text().trimmed();
    d.sheetNumber = QString::number(m_sheetNumber->value());
    d.sheetTotal  = QString::number(m_sheetTotal->value());
    d.logoPath    = m_logoPath->text();
    return d;
}
