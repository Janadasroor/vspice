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

private:
    QLabel *m_titleLabel;
    QLineEdit *m_tStart;
    QLineEdit *m_tEnd;
    QLineEdit *m_average;
    QLineEdit *m_rmsIntegral;
    QLabel *m_rmsIntegralLabel;

    QLineEdit* createReadOnlyEdit();
};
