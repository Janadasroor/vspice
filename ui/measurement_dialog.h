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
    void updateValues(const QString &seriesName, const QString &h1, const QString &v1, const QString &h2, const QString &v2, const QString &dh, const QString &dv, const QString &freq, const QString &slope);

private:
    QLabel *m_seriesLabel1;
    QLabel *m_seriesLabel2;
    QLineEdit *m_h1;
    QLineEdit *m_v1;
    QLineEdit *m_h2;
    QLineEdit *m_v2;
    QLineEdit *m_dh;
    QLineEdit *m_dv;
    QLineEdit *m_freq;
    QLineEdit *m_slope;

    QLineEdit* createReadOnlyEdit();
};
