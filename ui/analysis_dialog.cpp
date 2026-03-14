// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Janada Sroor

#include "analysis_dialog.h"
#include <cmath>

AnalysisDialog::AnalysisDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Waveform Analysis");
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
    
    auto *layout = new QGridLayout(this);
    
    m_titleLabel = new QLabel("Waveform: ");
    layout->addWidget(m_titleLabel, 0, 0, 1, 2);
    
    layout->addWidget(new QLabel("Interval Start:"), 1, 0);
    m_tStart = createReadOnlyEdit();
    layout->addWidget(m_tStart, 1, 1);
    
    layout->addWidget(new QLabel("Interval End:"), 2, 0);
    m_tEnd = createReadOnlyEdit();
    layout->addWidget(m_tEnd, 2, 1);
    
    layout->addWidget(new QLabel("Average:"), 3, 0);
    m_average = createReadOnlyEdit();
    layout->addWidget(m_average, 3, 1);
    
    m_rmsIntegralLabel = new QLabel("RMS:");
    layout->addWidget(m_rmsIntegralLabel, 4, 0);
    m_rmsIntegral = createReadOnlyEdit();
    layout->addWidget(m_rmsIntegral, 4, 1);

    setStyleSheet(R"(
        QDialog { background-color: #f0f0f0; border: 1px solid #ccc; color: black; min-width: 300px; }
        QLabel { font-weight: bold; color: #333; }
        QLineEdit { background-color: white; color: black; border: 1px solid #aaa; padding: 4px; font-family: 'Consolas', monospace; }
    )");
}

QLineEdit* AnalysisDialog::createReadOnlyEdit() {
    auto *edit = new QLineEdit(this);
    edit->setReadOnly(true);
    edit->setFixedWidth(180);
    edit->setAlignment(Qt::AlignRight);
    return edit;
}

void AnalysisDialog::setValues(const QString &seriesName, const QString &tStart, const QString &tEnd, const QString &average, const QString &rms_or_integral, bool isPower) {
    m_titleLabel->setText("Waveform: " + seriesName);
    m_tStart->setText(tStart);
    m_tEnd->setText(tEnd);
    m_average->setText(average);
    
    if (isPower) {
        m_rmsIntegralLabel->setText("Integral:");
    } else {
        m_rmsIntegralLabel->setText("RMS:");
    }
    m_rmsIntegral->setText(rms_or_integral);
}
