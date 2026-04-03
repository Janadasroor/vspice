#ifndef GERBER_EXPORT_DIALOG_H
#define GERBER_EXPORT_DIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QLineEdit>
#include <QCheckBox>
#include "../layers/pcb_layer.h"

class GerberExportDialog : public QDialog {
    Q_OBJECT

public:
    explicit GerberExportDialog(QWidget* parent = nullptr);
    ~GerberExportDialog();

    QList<int> selectedLayers() const;
    QString outputDirectory() const;

private slots:
    void onBrowse();
    void onExport();

private:
    void setupUI();
    void populateLayers();

    QListWidget* m_layerList;
    QLineEdit* m_dirEdit;
    QCheckBox* m_drillCheck;
};

#endif // GERBER_EXPORT_DIALOG_H
