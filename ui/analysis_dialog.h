// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Janada Sroor

#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QGridLayout>

class AnalysisDialog : public QDialog {
    Q_OBJECT
public:
    explicit AnalysisDialog(QWidget *parent = nullptr);
    void setValues(const QString &seriesName, const QString &tStart, const QString &tEnd, const QString &average, const QString &rms_or_integral, bool isPower);
    void setAcValues(const QString &seriesName, const QString &startFreq, const QString &endFreq, const QString &reference, const QString &bw, const QString &powerBw);

private:
    QLabel *m_titleLabel;
    QLabel *m_startLabel;
    QLabel *m_endLabel;
    QLabel *m_avgLabel;
    QLineEdit *m_tStart;
    QLineEdit *m_tEnd;
    QLineEdit *m_average;
    QLineEdit *m_rmsIntegral;
    QLabel *m_rmsIntegralLabel;
    QLabel *m_bwLabel;
    QLineEdit *m_bw;
    QLabel *m_powerBwLabel;
    QLineEdit *m_powerBw;

    QLineEdit* createReadOnlyEdit();
};
