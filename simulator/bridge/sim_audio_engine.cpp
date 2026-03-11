#include "sim_audio_engine.h"
#include <QMediaDevices>
#include <QAudioDevice>
#include <QDebug>
#include <cmath>
#include <algorithm>
#include "../core/sim_math.h"

SimAudioEngine& SimAudioEngine::instance() {
    static SimAudioEngine inst;
    return inst;
}

SimAudioEngine::SimAudioEngine(QObject* parent) : QObject(parent) {
    setupAudio();
}

SimAudioEngine::~SimAudioEngine() {
    stop();
}

void SimAudioEngine::setupAudio() {
    m_format.setSampleRate(44100);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);

    QAudioDevice info = QMediaDevices::defaultAudioOutput();
    if (!info.isFormatSupported(m_format)) {
        qWarning() << "Default audio format not supported, trying to find suitable format.";
        m_format = info.preferredFormat();
    }
}

void SimAudioEngine::playWaveform(const SimWaveform& wave, float volume) {
    stop();
    if (wave.yData.empty()) return;

    // Resample to 44.1kHz
    int sampleRate = m_format.sampleRate();
    m_outputDevice = new WaveformStreamDevice(wave.yData, sampleRate, this);
    m_outputDevice->open(QIODevice::ReadOnly);

    m_sink = new QAudioSink(m_format, this);
    m_sink->setVolume(volume);
    
    connect(m_sink, &QAudioSink::stateChanged, this, [this](QAudio::State state) {
        if (state == QAudio::IdleState || state == QAudio::StoppedState) {
            stop();
        }
    });

    m_sink->start(m_outputDevice);
    m_active = true;
}

void SimAudioEngine::stop() {
    if (m_sink) {
        m_sink->stop();
        delete m_sink;
        m_sink = nullptr;
    }
    if (m_outputDevice) {
        m_outputDevice->close();
        delete m_outputDevice;
        m_outputDevice = nullptr;
    }
    m_active = false;
}

// --- WaveformStreamDevice Implementation ---

WaveformStreamDevice::WaveformStreamDevice(const std::vector<double>& yData, int sampleRate, QObject* parent)
    : QIODevice(parent) {
    
    // Normalize and convert to Int16
    double maxVal = 0.01;
    for (double v : yData) maxVal = std::max(maxVal, std::abs(v));
    
    m_samples.reserve(static_cast<int>(yData.size()));
    for (double v : yData) {
        // Simple normalization to +/- 30000 (just below int16 max)
        double norm = (v / maxVal) * 30000.0;
        m_samples.append(static_cast<int16_t>(std::clamp(norm, -32767.0, 32767.0)));
    }
}

qint64 WaveformStreamDevice::readData(char* data, qint64 maxlen) {
    qint64 total = 0;
    while (maxlen >= (int)sizeof(int16_t) && m_pos < (qint64)(m_samples.size() * sizeof(int16_t))) {
        int index = m_pos / sizeof(int16_t);
        int16_t sample = m_samples[index];
        memcpy(data + total, &sample, sizeof(int16_t));
        total += sizeof(int16_t);
        m_pos += sizeof(int16_t);
        maxlen -= sizeof(int16_t);
    }
    return total;
}
