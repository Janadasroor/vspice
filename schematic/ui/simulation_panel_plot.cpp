void SimulationPanel::plotBuiltinResults(const SimResults& results) {
    m_chart->removeAllSeries();
    for (auto* axis : m_chart->axes()) m_chart->removeAxis(axis);
    
    m_spectrumChart->removeAllSeries();
    for (auto* axis : m_spectrumChart->axes()) m_spectrumChart->removeAxis(axis);

    m_signalList->blockSignals(true);
    m_signalList->clear();
    if (m_measurementsTable) m_measurementsTable->setRowCount(0);
    
    QString lastScopeChannel = m_scopeChannelCombo->currentText();
    m_scopeChannelCombo->blockSignals(true);
    m_scopeChannelCombo->clear();
    for(const auto& w : results.waveforms) {
        m_scopeChannelCombo->addItem(QString::fromStdString(w.name));
    }
    if (m_scopeChannelCombo->count() > 0) {
        int idx = m_scopeChannelCombo->findText(lastScopeChannel);
        if (idx >= 0) m_scopeChannelCombo->setCurrentIndex(idx);
        else m_scopeChannelCombo->setCurrentIndex(0);
    }
    m_scopeChannelCombo->blockSignals(false);
    if (m_oscilloscope) m_oscilloscope->setActiveChannel(m_scopeChannelCombo->currentText());

    if (results.waveforms.empty()) {
        if (!results.sensitivities.empty()) {
            m_logOutput->append("\n--- Sensitivity Analysis ---");
            for (const auto& [name, val] : results.sensitivities) {
                m_logOutput->append(QString("dTarget/d(%1) = %2").arg(QString::fromStdString(name)).arg(val));
            }
        } else {
            m_logOutput->append("\n--- DC Operating Point ---");
            for (const auto& [name, val] : results.nodeVoltages) {
                m_logOutput->append(QString("V(%1) = %2 V").arg(QString::fromStdString(name)).arg(val));
            }
        }
        m_signalList->blockSignals(false);
        return;
    }

    m_chart->legend()->hide();

    int analysisIdx = m_analysisType->currentIndex();
    QAbstractAxis* axisX = nullptr;
    QValueAxis* axisYPhase = nullptr;
    
    if (analysisIdx == 2) { // AC Sweep / Bode Plot
        auto* logX = new QLogValueAxis();
        logX->setTitleText("Frequency (Hz)");
        logX->setBase(10.0);
        logX->setLabelFormat("%.0e");
        axisX = logX;

        axisYPhase = new QValueAxis();
        axisYPhase->setTitleText("Phase (Deg)");
        axisYPhase->setRange(-180, 180);
        axisYPhase->setLabelFormat("%d");
        axisYPhase->setGridLineVisible(false);
        m_chart->addAxis(axisYPhase, Qt::AlignRight);
    } else {
        auto* valX = new QValueAxis();
        if (analysisIdx == 3) valX->setTitleText("Run Number");
        else valX->setTitleText(axisLabelFromSchema(results.xAxisName));
        axisX = valX;
    }
    
    axisX->setGridLinePen(QPen(QColor("#d0d0d0"), 1, Qt::DotLine));
    m_chart->addAxis(axisX, Qt::AlignBottom);

    QValueAxis* axisY = new QValueAxis();
    if (analysisIdx == 2) axisY->setTitleText("Magnitude (dB)");
    else axisY->setTitleText(axisLabelFromSchema(results.yAxisName));
    axisY->setGridLinePen(QPen(QColor("#d0d0d0"), 1, Qt::DotLine));
    m_chart->addAxis(axisY, Qt::AlignLeft);

    const QList<QColor> colors = {Qt::red, Qt::blue, QColor("#00aa00"), Qt::magenta, Qt::darkCyan};
    int colorIdx = 0;
    QSet<QString> currentWaveNames;

    for (const auto& wave : results.waveforms) {
        currentWaveNames.insert(QString::fromStdString(wave.name));
        QLineSeries* series = new QLineSeries();
        series->setName(QString::fromStdString(wave.name));
        series->setPen(QPen(colors[colorIdx % colors.size()], 1.5));
        
        QLineSeries* phaseSeries = nullptr;
        if (analysisIdx == 2 && !wave.yPhase.empty()) {
            phaseSeries = new QLineSeries();
            phaseSeries->setName(series->name() + " (Phase)");
            QPen phasePen = series->pen();
            phasePen.setStyle(Qt::DashLine);
            phasePen.setWidthF(1.0);
            phaseSeries->setPen(phasePen);
        }

        const int targetPoints = 4000;
        
        if (analysisIdx == 2) {
            std::vector<double> dbData;
            dbData.reserve(wave.yData.size());
            for (double v : wave.yData) dbData.push_back(20.0 * std::log10(std::max(v, 1e-15)));
            series->replace(decimateMinMaxBuckets(wave.xData, dbData, targetPoints));
            if (phaseSeries) {
                phaseSeries->replace(decimateMinMaxBuckets(wave.xData, wave.yPhase, targetPoints));
            }
        } else {
            series->replace(decimateMinMaxBuckets(wave.xData, wave.yData, targetPoints));
        }

        m_chart->addSeries(series);
        series->attachAxis(axisX);
        series->attachAxis(axisY);

        if (phaseSeries) {
            m_chart->addSeries(phaseSeries);
            phaseSeries->attachAxis(axisX);
            phaseSeries->attachAxis(axisYPhase);
        }

        colorIdx++;

        if (analysisIdx == 0 && wave.yData.size() >= 64) {
            int nfft = 1024;
            std::vector<double> resampled = SimMath::resample(wave.xData, wave.yData, nfft);
            std::vector<std::complex<double>> complexIn(nfft);
            for(int i=0; i<nfft; ++i) complexIn[i] = resampled[i];
            auto complexOut = SimMath::fft(complexIn);
            QLineSeries* specSeries = new QLineSeries();
            specSeries->setPen(series->pen());
            double sampleRate = 1.0 / ( (wave.xData.back() - wave.xData.front()) / (wave.xData.size()-1) );
            for (int i = 0; i < nfft / 2; ++i) {
                double freq = i * sampleRate / nfft;
                double mag = 2.0 * std::abs(complexOut[i]) / nfft;
                if (i == 0) mag /= 2.0;
                specSeries->append(freq, 20.0 * std::log10(std::max(mag, 1e-9)));
            }
            m_spectrumChart->addSeries(specSeries);
        }

        QListWidgetItem* item = new QListWidgetItem(series->name());
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        item->setForeground(series->pen().color());
        m_signalList->addItem(item);

        if (m_measurementsTable) {
            int row = m_measurementsTable->rowCount();
            m_measurementsTable->insertRow(row);
            m_measurementsTable->setItem(row, 0, new QTableWidgetItem(series->name()));
            m_measurementsTable->item(row, 0)->setForeground(series->pen().color());
            
            double minVal = 1e30, maxVal = -1e30, sum = 0;
            for (double v : (analysisIdx == 2 ? wave.yData : wave.yData)) {
                minVal = std::min(minVal, v);
                maxVal = std::max(maxVal, v);
                sum += v;
            }
            double avgVal = wave.yData.empty() ? 0 : sum / wave.yData.size();

            m_measurementsTable->setItem(row, 1, new QTableWidgetItem(QString::number(maxVal - minVal, 'f', 3)));
            m_measurementsTable->setItem(row, 2, new QTableWidgetItem(QString::number(avgVal, 'f', 3)));
            double sumSq = 0.0;
            for (double v : wave.yData) sumSq += v * v;
            const double rmsVal = wave.yData.empty() ? 0.0 : std::sqrt(sumSq / static_cast<double>(wave.yData.size()));
            m_measurementsTable->setItem(row, 3, new QTableWidgetItem(QString::number(rmsVal, 'f', 3)));
            const double freqHz = (analysisIdx == 2) ? 0.0 : estimateFrequency(wave);
            QString freqStr = (freqHz > 0.0) ? QString("%1 Hz").arg(QString::number(freqHz, 'g', 4)) : "-";
            if (analysisIdx == 0) {
                const double fftPeak = estimateFftPeakHz(wave);
                if (fftPeak > 0.0) freqStr = QString("%1 (FFT %2)").arg(freqStr == "-" ? QString("~") : freqStr).arg(QString::number(fftPeak, 'g', 4));
            }
            m_measurementsTable->setItem(row, 4, new QTableWidgetItem(freqStr));
            QString deltaStr = "-";
            if (wave.xData.size() >= 2 && wave.yData.size() >= 2) {
                const double x0 = wave.xData.front();
                const double x1 = wave.xData.back();
                const double xa = x0 + (x1 - x0) * m_cursorAFrac;
                const double xb = x0 + (x1 - x0) * m_cursorBFrac;
                deltaStr = QString::number(sampleAtX(wave, xb) - sampleAtX(wave, xa), 'f', 3);
            }
            m_measurementsTable->setItem(row, 5, new QTableWidgetItem(deltaStr));
        }
    }

    if (m_overlayPreviousRun && m_overlayPreviousRun->isChecked() && m_hasPreviousResults) {
        for (const auto& wave : m_previousResults.waveforms) {
            const QString prevName = QString::fromStdString(wave.name);
            if (currentWaveNames.contains(prevName)) {
                QLineSeries* prevSeries = new QLineSeries();
                prevSeries->setName("Prev: " + prevName);
                prevSeries->setPen(QPen(QColor("#94a3b8"), 1.1, Qt::DashLine));
                if (analysisIdx == 2) {
                    std::vector<double> dbData;
                    dbData.reserve(wave.yData.size());
                    for (double v : wave.yData) dbData.push_back(20.0 * std::log10(std::max(v, 1e-15)));
                    prevSeries->replace(decimateMinMaxBuckets(wave.xData, dbData, 3000));
                } else {
                    prevSeries->replace(decimateMinMaxBuckets(wave.xData, wave.yData, 3000));
                }
                m_chart->addSeries(prevSeries);
                prevSeries->attachAxis(axisX);
                prevSeries->attachAxis(axisY);
            }
        }
    }

    if (!m_spectrumChart->series().isEmpty()) {
        QValueAxis* axisFreq = new QValueAxis();
        axisFreq->setTitleText("Frequency (Hz)");
        m_spectrumChart->addAxis(axisFreq, Qt::AlignBottom);
        for(auto* s : m_spectrumChart->series()) s->attachAxis(axisFreq);
        QValueAxis* axisMag = new QValueAxis();
        axisMag->setTitleText("Magnitude (dB)");
        m_spectrumChart->addAxis(axisMag, Qt::AlignLeft);
        for(auto* s : m_spectrumChart->series()) s->attachAxis(axisMag);
    }

    m_signalList->blockSignals(false);
}
