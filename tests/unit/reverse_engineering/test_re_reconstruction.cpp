#include <QtTest>
#include "../../../reverse_engineering/net_graph_reconstructor.h"
#include "../../../reverse_engineering/candidate_extractor.h"
#include "../../../reverse_engineering/normalized_parser.h"
#include "../../../reverse_engineering/drill_parser.h"
#include "../../../reverse_engineering/re_types.h"

using namespace VioraEDA::ReverseEngineering;

class TestREReconstruction : public QObject {
    Q_OBJECT
private slots:
    void testFullPipelineStack() {
        // 1. Setup Mock Workspace
        QList<NormalizedLayer*> layers;
        
        Session::FileEntry fcu; fcu.path = "test-files/mm-F_Cu.gbr"; fcu.detectedFormat = "Gerber"; fcu.layerRole = "TopCopper";
        Session::FileEntry bcu; bcu.path = "test-files/mm-B_Cu.gbr"; bcu.detectedFormat = "Gerber"; bcu.layerRole = "BottomCopper";
        
        layers << NormalizedParser::parseFile(fcu);
        layers << NormalizedParser::parseFile(bcu);
        
        QList<DrillHole> drills = DrillParser::parse("test-files/mm-PTH.drl");
        
        // 2. Extract Candidates
        CandidateExtractor extractor;
        QList<PadCandidate> pads;
        QList<ViaCandidate> vias;
        extractor.extract(layers, drills, pads, vias);
        
        // Sanity check: mm fixture should have some pads/vias
        QVERIFY(pads.size() > 0);
        
        // 3. Reconstruct Nets
        NetGraphReconstructor reconstructor;
        QList<NetCandidate> nets;
        reconstructor.reconstruct(layers, pads, vias, nets);
        
        QVERIFY(!nets.isEmpty());
        
        // Clean up
        qDeleteAll(layers);
    }
};

QTEST_MAIN(TestREReconstruction)
#include "test_re_reconstruction.moc"
