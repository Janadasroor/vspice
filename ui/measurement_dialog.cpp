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
    
    layout->addWidget(new QLabel("Horz:"), 1, 0);
    m_h1 = createReadOnlyEdit();
    layout->addWidget(m_h1, 1, 1);
    
    layout->addWidget(new QLabel("Vert:"), 1, 2);
    m_v1 = createReadOnlyEdit();
    layout->addWidget(m_v1, 1, 3);
    
    // Cursor 2
    layout->addWidget(new QLabel("Cursor 2"), 2, 0);
    m_seriesLabel2 = new QLabel("None");
    layout->addWidget(m_seriesLabel2, 2, 1, 1, 2);
    
    layout->addWidget(new QLabel("Horz:"), 3, 0);
    m_h2 = createReadOnlyEdit();
    layout->addWidget(m_h2, 3, 1);
    
    layout->addWidget(new QLabel("Vert:"), 3, 2);
    m_v2 = createReadOnlyEdit();
    layout->addWidget(m_v2, 3, 3);
    
    // Diff
    layout->addWidget(new QLabel("Diff (Cursor 2 - Cursor 1)"), 4, 0, 1, 4);
    
    layout->addWidget(new QLabel("Horz (Δt):"), 5, 0);
    m_dh = createReadOnlyEdit();
    layout->addWidget(m_dh, 5, 1);
    
    layout->addWidget(new QLabel("Vert (ΔV/Vpp):"), 5, 2);
    m_dv = createReadOnlyEdit();
    layout->addWidget(m_dv, 5, 3);
    
    layout->addWidget(new QLabel("Freq:"), 6, 0);
    m_freq = createReadOnlyEdit();
    layout->addWidget(m_freq, 6, 1);
    
    layout->addWidget(new QLabel("Slope:"), 6, 2);
    m_slope = createReadOnlyEdit();
    layout->addWidget(m_slope, 6, 3);

    setStyleSheet(R"(
        QDialog { background-color: #f0f0f0; border: 1px solid #ccc; color: black; }
        QLabel { font-weight: bold; color: #333; }
        QLineEdit { background-color: white; color: black; border: 1px solid #aaa; padding: 2px; }
    )");
}

QLineEdit* MeasurementDialog::createReadOnlyEdit() {
    auto *edit = new QLineEdit(this);
    edit->setReadOnly(true);
    edit->setFixedWidth(120);
    return edit;
}

void MeasurementDialog::updateValues(const QString &seriesName, const QString &h1, const QString &v1, const QString &h2, const QString &v2, const QString &dh, const QString &dv, const QString &freq, const QString &slope) {
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
