#ifndef VOLTAGESOURCEITEM_H
#define VOLTAGESOURCEITEM_H

#include "schematic_item.h"
#include "schematic_primitives.h"
#include <QBrush>
#include <QPen>
#include <memory>
#include <vector>

class VoltageSourceItem : public SchematicItem {
public:
    enum SourceType { 
        DC, 
        Sine, 
        Pulse, 
        EXP,     // Exponential
        SFFM,    // Single Frequency FM
        PWL,     // Piecewise Linear
        PWLFile, // PWL from file
        Noise,   // Random Noise
        AC = Sine // Legacy compatibility
    };

    VoltageSourceItem(QPointF pos = QPointF(), const QString& value = "5V", SourceType type = DC, QGraphicsItem* parent = nullptr);

    // SchematicItem interface
    QString itemTypeName() const override;
    ItemType itemType() const override { return SchematicItem::VoltageSourceType; }
    QString referencePrefix() const override { return "V"; }
    void rebuildPrimitives() override;
    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

    void onInteractiveDoubleClick(const QPointF& pos) override;

    // Connectivity
    QList<QPointF> connectionPoints() const override;
    QList<PinElectricalType> pinElectricalTypes() const override { return { PowerOutputPin, PowerOutputPin }; }

    // Properties
    QString value() const override { return m_value; }
    void setValue(const QString& val) override;

    SourceType sourceType() const { return m_sourceType; }
    void setSourceType(SourceType type);

    double dcVoltage() const { return m_dcVoltage; }
    void setDcVoltage(double v) { m_dcVoltage = v; updateValue(); update(); }

    // Sine Params
    double sineAmplitude() const { return m_sineAmplitude; }
    void setSineAmplitude(double v) { m_sineAmplitude = v; updateValue(); update(); }
    double sineFrequency() const { return m_sineFrequency; }
    void setSineFrequency(double f) { m_sineFrequency = f; updateValue(); update(); }
    double sineOffset() const { return m_sineOffset; }
    void setSineOffset(double o) { m_sineOffset = o; updateValue(); update(); }
    double sineDelay() const { return m_sineDelay; }
    void setSineDelay(double d) { m_sineDelay = d; updateValue(); update(); }
    double sineTheta() const { return m_sineTheta; }
    void setSineTheta(double t) { m_sineTheta = t; updateValue(); update(); }
    double sinePhi() const { return m_sinePhi; }
    void setSinePhi(double p) { m_sinePhi = p; updateValue(); update(); }
    int sineNcycles() const { return m_sineNcycles; }
    void setSineNcycles(int n) { m_sineNcycles = n; updateValue(); update(); }

    // Pulse Params
    double pulseV1() const { return m_pulseV1; }
    void setPulseV1(double v) { m_pulseV1 = v; updateValue(); update(); }
    double pulseV2() const { return m_pulseV2; }
    void setPulseV2(double v) { m_pulseV2 = v; updateValue(); update(); }
    double pulseDelay() const { return m_pulseDelay; }
    void setPulseDelay(double d) { m_pulseDelay = d; updateValue(); update(); }
    double pulseRise() const { return m_pulseRise; }
    void setPulseRise(double r) { m_pulseRise = r; updateValue(); update(); }
    double pulseFall() const { return m_pulseFall; }
    void setPulseFall(double f) { m_pulseFall = f; updateValue(); update(); }
    double pulseWidth() const { return m_pulseWidth; }
    void setPulseWidth(double w) { m_pulseWidth = w; updateValue(); update(); }
    double pulsePeriod() const { return m_pulsePeriod; }
    void setPulsePeriod(double p) { m_pulsePeriod = p; updateValue(); update(); }
    int pulseNcycles() const { return m_pulseNcycles; }
    void setPulseNcycles(int n) { m_pulseNcycles = n; updateValue(); update(); }

    // EXP Params
    double expV1() const { return m_expV1; }
    void setExpV1(double v) { m_expV1 = v; updateValue(); update(); }
    double expV2() const { return m_expV2; }
    void setExpV2(double v) { m_expV2 = v; updateValue(); update(); }
    double expTd1() const { return m_expTd1; }
    void setExpTd1(double t) { m_expTd1 = t; updateValue(); update(); }
    double expTau1() const { return m_expTau1; }
    void setExpTau1(double t) { m_expTau1 = t; updateValue(); update(); }
    double expTd2() const { return m_expTd2; }
    void setExpTd2(double t) { m_expTd2 = t; updateValue(); update(); }
    double expTau2() const { return m_expTau2; }
    void setExpTau2(double t) { m_expTau2 = t; updateValue(); update(); }

    // SFFM Params
    double sffmOff() const { return m_sffmOff; }
    void setSffmOff(double v) { m_sffmOff = v; updateValue(); update(); }
    double sffmAmplit() const { return m_sffmAmplit; }
    void setSffmAmplit(double v) { m_sffmAmplit = v; updateValue(); update(); }
    double sffmCarrier() const { return m_sffmCarrier; }
    void setSffmCarrier(double f) { m_sffmCarrier = f; updateValue(); update(); }
    double sffmModIndex() const { return m_sffmModIndex; }
    void setSffmModIndex(double i) { m_sffmModIndex = i; updateValue(); update(); }
    double sffmSignalFreq() const { return m_sffmSignalFreq; }
    void setSffmSignalFreq(double f) { m_sffmSignalFreq = f; updateValue(); update(); }

    // PWL Params
    QString pwlPoints() const { return m_pwlPoints; }
    void setPwlPoints(const QString& p) { m_pwlPoints = p; updateValue(); update(); }
    QString pwlFile() const { return m_pwlFile; }
    void setPwlFile(const QString& f) { m_pwlFile = f; updateValue(); update(); }

    // AC Analysis
    double acAmplitude() const { return m_acAmplitude; }
    void setAcAmplitude(double v) { m_acAmplitude = v; updateValue(); update(); }
    double acPhase() const { return m_acPhase; }
    void setAcPhase(double p) { m_acPhase = p; updateValue(); update(); }

    // Parasitics
    double seriesResistance() const { return m_seriesResistance; }
    void setSeriesResistance(double r) { m_seriesResistance = r; updateValue(); update(); }
    double parallelCapacitance() const { return m_parallelCapacitance; }
    void setParallelCapacitance(double c) { m_parallelCapacitance = c; updateValue(); update(); }

    // Visibility Flags
    bool isFunctionVisible() const { return m_showFunction; }
    void setFunctionVisible(bool v) { m_showFunction = v; update(); }
    bool isDcVisible() const { return m_showDc; }
    void setDcVisible(bool v) { m_showDc = v; update(); }
    bool isAcVisible() const { return m_showAc; }
    void setAcVisible(bool v) { m_showAc = v; update(); }
    bool isParasiticVisible() const { return m_showParasitic; }
    void setParasiticVisible(bool v) { m_showParasitic = v; update(); }

    // QGraphicsItem interface
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
    void updateValue();

    SourceType m_sourceType;
    double m_dcVoltage;
    
    // Sine
    double m_sineAmplitude;
    double m_sineFrequency;
    double m_sineOffset;
    double m_sineDelay;
    double m_sineTheta;
    double m_sinePhi;
    int m_sineNcycles;
    
    // Pulse
    double m_pulseV1;
    double m_pulseV2;
    double m_pulseDelay;
    double m_pulseRise;
    double m_pulseFall;
    double m_pulseWidth;
    double m_pulsePeriod;
    int m_pulseNcycles;

    // EXP
    double m_expV1;
    double m_expV2;
    double m_expTd1;
    double m_expTau1;
    double m_expTd2;
    double m_expTau2;

    // SFFM
    double m_sffmOff;
    double m_sffmAmplit;
    double m_sffmCarrier;
    double m_sffmModIndex;
    double m_sffmSignalFreq;

    // PWL
    QString m_pwlPoints;
    QString m_pwlFile;

    // AC
    double m_acAmplitude;
    double m_acPhase;

    // Parasitics
    double m_seriesResistance;
    double m_parallelCapacitance;

    // Visibility
    bool m_showFunction;
    bool m_showDc;
    bool m_showAc;
    bool m_showParasitic;

    QPen m_pen;
    QBrush m_brush;
    std::vector<std::unique_ptr<SchematicPrimitive>> m_primitives;
};

#endif // VOLTAGESOURCEITEM_H
