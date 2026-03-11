#ifndef VIRTUAL_INSTRUMENTS_H
#define VIRTUAL_INSTRUMENTS_H

#include <QWidget>
#include <QPainter>
#include <QTimer>
#include <QVector>
#include <QPointF>
#include <QLabel>

/**
 * @brief Professional real-time oscilloscope widget with CRT-style phosphor glow.
 */
class OscilloscopeWidget : public QWidget {
    Q_OBJECT
public:
    explicit OscilloscopeWidget(QWidget* parent = nullptr);
    void addData(const QString& channel, double x, double y);
    void setChannelData(const QString& channel, const QVector<QPointF>& points);
    void beginBatchUpdate();
    void endBatchUpdate();
    void clear();

    void setTimePerDiv(double ms) { m_timePerDiv = ms; requestRepaint(); }
    void setVoltsPerDiv(double v);
    void setTriggerLevel(double v) { m_triggerLevel = v; requestRepaint(); }
    void setActiveChannel(const QString& name);
    
    void setVerticalOffset(const QString& channel, double volts);
    void setHorizontalOffset(double ms) { m_horizontalOffset = ms; requestRepaint(); }
    void setTimeCursor(double t) { m_timeCursor = t; requestRepaint(); }
    
    void zoomInX();
    void zoomOutX();
    void zoomInY();
    void zoomOutY();
    
    void autoScale();
    void setFftEnabled(bool enabled);
    bool isFftEnabled() const { return m_fftEnabled; }

    static QString formatValue(double val, const QString& unit = "");

    enum class TriggerMode { Auto, Normal, Single };
    void setTriggerMode(TriggerMode mode) { m_triggerMode = mode; requestRepaint(); }
    void setTriggerSource(const QString& channel) { m_triggerChannel = channel; requestRepaint(); }

signals:
    void timePerDivChanged(double ms);
    void voltsPerDivChanged(double v);
    void horizontalOffsetChanged(double ms);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    QMap<QString, QVector<QPointF>> m_data;
    double m_timePerDiv;   // ms/div
    double m_voltsPerDiv;  // v/div (default/active)
    QMap<QString, double> m_voltsPerDivs; // Per-channel v/div
    double m_triggerLevel; // volts
    QString m_activeChannel;

    QMap<QString, double> m_verticalOffsets; // Volts
    double m_horizontalOffset = 0.0;         // ms
    double m_timeCursor = -1.0;              // seconds

    TriggerMode m_triggerMode = TriggerMode::Auto;
    QString m_triggerChannel;
    bool m_singleTriggered = false;

    struct Measurements {
        double vpp = 0;
        double vrms = 0;
        double freq = 0;
    };
    QMap<QString, Measurements> m_measurements;
    void updateMeasurements(const QString& channel);
    void requestRepaint();
    bool m_batchMode = false;
    bool m_repaintPending = false;

    // Time Cursors (X) - 0.0 to 1.0
    double m_cursorA = 0.25;
    double m_cursorB = 0.75;
    
    // Voltage Cursors (Y) - 0.0 to 1.0 (bottom to top)
    double m_cursorV1 = 0.3;
    double m_cursorV2 = 0.7;

    // Tracking Cursor (LTSpice style)
    bool m_showTracking = false;
    QPointF m_trackingPoint; // Data coordinates (t, V)
    QString m_trackingChannel;

    bool m_fftEnabled = false;
    struct FftTrace {
        QVector<QPointF> points; // (freq, dBV)
    };
    QMap<QString, FftTrace> m_fftData;

    int m_draggingCursor = 0; // 0=none, 1=A, 2=B, 3=V1, 4=V2
    QPoint m_lastMousePos;

    // Box Zoom
    bool m_zoomRectActive = false;
    QRect m_zoomRect;
};

/**
 * @brief Professional 8-channel logic analyzer widget.
 */
class LogicAnalyzerWidget : public QWidget {
    Q_OBJECT
public:
    explicit LogicAnalyzerWidget(QWidget* parent = nullptr);
    void addDigitalData(const QString& channel, double time, bool level);
    void setChannelData(const QString& channel, const QVector<QPointF>& points);
    void beginBatchUpdate();
    void endBatchUpdate();
    void clear();

    void setTimePerDiv(double ms) { m_timePerDiv = ms; requestRepaint(); }
    void setHorizontalOffset(double ms) { m_horizontalOffset = ms; requestRepaint(); }
    void setThreshold(double v) { m_threshold = v; requestRepaint(); }
    
    enum class Protocol { None, UART, SPI, I2C };
    struct DecoderConfig {
        Protocol protocol = Protocol::None;
        QString channelA; // RX for UART, CLK for SPI/I2C
        QString channelB; // TX for UART, MOSI for SPI, SDA for I2C
        QString channelC; // CS for SPI
        int baudRate = 9600;
    };
    void addDecoder(const DecoderConfig& config);
    void clearDecoders();

    void zoomInX();
    void zoomOutX();
    void autoScale();

signals:
    void timePerDivChanged(double ms);
    void horizontalOffsetChanged(double ms);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct DigitalSample { double t; bool val; };
    QMap<QString, QVector<DigitalSample>> m_channels;
    
    struct DecodedSymbol {
        double startTime;
        double endTime;
        QString text;
        QColor color;
        QString channel;
    };
    QList<DecodedSymbol> m_decodedData;
    QList<DecoderConfig> m_decoders;
    
    void runDecoders();
    void decodeUART(const DecoderConfig& cfg);
    
    double m_timePerDiv = 1.0;      // ms/div
    double m_horizontalOffset = 0.0; // ms
    double m_threshold = 2.5;        // Volts
    
    bool m_batchMode = false;
    bool m_repaintPending = false;
    void requestRepaint();
    
    QPoint m_lastMousePos;
};

/**
 * @brief Compact digital-style panel meter for voltage readings.
 */
class VoltmeterWidget : public QWidget {
    Q_OBJECT
public:
    explicit VoltmeterWidget(QWidget* parent = nullptr);
    void setReading(const QString& signal, double valueVolts);
    void clear();

private:
    QLabel* m_signalLabel;
    QLabel* m_valueLabel;
};

/**
 * @brief Compact digital-style panel meter for current readings.
 */
class AmmeterWidget : public QWidget {
    Q_OBJECT
public:
    explicit AmmeterWidget(QWidget* parent = nullptr);
    void setReading(const QString& signal, double valueAmps);
    void clear();

private:
    QLabel* m_signalLabel;
    QLabel* m_valueLabel;
};

/**
 * @brief Compact digital-style panel meter for power readings.
 */
class WattmeterWidget : public QWidget {
    Q_OBJECT
public:
    explicit WattmeterWidget(QWidget* parent = nullptr);
    void setReading(const QString& signal, double valueWatts);
    void clear();

private:
    QLabel* m_signalLabel;
    QLabel* m_valueLabel;
};

/**
 * @brief Frequency counter display widget.
 */
class FrequencyCounterWidget : public QWidget {
    Q_OBJECT
public:
    explicit FrequencyCounterWidget(QWidget* parent = nullptr);
    void setReading(const QString& signal, double valueHz);
    void clear();

private:
    QLabel* m_signalLabel;
    QLabel* m_valueLabel;
};

/**
 * @brief Logic probe indicator widget.
 */
class LogicProbeWidget : public QWidget {
    Q_OBJECT
public:
    explicit LogicProbeWidget(QWidget* parent = nullptr);
    void setState(const QString& signal, bool high, double valueVolts);
    void clear();

private:
    QLabel* m_signalLabel;
    QLabel* m_stateLabel;
};

#endif // VIRTUAL_INSTRUMENTS_H
