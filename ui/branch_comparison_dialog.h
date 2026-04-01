#ifndef BRANCH_COMPARISON_DIALOG_H
#define BRANCH_COMPARISON_DIALOG_H

#include <QDialog>
#include <QJsonObject>
#include "../../ui/git_backend.h"

class QListWidget;
class QListWidgetItem;
class QLabel;
class QPushButton;
class QTableWidget;
class QSplitter;

class BranchComparisonDialog : public QDialog {
    Q_OBJECT

public:
    explicit BranchComparisonDialog(const QString& workingDir, QWidget* parent = nullptr);
    ~BranchComparisonDialog();

    void compareBranches(const QString& branchA, const QString& branchB);

signals:
    void mergeRequested(const QString& sourceBranch, const QString& targetBranch);
    void checkoutRequested(const QString& branch);

private slots:
    void onBranchAChanged(int index);
    void onBranchBChanged(int index);
    void onCompareClicked();
    void onMergeClicked();
    void onFileDoubleClicked(QListWidgetItem* item);
    void onRefreshClicked();

private:
    void setupUi();
    void loadBranches();
    void populateComparisonTable();
    void compareSchematics(const QString& branchA, const QString& branchB);

    QString m_workingDir;
    QString m_branchA;
    QString m_branchB;

    QComboBox* m_branchACombo;
    QComboBox* m_branchBCombo;
    QPushButton* m_compareBtn;
    QPushButton* m_mergeBtn;
    QPushButton* m_refreshBtn;
    QTableWidget* m_comparisonTable;
    QLabel* m_summaryLabel;
    QSplitter* m_splitter;

    QVector<GitBranch> m_branches;
    struct FileDiff {
        QString path;
        QString status;
        int addedLines;
        int removedLines;
        bool isSchematic;
    };
    QList<FileDiff> m_fileDiffs;
};

#endif // BRANCH_COMPARISON_DIALOG_H