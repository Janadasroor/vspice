#include "length_matching_dialog.h"
#include "../analysis/length_measurement_engine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QHeaderView>
#include <QListWidget>
#include <QMessageBox>
#include <QInputDialog>
#include <QDialogButtonBox>
#include <QSet>

LengthMatchingDialog::LengthMatchingDialog(QGraphicsScene* scene, QWidget* parent)
    : QDialog(parent), m_scene(scene)
{
    setWindowTitle("High-Speed Length Matching");
    resize(1000, 650);
    setupUI();
    refreshAll();

    // Connect manager signals
    connect(&LengthMatchManager::instance(), &LengthMatchManager::groupsChanged,
            this, &LengthMatchingDialog::updateGroupList);
    connect(&LengthMatchManager::instance(), &LengthMatchManager::measurementUpdated,
            this, [this](const QString&) { updateNetTable(); updateDiffPairTable(); updateStatus(); });
}

LengthMatchingDialog::~LengthMatchingDialog() = default;

void LengthMatchingDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // === Main content area (3 panels) ===
    QHBoxLayout* contentLayout = new QHBoxLayout();

    // --- Left: Group List ---
    QGroupBox* groupGroup = new QGroupBox("Length Match Groups");
    QVBoxLayout* groupLayout = new QVBoxLayout(groupGroup);
    m_groupList = new QListWidget();
    m_groupList->setSelectionMode(QAbstractItemView::SingleSelection);
    groupLayout->addWidget(m_groupList);

    QHBoxLayout* groupBtns = new QHBoxLayout();
    m_createGroupBtn = new QPushButton("+ New Group");
    m_deleteGroupBtn = new QPushButton("Delete");
    group_btns->addWidget(m_createGroupBtn);
    group_btns->addWidget(m_deleteGroupBtn);
    groupLayout->addLayout(group_btns);

    connect(m_createGroupBtn, &QPushButton::clicked, this, &LengthMatchingDialog::onCreateGroup);
    connect(m_deleteGroupBtn, &QPushButton::clicked, this, &LengthMatchingDialog::onDeleteGroup);
    connect(m_groupList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current, QListWidgetItem*) {
        if (current) onGroupSelected(current->data(Qt::UserRole).toString());
    });

    contentLayout->addWidget(groupGroup, 1);

    // --- Center: Net Table ---
    QGroupBox* netGroup = new QGroupBox("Net Lengths");
    QVBoxLayout* netLayout = new QVBoxLayout(netGroup);
    m_netTable = new QTableWidget();
    m_netTable->setColumnCount(7);
    m_netTable->setHorizontalHeaderLabels({"Net", "Length (mm)", "Via Len", "Vias", "Δ Target", "Status", "Delay (ps)"});
    m_netTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_netTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_netTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    m_netTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_netTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    netLayout->addWidget(m_netTable);

    QHBoxLayout* netBtns = new QHBoxLayout();
    m_addNetBtn = new QPushButton("+ Add Net");
    m_removeNetBtn = new QPushButton("Remove");
    netBtns->addWidget(m_addNetBtn);
    netBtns->addWidget(m_removeNetBtn);
    netBtns->addStretch();
    netLayout->addLayout(netBtns);

    connect(m_addNetBtn, &QPushButton::clicked, this, &LengthMatchingDialog::onAddNet);
    connect(m_removeNetBtn, &QPushButton::clicked, this, &LengthMatchingDialog::onRemoveNet);

    contentLayout->addWidget(netGroup, 2);

    // --- Right: Diff Pair Table ---
    QGroupBox* dpGroup = new QGroupBox("Differential Pair Skew");
    QVBoxLayout* dpLayout = new QVBoxLayout(dpGroup);
    m_diffPairTable = new QTableWidget();
    m_diffPairTable->setColumnCount(5);
    m_diffPairTable->setHorizontalHeaderLabels({"Pair", "P Net", "N Net", "P Length", "Skew"});
    m_diffPairTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_diffPairTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_diffPairTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    dpLayout->addWidget(m_diffPairTable);
    contentLayout->addWidget(dpGroup, 2);

    mainLayout->addLayout(contentLayout);

    // === Settings Panel ===
    QGroupBox* settingsGroup = new QGroupBox("Group Settings");
    QGridLayout* settingsLayout = new QGridLayout(settingsGroup);

    settingsLayout->addWidget(new QLabel("Tolerance (±mm):"), 0, 0);
    m_toleranceSpin = new QDoubleSpinBox();
    m_toleranceSpin->setRange(0.01, 50.0);
    m_toleranceSpin->setSingleStep(0.1);
    m_toleranceSpin->setValue(2.0);
    settingsLayout->addWidget(m_toleranceSpin, 0, 1);
    connect(m_toleranceSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &LengthMatchingDialog::onToleranceChanged);

    settingsLayout->addWidget(new QLabel("Intra-Pair Skew (mm):"), 0, 2);
    m_intraPairToleranceSpin = new QDoubleSpinBox();
    m_intraPairToleranceSpin->setRange(0.01, 5.0);
    m_intraPairToleranceSpin->setSingleStep(0.01);
    m_intraPairToleranceSpin->setValue(0.1);
    settingsLayout->addWidget(m_intraPairToleranceSpin, 0, 3);
    connect(m_intraPairToleranceSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &LengthMatchingDialog::onIntraPairToleranceChanged);

    settingsLayout->addWidget(new QLabel("Target Length:"), 1, 0);
    m_targetLengthSpin = new QDoubleSpinBox();
    m_targetLengthSpin->setRange(0.0, 9999.0);
    m_targetLengthSpin->setSingleStep(1.0);
    m_targetLengthSpin->setSuffix(" mm");
    m_targetLengthSpin->setEnabled(false);
    settingsLayout->addWidget(m_targetLengthSpin, 1, 1);

    m_autoTargetCheck = new QCheckBox("Auto (longest net)");
    m_autoTargetCheck->setChecked(true);
    settingsLayout->addWidget(m_autoTargetCheck, 1, 2, 1, 2);
    connect(m_autoTargetCheck, &QCheckBox::toggled, this, &LengthMatchingDialog::onAutoTargetToggled);

    settingsLayout->addWidget(new QLabel("Serpentine Amplitude:"), 2, 0);
    m_serpAmplitudeSpin = new QDoubleSpinBox();
    m_serpAmplitudeSpin->setRange(0.1, 10.0);
    m_serpAmplitudeSpin->setSingleStep(0.1);
    m_serpAmplitudeSpin->setValue(1.5);
    m_serpAmplitudeSpin->setSuffix(" mm");
    settingsLayout->addWidget(m_serpAmplitudeSpin, 2, 1);

    settingsLayout->addWidget(new QLabel("Serpentine Spacing:"), 2, 2);
    m_serpSpacingSpin = new QDoubleSpinBox();
    m_serpSpacingSpin->setRange(0.05, 5.0);
    m_serpSpacingSpin->setSingleStep(0.05);
    m_serpSpacingSpin->setValue(0.3);
    m_serpSpacingSpin->setSuffix(" mm");
    settingsLayout->addWidget(m_serpSpacingSpin, 2, 3);

    mainLayout->addWidget(settingsGroup);

    // === Status and Action Buttons ===
    m_statusLabel = new QLabel("Ready");
    m_statusLabel->setStyleSheet("font-weight: bold; padding: 6px;");
    mainLayout->addWidget(m_statusLabel);

    QHBoxLayout* actionLayout = new QHBoxLayout();
    actionLayout->addStretch();

    m_measureBtn = new QPushButton("📏 Measure All");
    m_autoTuneBtn = new QPushButton("🐍 Auto-Tune Group");
    m_closeBtn = new QPushButton("Close");

    m_measureBtn->setStyleSheet("background-color: #17a2b8; color: white; font-weight: bold; padding: 8px;");
    m_autoTuneBtn->setStyleSheet("background-color: #28a745; color: white; font-weight: bold; padding: 8px;");

    actionLayout->addWidget(m_measureBtn);
    actionLayout->addWidget(m_autoTuneBtn);
    actionLayout->addWidget(m_closeBtn);
    mainLayout->addLayout(actionLayout);

    connect(m_measureBtn, &QPushButton::clicked, this, &LengthMatchingDialog::onMeasure);
    connect(m_autoTuneBtn, &QPushButton::clicked, this, &LengthMatchingDialog::onAutoTune);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

// ============================================================================
// Update functions
// ============================================================================

void LengthMatchingDialog::updateGroupList() {
    m_groupList->clear();
    const auto& groups = LengthMatchManager::instance().groups();

    for (const auto& group : groups) {
        auto* item = new QListWidgetItem(group.name);
        item->setData(Qt::UserRole, group.id);

        // Color code by status
        if (group.allWithinTolerance && !group.netNames.isEmpty()) {
            item->setForeground(Qt::green);
            item->setText(group.name + " ✅");
        } else if (!group.netNames.isEmpty()) {
            item->setForeground(Qt::yellow);
            item->setText(group.name + " ⚠️");
        } else {
            item->setText(group.name);
        }

        m_groupList->addItem(item);
    }
}

void LengthMatchingDialog::updateNetTable() {
    auto* group = LengthMatchManager::instance().getGroup(m_currentGroupId);
    if (!group) {
        m_netTable->setRowCount(0);
        return;
    }

    m_netTable->setRowCount(group->entries.size());

    for (int row = 0; row < group->entries.size(); ++row) {
        const auto& entry = group->entries[row];

        m_netTable->setItem(row, 0, new QTableWidgetItem(entry.netName));

        auto* lenItem = new QTableWidgetItem(QString::number(entry.length, 'f', 2));
        lenItem->setTextAlignment(Qt::AlignCenter);
        m_netTable->setItem(row, 1, lenItem);

        auto* viaLenItem = new QTableWidgetItem(QString::number(entry.viaLength, 'f', 2));
        viaLenItem->setTextAlignment(Qt::AlignCenter);
        m_netTable->setItem(row, 2, viaLenItem);

        auto* viaCountItem = new QTableWidgetItem(QString::number(entry.viaCount));
        viaCountItem->setTextAlignment(Qt::AlignCenter);
        m_netTable->setItem(row, 3, viaCountItem);

        QString deltaStr = QString("%1%2 mm").arg(entry.deltaFromTarget >= 0 ? "+" : "")
                                          .arg(entry.deltaFromTarget, 0, 'f', 2);
        auto* deltaItem = new QTableWidgetItem(deltaStr);
        deltaItem->setTextAlignment(Qt::AlignCenter);
        deltaItem->setForeground(entry.withinTolerance ? Qt::green : Qt::red);
        m_netTable->setItem(row, 4, deltaItem);

        auto* statusItem = new QTableWidgetItem(entry.withinTolerance ? "✅ OK" : "❌ Fail");
        statusItem->setTextAlignment(Qt::AlignCenter);
        statusItem->setForeground(entry.withinTolerance ? Qt::green : Qt::red);
        m_netTable->setItem(row, 5, statusItem);

        auto* delayItem = new QTableWidgetItem(QString::number(entry.delayPs, 'f', 1));
        delayItem->setTextAlignment(Qt::AlignCenter);
        m_netTable->setItem(row, 6, delayItem);
    }
}

void LengthMatchingDialog::updateDiffPairTable() {
    auto* group = LengthMatchManager::instance().getGroup(m_currentGroupId);
    if (!group) {
        m_diffPairTable->setRowCount(0);
        return;
    }

    m_diffPairTable->setRowCount(group->diffPairInfo.size());

    for (int row = 0; row < group->diffPairInfo.size(); ++row) {
        const auto& dp = group->diffPairInfo[row];

        m_diffPairTable->setItem(row, 0, new QTableWidgetItem(dp.pairName));
        m_diffPairTable->setItem(row, 1, new QTableWidgetItem(dp.pNet));
        m_diffPairTable->setItem(row, 2, new QTableWidgetItem(dp.nNet));

        auto* pLenItem = new QTableWidgetItem(QString::number(dp.pLength, 'f', 2));
        pLenItem->setTextAlignment(Qt::AlignCenter);
        m_diffPairTable->setItem(row, 3, pLenItem);

        QString skewStr = QString("%1 mm").arg(dp.intraPairSkew, 0, 'f', 3);
        auto* skewItem = new QTableWidgetItem(skewStr);
        skewItem->setTextAlignment(Qt::AlignCenter);
        skewItem->setForeground(dp.withinTolerance ? Qt::green : Qt::red);
        m_diffPairTable->setItem(row, 4, skewItem);
    }
}

void LengthMatchingDialog::updateStatus() {
    auto* group = LengthMatchManager::instance().getGroup(m_currentGroupId);
    if (!group || group->netNames.isEmpty()) {
        m_statusLabel->setText("No group selected");
        m_statusLabel->setStyleSheet("font-weight: bold; padding: 6px; color: palette(text);");
        return;
    }

    int passCount = 0;
    for (const auto& entry : group->entries) {
        if (entry.withinTolerance) passCount++;
    }

    if (group->allWithinTolerance) {
        m_statusLabel->setText(QString("✅ All %1 nets within ±%2 mm tolerance")
            .arg(group->entries.size()).arg(group->tolerance));
        m_statusLabel->setStyleSheet("font-weight: bold; padding: 6px; color: #28a745;");
    } else {
        m_statusLabel->setText(QString("⚠️ %1/%2 nets within tolerance (%3 need tuning)")
            .arg(passCount).arg(group->entries.size()).arg(group->entries.size() - passCount));
        m_statusLabel->setStyleSheet("font-weight: bold; padding: 6px; color: #ffc107;");
    }
}

void LengthMatchingDialog::refreshAll() {
    updateGroupList();
    if (!m_currentGroupId.isEmpty()) {
        updateNetTable();
        updateDiffPairTable();
        updateStatus();
    }
}

// ============================================================================
// Slots
// ============================================================================

void LengthMatchingDialog::onCreateGroup() {
    bool ok;
    QString name = QInputDialog::getText(this, "New Length Match Group",
                                          "Group name:", QLineEdit::Normal,
                                          "HighSpeed_Bus", &ok);
    if (ok && !name.isEmpty()) {
        QString id = LengthMatchManager::instance().createGroup(name);
        m_groupList->setCurrentRow(m_groupList->count() - 1);
        onGroupSelected(id);
    }
}

void LengthMatchingDialog::onDeleteGroup() {
    if (m_currentGroupId.isEmpty()) return;

    auto reply = QMessageBox::question(this, "Delete Group",
                                        "Delete this length match group?",
                                        QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        LengthMatchManager::instance().deleteGroup(m_currentGroupId);
        m_currentGroupId.clear();
        updateGroupList();
        updateNetTable();
        updateDiffPairTable();
        updateStatus();
    }
}

void LengthMatchingDialog::onAddNet() {
    if (m_currentGroupId.isEmpty()) {
        QMessageBox::information(this, "No Group", "Create or select a group first.");
        return;
    }

    // Get all net names from scene
    QStringList allNets = LengthMeasurementEngine::getNetNames(m_scene);
    auto* group = LengthMatchManager::instance().getGroup(m_currentGroupId);
    if (group) {
        allNets = allNets.toSet().subtract(group->netNames).values();
    }

    bool ok;
    QString net = QInputDialog::getItem(this, "Add Net", "Select net to add:",
                                         allNets, 0, false, &ok);
    if (ok && !net.isEmpty()) {
        LengthMatchManager::instance().addNetToGroup(m_currentGroupId, net);
        LengthMatchManager::instance().measureAll(m_scene);
        updateNetTable();
        updateDiffPairTable();
    }
}

void LengthMatchingDialog::onRemoveNet() {
    if (m_currentGroupId.isEmpty()) return;

    int row = m_netTable->currentRow();
    if (row < 0) return;

    QTableWidgetItem* item = m_netTable->item(row, 0);
    if (!item) return;

    QString netName = item->text();
    LengthMatchManager::instance().removeNetFromGroup(m_currentGroupId, netName);
    LengthMatchManager::instance().measureAll(m_scene);
    updateNetTable();
    updateDiffPairTable();
}

void LengthMatchingDialog::onMeasure() {
    LengthMatchManager::instance().measureAll(m_scene);
    updateNetTable();
    updateDiffPairTable();
    updateStatus();
    m_statusLabel->setText("Measurement complete");
}

void LengthMatchingDialog::onAutoTune() {
    if (m_currentGroupId.isEmpty()) {
        QMessageBox::information(this, "No Group", "Select a group to tune.");
        return;
    }

    auto* group = LengthMatchManager::instance().getGroup(m_currentGroupId);
    if (!group || !group->enableSerpentine) {
        QMessageBox::information(this, "Serpentine Disabled", "Enable serpentine for this group first.");
        return;
    }

    // Count nets needing tuning
    int needTuning = 0;
    for (const auto& entry : group->entries) {
        if (!entry.withinTolerance && entry.deltaFromTarget < 0) needTuning++;
    }

    if (needTuning == 0) {
        QMessageBox::information(this, "No Tuning Needed", "All nets are within tolerance.");
        return;
    }

    auto reply = QMessageBox::question(this, "Auto-Tune",
        QString("Generate serpentine patterns for %1 nets?\n\n"
                "Amplitude: %2 mm\nSpacing: %3 mm")
            .arg(needTuning)
            .arg(group->serpentineAmplitude)
            .arg(group->serpentineSpacing),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        int tuned = LengthMatchManager::instance().autoTuneGroup(m_currentGroupId, m_scene);
        LengthMatchManager::instance().measureAll(m_scene);
        updateNetTable();
        updateDiffPairTable();
        updateStatus();

        QMessageBox::information(this, "Auto-Tune Complete",
            QString("Successfully tuned %1 nets.").arg(tuned));
    }
}

void LengthMatchingDialog::onGroupSelected(const QString& groupId) {
    m_currentGroupId = groupId;
    auto* group = LengthMatchManager::instance().getGroup(groupId);
    if (!group) return;

    // Update settings widgets
    m_toleranceSpin->blockSignals(true);
    m_toleranceSpin->setValue(group->tolerance);
    m_toleranceSpin->blockSignals(false);

    m_intraPairToleranceSpin->blockSignals(true);
    m_intraPairToleranceSpin->setValue(group->intraPairTolerance);
    m_intraPairToleranceSpin->blockSignals(false);

    m_autoTargetCheck->blockSignals(true);
    m_autoTargetCheck->setChecked(group->autoComputeTarget);
    m_autoTargetCheck->blockSignals(false);
    m_targetLengthSpin->setEnabled(!group->autoComputeTarget);
    m_targetLengthSpin->blockSignals(true);
    m_targetLengthSpin->setValue(group->targetLength);
    m_targetLengthSpin->blockSignals(false);

    m_serpAmplitudeSpin->setValue(group->serpentineAmplitude);
    m_serpSpacingSpin->setValue(group->serpentineSpacing);

    updateNetTable();
    updateDiffPairTable();
    updateStatus();
}

void LengthMatchingDialog::onToleranceChanged(double value) {
    if (m_currentGroupId.isEmpty()) return;
    LengthMatchManager::instance().setGroupTolerance(m_currentGroupId, value);
    LengthMatchManager::instance().measureAll(m_scene);
    updateNetTable();
    updateStatus();
}

void LengthMatchingDialog::onIntraPairToleranceChanged(double value) {
    if (m_currentGroupId.isEmpty()) return;
    LengthMatchManager::instance().setIntraPairTolerance(m_currentGroupId, value);
    LengthMatchManager::instance().measureAll(m_scene);
    updateDiffPairTable();
    updateStatus();
}

void LengthMatchingDialog::onAutoTargetToggled(bool checked) {
    if (m_currentGroupId.isEmpty()) return;
    m_targetLengthSpin->setEnabled(!checked);
    LengthMatchManager::instance().setGroupTarget(m_currentGroupId,
                                                    m_targetLengthSpin->value(), checked);
    LengthMatchManager::instance().measureAll(m_scene);
    updateNetTable();
    updateStatus();
}
