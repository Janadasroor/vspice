#ifndef LENGTH_MATCHING_DIALOG_H
#define LENGTH_MATCHING_DIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QGroupBox>
#include <QComboBox>
#include <QTextEdit>
#include <QCheckBox>
#include "../analysis/length_match_manager.h"

class QGraphicsScene;

/**
 * @brief Dialog for managing length match groups and auto-tuning serpentine patterns.
 */
class LengthMatchingDialog : public QDialog {
    Q_OBJECT

public:
    explicit LengthMatchingDialog(QGraphicsScene* scene, QWidget* parent = nullptr);
    ~LengthMatchingDialog();

private slots:
    void onCreateGroup();
    void onDeleteGroup();
    void onAddNet();
    void onRemoveNet();
    void onMeasure();
    void onAutoTune();
    void onGroupSelected(const QString& groupId);
    void onToleranceChanged(double value);
    void onIntraPairToleranceChanged(double value);
    void onAutoTargetToggled(bool checked);

private:
    void setupUI();
    void updateGroupList();
    void updateNetTable();
    void updateDiffPairTable();
    void updateStatus();
    void refreshAll();

    QGraphicsScene* m_scene;

    // Left panel: Group list
    QListWidget* m_groupList;
    QPushButton* m_createGroupBtn;
    QPushButton* m_deleteGroupBtn;

    // Center panel: Nets in group
    QTableWidget* m_netTable;
    QPushButton* m_addNetBtn;
    QPushButton* m_removeNetBtn;

    // Right panel: Differential pair skew
    QTableWidget* m_diffPairTable;

    // Bottom panel: Settings
    QDoubleSpinBox* m_toleranceSpin;
    QDoubleSpinBox* m_intraPairToleranceSpin;
    QDoubleSpinBox* m_targetLengthSpin;
    QCheckBox* m_autoTargetCheck;
    QDoubleSpinBox* m_serpAmplitudeSpin;
    QDoubleSpinBox* m_serpSpacingSpin;
    QLabel* m_statusLabel;

    QPushButton* m_measureBtn;
    QPushButton* m_autoTuneBtn;
    QPushButton* m_closeBtn;

    QString m_currentGroupId;
};

#endif // LENGTH_MATCHING_DIALOG_H
