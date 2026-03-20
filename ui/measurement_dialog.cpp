// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Janada Sroor

#include "measurement_dialog.h"
#include <cmath>

MeasurementDialog::MeasurementDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("VioView Cursor Measurements");
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
    
    auto *layout = new QGridLayout(this);
    
    // Cursor 1
    layout->addWidget(new QLabel("Cursor 1"), 0, 0);
    m_seriesLabel1 = new QLabel("None");
    layout->addWidget(m_seriesLabel1, 0, 1, 1, 2);
    
    m_h1Label = new QLabel("Horz:");
    layout->addWidget(m_h1Label, 1, 0);
    m_h1 = createReadOnlyEdit();
    layout->addWidget(m_h1, 1, 1);
    
    m_v1Label = new QLabel("Vert:");
    layout->addWidget(m_v1Label, 1, 2);
    m_v1 = createReadOnlyEdit();
    layout->addWidget(m_v1, 1, 3);

    m_phase1Label = new QLabel("Phase:");
    layout->addWidget(m_phase1Label, 2, 0);
    m_phase1 = createReadOnlyEdit();
    layout->addWidget(m_phase1, 2, 1);

    m_gd1Label = new QLabel("Group Delay:");
    layout->addWidget(m_gd1Label, 2, 2);
    m_gd1 = createReadOnlyEdit();
    layout->addWidget(m_gd1, 2, 3);
    
    // Cursor 2
    layout->addWidget(new QLabel("Cursor 2"), 3, 0);
    m_seriesLabel2 = new QLabel("None");
    layout->addWidget(m_seriesLabel2, 3, 1, 1, 2);
    
    m_h2Label = new QLabel("Horz:");
    layout->addWidget(m_h2Label, 4, 0);
    m_h2 = createReadOnlyEdit();
    layout->addWidget(m_h2, 4, 1);
    
    m_v2Label = new QLabel("Vert:");
    layout->addWidget(m_v2Label, 4, 2);
    m_v2 = createReadOnlyEdit();
    layout->addWidget(m_v2, 4, 3);

    m_phase2Label = new QLabel("Phase:");
    layout->addWidget(m_phase2Label, 5, 0);
    m_phase2 = createReadOnlyEdit();
    layout->addWidget(m_phase2, 5, 1);

    m_gd2Label = new QLabel("Group Delay:");
    layout->addWidget(m_gd2Label, 5, 2);
    m_gd2 = createReadOnlyEdit();
    layout->addWidget(m_gd2, 5, 3);
    
    // Diff
    m_diffLabel = new QLabel("Diff (Cursor 2 - Cursor 1)");
    layout->addWidget(m_diffLabel, 6, 0, 1, 4);
    
    m_dhLabel = new QLabel("Horz (Δt):");
    layout->addWidget(m_dhLabel, 7, 0);
    m_dh = createReadOnlyEdit();
    layout->addWidget(m_dh, 7, 1);
    
    m_dvLabel = new QLabel("Vert (ΔV/Vpp):");
    layout->addWidget(m_dvLabel, 7, 2);
    m_dv = createReadOnlyEdit();
    layout->addWidget(m_dv, 7, 3);
    
    m_freqLabel = new QLabel("Freq:");
    layout->addWidget(m_freqLabel, 8, 0);
    m_freq = createReadOnlyEdit();
    layout->addWidget(m_freq, 8, 1);
    
    m_slopeLabel = new QLabel("Slope:");
    layout->addWidget(m_slopeLabel, 8, 2);
    m_slope = createReadOnlyEdit();
    layout->addWidget(m_slope, 8, 3);

    m_dphaseLabel = new QLabel("Phase (Δ°):");
    layout->addWidget(m_dphaseLabel, 9, 0);
    m_dphase = createReadOnlyEdit();
    layout->addWidget(m_dphase, 9, 1);

    m_dgdLabel = new QLabel("Group Delay (Δ):");
    layout->addWidget(m_dgdLabel, 9, 2);
    m_dgd = createReadOnlyEdit();
    layout->addWidget(m_dgd, 9, 3);

    setStyleSheet(R"(
        QDialog { background-color: #f0f0f0; border: 1px solid #ccc; color: black; }
        QLabel { font-weight: bold; color: #333; }
        QLineEdit { background-color: white; color: black; border: 1px solid #aaa; padding: 2px; }
    )");

    setAcMode(false);
}

QLineEdit* MeasurementDialog::createReadOnlyEdit() {
    auto *edit = new QLineEdit(this);
    edit->setReadOnly(true);
    edit->setFixedWidth(120);
    return edit;
}

void MeasurementDialog::updateValues(const QString &seriesName, const QString &h1, const QString &v1, const QString &h2, const QString &v2, const QString &dh, const QString &dv, const QString &freq, const QString &slope) {
    if (m_phase1) m_phase1->clear();
    if (m_phase2) m_phase2->clear();
    if (m_dphase) m_dphase->clear();
    if (m_gd1) m_gd1->clear();
    if (m_gd2) m_gd2->clear();
    if (m_dgd) m_dgd->clear();

    m_seriesLabel1->setText(seriesName);
    m_seriesLabel2->setText(seriesName);
    
    m_h1->setText(h1);
    m_v1->setText(v1);
    m_h2->setText(h2);
    m_v2->setText(v2);
    
    m_dh->setText(dh);
    m_dv->setText(dv);
    m_freq->setText(freq);
    m_slope->setText(slope);
}

void MeasurementDialog::setAcMode(bool enabled) {
    m_h1Label->setText(enabled ? "Freq:" : "Horz:");
    m_v1Label->setText(enabled ? "Mag:" : "Vert:");
    m_h2Label->setText(enabled ? "Freq:" : "Horz:");
    m_v2Label->setText(enabled ? "Mag:" : "Vert:");
    m_diffLabel->setText(enabled ? "Ratio (Cursor 2 / Cursor 1)" : "Diff (Cursor 2 - Cursor 1)");
    m_dhLabel->setText(enabled ? "Freq (Δf):" : "Horz (Δt):");
    m_dvLabel->setText(enabled ? "Mag (ΔdB):" : "Vert (ΔV/Vpp):");

    m_freqLabel->setVisible(!enabled);
    m_freq->setVisible(!enabled);
    m_slopeLabel->setVisible(!enabled);
    m_slope->setVisible(!enabled);

    m_phase1Label->setVisible(enabled);
    m_phase1->setVisible(enabled);
    m_gd1Label->setVisible(enabled);
    m_gd1->setVisible(enabled);
    m_phase2Label->setVisible(enabled);
    m_phase2->setVisible(enabled);
    m_gd2Label->setVisible(enabled);
    m_gd2->setVisible(enabled);
    m_dphaseLabel->setVisible(enabled);
    m_dphase->setVisible(enabled);
    m_dgdLabel->setVisible(enabled);
    m_dgd->setVisible(enabled);
}

void MeasurementDialog::updateAcValues(const QString &seriesName,
                                       const QString &f1, const QString &mag1, const QString &ph1, const QString &gd1,
                                       const QString &f2, const QString &mag2, const QString &ph2, const QString &gd2,
                                       const QString &df, const QString &dmag, const QString &dph, const QString &dgd) {
    m_seriesLabel1->setText(seriesName);
    m_seriesLabel2->setText(seriesName);

    m_h1->setText(f1);
    m_v1->setText(mag1);
    m_phase1->setText(ph1);
    m_gd1->setText(gd1);

    m_h2->setText(f2);
    m_v2->setText(mag2);
    m_phase2->setText(ph2);
    m_gd2->setText(gd2);

    m_dh->setText(df);
    m_dv->setText(dmag);
    m_dphase->setText(dph);
    m_dgd->setText(dgd);
}
