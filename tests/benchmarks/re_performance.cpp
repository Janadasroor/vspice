#include <QtTest>
#include <QElapsedTimer>
#include "../../reverse_engineering/net_graph_reconstructor.h"
#include "../../reverse_engineering/candidate_extractor.h"
#include "../../reverse_engineering/normalized_parser.h"
#include "../../reverse_engineering/drill_parser.h"
#include "../../reverse_engineering/component_clustering_engine.h"
#include "../../reverse_engineering/footprint_matcher.h"
#include "../../reverse_engineering/schematic_exporter.h"
#include "eco_types.h"
#include "../../footprints/footprint_library.h"

using namespace VioraEDA::ReverseEngineering;

class BenchmarkRE : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void benchmarkFullPipeline() {
        // Ensure library is ready
        auto& manager = FootprintLibraryManager::instance();
        manager.initialize();
        
        // Inject a mock resistor footprint for the test
        FootprintDefinition rConn("Connector_2Pin_Test");
        rConn.addPrimitive(FootprintPrimitive::createPad(QPointF(-2.54, 0), "1", "Rect", QSizeF(2, 2)));
        rConn.addPrimitive(FootprintPrimitive::createPad(QPointF(2.54, 0), "2", "Rect", QSizeF(2, 2)));
        
        if (manager.libraries().isEmpty()) {
             manager.createLibrary("TestLib");
        }
        manager.libraries().first()->addFootprint(rConn);

        auto libs = manager.libraries();

        QElapsedTimer timer;
        timer.start();

        // 1. Parsing
        Session::FileEntry fcu; fcu.path = "test-files/mm-F_Cu.gbr"; fcu.detectedFormat = "Gerber"; fcu.layerRole = "TopCopper";
        Session::FileEntry bcu; bcu.path = "test-files/mm-B_Cu.gbr"; bcu.detectedFormat = "Gerber"; bcu.layerRole = "BottomCopper";
        
        QList<NormalizedLayer*> layers;
        layers << NormalizedParser::parseFile(fcu);
        layers << NormalizedParser::parseFile(bcu);
        QList<DrillHole> drills = DrillParser::parse("test-files/mm-PTH.drl");

        qint64 parseTime = timer.elapsed();

        // 2. Extraction
        QList<PadCandidate> pads;
        QList<ViaCandidate> vias;
        CandidateExtractor::extract(layers, drills, pads, vias);

        qint64 extractTime = timer.elapsed() - parseTime;

        // 3. Connectivity
        QList<NetCandidate> nets;
        NetGraphReconstructor::reconstruct(layers, pads, vias, nets);

        qint64 connectTime = timer.elapsed() - extractTime - parseTime;

        // 4. Clustering & Matching
        QList<PlacementData> placements; // Empty for now
        QList<ComponentMetadata> bom;     // Empty for now
        QList<ComponentCluster> clusters;

        ComponentClusteringEngine::cluster(pads, placements, bom, clusters);
        FootprintMatcher::scoreClusters(clusters, pads);

        qint64 clusterTime = timer.elapsed() - connectTime - extractTime - parseTime;

        qDebug() << "RE Performance Metrics (mm-fixture):";
        qDebug() << "  Parsing:        " << parseTime << "ms";
        qDebug() << "  Extraction:     " << extractTime << "ms";
        qDebug() << "  Connectivity:   " << connectTime << "ms";
        qDebug() << "  Match/Cluster:  " << clusterTime << "ms";
        qDebug() << "  Total:          " << timer.elapsed() << "ms";

        qDebug() << "RE Matching Results:";
        for (const auto& c : clusters) {
            QString padsStr;
            for (int idx : c.padIndices) {
                QPointF rel = pads[idx].position - c.position;
                padsStr += QString("(%1,%2) ").arg(rel.x()).arg(rel.y());
            }
            qDebug() << "  Cluster at" << c.position << "Matched:" << c.footprint << "Conf:" << c.confidence << "Pads:" << padsStr;
        }

        // 5. Schematic Export Verification
        QJsonObject sch = SchematicExporter::exportToSchematic(clusters, nets, "BenchmarkBoard");
        qDebug() << "Exported Schematic Items:" << sch["items"].toArray().size();
        
        QVERIFY(!sch.isEmpty());
        QVERIFY(sch.contains("items"));
        QVERIFY(sch["items"].toArray().size() >= clusters.size()); // At least components + labels

        // 6. ECO Package Verification (Sync Logic)
        ECOPackage ecoPkg;
        for (const auto& cluster : clusters) {
            ECOComponent comp;
            comp.reference = cluster.refDes;
            comp.footprint = cluster.footprint;
            comp.value = cluster.value;
            ecoPkg.components.append(comp);
        }
        QVERIFY(ecoPkg.components.size() == clusters.size());
        if (!clusters.isEmpty()) {
            QCOMPARE(ecoPkg.components.first().reference, clusters.first().refDes);
        }

        qDeleteAll(layers);
    }
};

QTEST_MAIN(BenchmarkRE)
#include "re_performance.moc"
