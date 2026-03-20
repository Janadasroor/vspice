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
    
    m_startLabel = new QLabel("Interval Start:");
    layout->addWidget(m_startLabel, 1, 0);
    m_tStart = createReadOnlyEdit();
    layout->addWidget(m_tStart, 1, 1);
    
    m_endLabel = new QLabel("Interval End:");
    layout->addWidget(m_endLabel, 2, 0);
    m_tEnd = createReadOnlyEdit();
    layout->addWidget(m_tEnd, 2, 1);
    
    m_avgLabel = new QLabel("Average:");
    layout->addWidget(m_avgLabel, 3, 0);
    m_average = createReadOnlyEdit();
    layout->addWidget(m_average, 3, 1);
    
    m_rmsIntegralLabel = new QLabel("RMS:");
    layout->addWidget(m_rmsIntegralLabel, 4, 0);
    m_rmsIntegral = createReadOnlyEdit();
    layout->addWidget(m_rmsIntegral, 4, 1);

    m_bwLabel = new QLabel("BW:");
    layout->addWidget(m_bwLabel, 5, 0);
    m_bw = createReadOnlyEdit();
    layout->addWidget(m_bw, 5, 1);

    m_powerBwLabel = new QLabel("Power BW:");
    layout->addWidget(m_powerBwLabel, 6, 0);
    m_powerBw = createReadOnlyEdit();
    layout->addWidget(m_powerBw, 6, 1);

    setStyleSheet(R"(
        QDialog { background-color: #f0f0f0; border: 1px solid #ccc; color: black; min-width: 300px; }
        QLabel { font-weight: bold; color: #333; }
        QLineEdit { background-color: white; color: black; border: 1px solid #aaa; padding: 4px; font-family: 'Consolas', monospace; }
    )");

    m_bwLabel->setVisible(false);
    m_bw->setVisible(false);
    m_powerBwLabel->setVisible(false);
    m_powerBw->setVisible(false);
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

    // Time-domain labels
    m_startLabel->setText("Interval Start:");
    m_endLabel->setText("Interval End:");
    m_avgLabel->setText("Average:");

    m_bwLabel->setVisible(false);
    m_bw->setVisible(false);
    m_powerBwLabel->setVisible(false);
    m_powerBw->setVisible(false);
}

void AnalysisDialog::setAcValues(const QString &seriesName, const QString &startFreq, const QString &endFreq, const QString &reference, const QString &bw, const QString &powerBw) {
    m_titleLabel->setText("Waveform: " + seriesName);

    m_startLabel->setText("Start Frequency:");
    m_endLabel->setText("End Frequency:");
    m_avgLabel->setText("Reference:");
    m_rmsIntegralLabel->setText("BW:");

    m_tStart->setText(startFreq);
    m_tEnd->setText(endFreq);
    m_average->setText(reference);
    m_rmsIntegral->setText(bw);

    m_bwLabel->setVisible(false);
    m_bw->setVisible(false);
    m_powerBwLabel->setVisible(true);
    m_powerBw->setVisible(true);
    m_powerBwLabel->setText("Power BW:");
    m_powerBw->setText(powerBw);
}
