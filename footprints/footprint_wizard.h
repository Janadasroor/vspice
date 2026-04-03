#ifndef FOOTPRINT_WIZARD_H
#define FOOTPRINT_WIZARD_H

#include <QString>
#include <QList>
#include <QMap>
#include <QSizeF>
#include <QPointF>
#include "models/footprint_definition.h"
#include "models/footprint_primitive.h"

class FootprintWizard {
public:
    struct WizardParams {
        QString packageType;
        int pinCount = 0;
        double pitch = 2.54;
        double padWidth = 1.0;
        double padHeight = 1.0;
        double bodyWidth = 0;
        double bodyHeight = 0;
        double rowSpan = 0;
        double drillSize = 0;
        double silkscreenWidth = 0;
        double courtyardExtra = 0.5;
        QString classification;
        QString category;
    };

    static Flux::Model::FootprintDefinition generate(const WizardParams& params);
    static WizardParams getPredefined(const QString& packageName);
    static QStringList getSupportedPackages();
    static QString getCategory(const QString& packageName);

private:
    static Flux::Model::FootprintDefinition generateDIP(const WizardParams& p);
    static Flux::Model::FootprintDefinition generateSOIC(const WizardParams& p);
    static Flux::Model::FootprintDefinition generateQFP(const WizardParams& p);
    static Flux::Model::FootprintDefinition generateQFN(const WizardParams& p);
    static Flux::Model::FootprintDefinition generateBGA(const WizardParams& p);
    static Flux::Model::FootprintDefinition generateSOT(const WizardParams& p);
    static Flux::Model::FootprintDefinition generatePassiveSMD(const WizardParams& p);
    static Flux::Model::FootprintDefinition generatePassiveTHT(const WizardParams& p);
    static Flux::Model::FootprintDefinition generateTO(const WizardParams& p);

    static void addSilkscreenOutline(Flux::Model::FootprintDefinition& fp, QRectF rect, double lineWidth = 0.15);
    static void addCourtyard(Flux::Model::FootprintDefinition& fp, QRectF rect, double lineWidth = 0.05);
    static void addReferenceText(Flux::Model::FootprintDefinition& fp, const QString& refDes, QPointF pos, double height = 1.0);
    static void addValueText(Flux::Model::FootprintDefinition& fp, const QString& value, QPointF pos, double height = 1.0);
    static void addPin1Marker(Flux::Model::FootprintDefinition& fp, QPointF pos, double size = 0.3);
    static Flux::Model::FootprintPrimitive::Layer topCopper();
    static Flux::Model::FootprintPrimitive::Layer bottomCopper();
    static Flux::Model::FootprintPrimitive::Layer topSilkscreen();
    static Flux::Model::FootprintPrimitive::Layer topCourtyard();
    static Flux::Model::FootprintPrimitive::Layer topFabrication();
};

#endif
