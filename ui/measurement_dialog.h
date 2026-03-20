// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Janada Sroor

#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QGridLayout>

class MeasurementDialog : public QDialog {
    Q_OBJECT
public:
    explicit MeasurementDialog(QWidget *parent = nullptr);
    void setAcMode(bool enabled);
    void updateValues(const QString &seriesName, const QString &h1, const QString &v1, const QString &h2, const QString &v2, const QString &dh, const QString &dv, const QString &freq, const QString &slope);
    void updateAcValues(const QString &seriesName,
                        const QString &f1, const QString &mag1, const QString &ph1, const QString &gd1,
                        const QString &f2, const QString &mag2, const QString &ph2, const QString &gd2,
                        const QString &df, const QString &dmag, const QString &dph, const QString &dgd);

private:
    QLabel *m_seriesLabel1;
    QLabel *m_seriesLabel2;
    QLabel *m_h1Label;
    QLabel *m_v1Label;
    QLabel *m_h2Label;
    QLabel *m_v2Label;
    QLabel *m_diffLabel;
    QLabel *m_dhLabel;
    QLabel *m_dvLabel;
    QLabel *m_freqLabel;
    QLabel *m_slopeLabel;
    QLabel *m_phase1Label;
    QLabel *m_phase2Label;
    QLabel *m_dphaseLabel;
    QLabel *m_gd1Label;
    QLabel *m_gd2Label;
    QLabel *m_dgdLabel;
    QLineEdit *m_h1;
    QLineEdit *m_v1;
    QLineEdit *m_h2;
    QLineEdit *m_v2;
    QLineEdit *m_dh;
    QLineEdit *m_dv;
    QLineEdit *m_freq;
    QLineEdit *m_slope;
    QLineEdit *m_phase1;
    QLineEdit *m_phase2;
    QLineEdit *m_dphase;
    QLineEdit *m_gd1;
    QLineEdit *m_gd2;
    QLineEdit *m_dgd;

    QLineEdit* createReadOnlyEdit();
};
