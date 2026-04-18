#ifndef WAVEFORM_DRAW_WIDGET_H
#define WAVEFORM_DRAW_WIDGET_H

#include <QWidget>
#include <QVector>
#include <QPointF>

class WaveformDrawWidget : public QWidget {
    Q_OBJECT
public:
    explicit WaveformDrawWidget(QWidget* parent = nullptr);

    void clearPoints();
    void setPoints(const QVector<QPointF>& points);
    QVector<QPointF> points() const;

    void setSnapToGrid(bool enabled);
    void setStepMode(bool enabled);
    bool isStepMode() const;

    void setPolylineMode(bool enabled);
    bool isPolylineMode() const { return m_polylineMode; }

    // Transformations
    void reverseTime();
    void shiftTime(double delta);
    void scaleTime(double factor);
    void scaleValue(double factor);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void addPoint(const QPoint& pos);
    QPointF toNormalized(const QPoint& pos) const;
    QPointF toWidget(const QPointF& n) const;

    QVector<QPointF> m_points;
    QPointF m_previewPoint;
    bool m_drawing;
    bool m_snapToGrid;
    bool m_stepMode;
    bool m_polylineMode;
    bool m_hasPreview;
};

#endif // WAVEFORM_DRAW_WIDGET_H
