#ifndef SIM_AUDIO_ENGINE_H
#define SIM_AUDIO_ENGINE_H

#include <QObject>
#include <QIODevice>
#include <QAudioSink>
#include <QAudioFormat>
#include <QByteArray>
#include <QVector>
#include "../../simulator/core/sim_results.h"

class SimAudioEngine : public QObject {
    Q_OBJECT
public:
    static SimAudioEngine& instance();

    /**
     * @brief Start playing a waveform as audio.
     * @param wave The simulation waveform to play.
     * @param volume Volume 0.0 to 1.0
     */
    void playWaveform(const SimWaveform& wave, float volume = 0.5f);

    /**
     * @brief Stop playback.
     */
    void stop();

    bool isPlaying() const { return m_active; }

private:
    explicit SimAudioEngine(QObject* parent = nullptr);
    ~SimAudioEngine();

    void setupAudio();

    QAudioSink* m_sink = nullptr;
    QIODevice* m_outputDevice = nullptr;
    bool m_active = false;
    QAudioFormat m_format;
};

/**
 * @brief Internal helper to stream waveform data to QAudioSink.
 */
class WaveformStreamDevice : public QIODevice {
    Q_OBJECT
public:
    explicit WaveformStreamDevice(const std::vector<double>& yData, int sampleRate, QObject* parent = nullptr);

    bool isSequential() const override { return true; }
    qint64 readData(char* data, qint64 maxlen) override;
    qint64 writeData(const char* data, qint64 len) override { return 0; }
    qint64 bytesAvailable() const override { return m_samples.size() * sizeof(int16_t) - m_pos; }

private:
    QVector<int16_t> m_samples;
    qint64 m_pos = 0;
};

#endif // SIM_AUDIO_ENGINE_H
