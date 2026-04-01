#ifndef SMART_PROBE_OVERLAY_H
#define SMART_PROBE_OVERLAY_H

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QPropertyAnimation>

class SmartProbeOverlay : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double opacity READ opacity WRITE setOpacity)

public:
    explicit SmartProbeOverlay(QWidget* parent = nullptr);

    void showAt(const QPoint& pos, const QString& netName, const QString& instantVal, const QString& context);
    void updateAIAnnotation(const QString& text);
    void setAIStatus(const QString& status);
    void hideOverlay();

    double opacity() const { return m_opacity; }
    void setOpacity(double opacity);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QLabel* m_netLabel;
    QLabel* m_instantLabel;
    QLabel* m_contextLabel;
    QLabel* m_aiLabel;
    QLabel* m_aiStatusLabel;
    
    double m_opacity = 0.0;
    QPropertyAnimation* m_fadeAnimation;
};

#endif // SMART_PROBE_OVERLAY_H
