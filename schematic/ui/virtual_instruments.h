#ifndef VIRTUAL_INSTRUMENTS_H
#define VIRTUAL_INSTRUMENTS_H

#include <QWidget>
#include <QPainter>
#include <QTimer>
#include <QVector>
#include <QPointF>
#include <QLabel>

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
