#ifndef PCBDRCPANEL_H
#define PCBDRCPANEL_H

#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QProgressBar>
#include "pcb_drc.h"

class QGraphicsScene;

class PCBDRCPanel : public QWidget {
    Q_OBJECT

public:
    explicit PCBDRCPanel(QWidget* parent = nullptr);

    void setScene(QGraphicsScene* scene) { m_scene = scene; }
    PCBDRC* drc() { return &m_drc; }

signals:
    void violationSelected(QPointF location);
    void checkCompleted(int errors, int warnings);

public slots:
    void runCheck();
    void clearResults();

private slots:
    void onCheckStarted();
    void onCheckProgress(int percent, const QString& message);
    void onCheckCompleted(int errorCount, int warningCount);
    void onViolationSelected(QListWidgetItem* item);

private:
    void setupUI();
    void addViolationToList(const DRCViolation& violation);

    PCBDRC m_drc;
    QGraphicsScene* m_scene;

    QLabel* m_statusLabel;
    QLabel* m_summaryLabel;
    QProgressBar* m_progressBar;
    QListWidget* m_violationList;
    QPushButton* m_runButton;
    QPushButton* m_clearButton;
};

#endif // PCBDRCPANEL_H
