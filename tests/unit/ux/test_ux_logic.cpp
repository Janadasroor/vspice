#include <QtTest>
#include "../../../schematic/editor/schematic_menu_registry.h"
#include "../../../schematic/editor/schematic_view.h"
#include "../../../schematic/items/generic_component_item.h"
#include "../../../schematic/items/resistor_item.h"
#include "../../../schematic/items/transistor_item.h"
#include "../../../schematic/items/wire_item.h"
#include "../../../schematic/analysis/schematic_connectivity.h"
#include "../../../schematic/tools/schematic_tool_registry_builtin.h"
#include "../../../schematic/dialogs/smart_properties_dialog.h"
#include "../../../core/config_manager.h"
#include "../../../symbols/models/symbol_definition.h"
#include "../../../symbols/models/symbol_primitive.h"
#include <cmath>
#include <QRegularExpression>
#include <QGraphicsScene>

namespace {

class TestSchematicView : public SchematicView {
public:
    using SchematicView::mouseMoveEvent;
    using SchematicView::mousePressEvent;
    using SchematicView::mouseReleaseEvent;
};

class TestObstacleItem : public SchematicItem {
public:
    explicit TestObstacleItem(const QRectF& rect, QGraphicsItem* parent = nullptr)
        : SchematicItem(parent), m_rect(rect) {}

    QString itemTypeName() const override { return "Obstacle"; }
    ItemType itemType() const override { return SchematicItem::ComponentType; }
    QList<QPointF> connectionPoints() const override { return {}; }
    SchematicItem* clone() const override { return new TestObstacleItem(m_rect); }

    QRectF boundingRect() const override { return m_rect; }
    void paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*) override {}

private:
    QRectF m_rect;
};

QPoint sceneToView(const SchematicView& view, const QPointF& scenePos) {
    return view.mapFromScene(scenePos);
}

void sendMousePress(TestSchematicView& view, const QPoint& pos, Qt::MouseButton button = Qt::LeftButton) {
    QMouseEvent ev(QEvent::MouseButtonPress,
                   pos,
                   view.mapToGlobal(pos),
                   button,
                   button,
                   Qt::NoModifier);
    view.mousePressEvent(&ev);
}

void sendMouseMoveWithLeftButton(TestSchematicView& view, const QPoint& pos) {
    QMouseEvent ev(QEvent::MouseMove,
                   pos,
                   view.mapToGlobal(pos),
                   Qt::NoButton,
                   Qt::LeftButton,
                   Qt::NoModifier);
    view.mouseMoveEvent(&ev);
}

void sendMouseRelease(TestSchematicView& view, const QPoint& pos, Qt::MouseButton button = Qt::LeftButton) {
    QMouseEvent ev(QEvent::MouseButtonRelease,
                   pos,
                   view.mapToGlobal(pos),
                   button,
                   Qt::NoButton,
                   Qt::NoModifier);
    view.mouseReleaseEvent(&ev);
}

bool near(const QPointF& a, const QPointF& b, qreal eps = 0.5) {
    return QLineF(a, b).length() <= eps;
}

bool alignedToGrid(qreal value, qreal grid = 15.0, qreal eps = 0.01) {
    const qreal rem = std::fmod(std::abs(value), grid);
    return rem <= eps || std::abs(rem - grid) <= eps;
}

bool pointsGridAligned(const QList<QPointF>& points, qreal grid = 15.0, qreal eps = 0.01) {
    for (const QPointF& p : points) {
        if (!alignedToGrid(p.x(), grid, eps) || !alignedToGrid(p.y(), grid, eps)) return false;
    }
    return true;
}

bool wireSegmentsOrthogonal(const WireItem* wire, qreal tol = 1.0) {
    if (!wire) return false;
    const QList<QPointF> pts = wire->points();
    if (pts.size() < 2) return true;
    for (int i = 0; i < pts.size() - 1; ++i) {
        const QPointF a = pts[i];
        const QPointF b = pts[i + 1];
        const qreal dx = std::abs(a.x() - b.x());
        const qreal dy = std::abs(a.y() - b.y());
        if (dx >= tol && dy >= tol) return false;
    }
    return true;
}

bool containsPointNear(const QList<QPointF>& points, const QPointF& target, qreal eps = 0.5) {
    for (const QPointF& p : points) {
        if (QLineF(p, target).length() <= eps) return true;
    }
    return false;
}

} // namespace

class TestUXLogic : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // Initialize the registry
        SchematicMenuRegistry::instance().initializeDefaultActions();
        SchematicToolRegistryBuiltIn::registerBuiltInTools();
    }

    void testMenuRegistry_SingleSelection() {
        ResistorItem resistor;
        QList<SchematicItem*> selection = { &resistor };
        
        auto actions = SchematicMenuRegistry::instance().getActions(selection);
        
        bool hasRotate = false;
        bool hasFlip = false;
        for (const auto& a : actions) {
            if (a.label.contains("Rotate")) hasRotate = true;
            if (a.label.contains("Flip")) hasFlip = true;
        }
        
        QVERIFY(hasRotate);
        QVERIFY(hasFlip);
    }

    void testMenuRegistry_MixedSelection() {
        ResistorItem resistor;
        WireItem wire;
        QList<SchematicItem*> selection = { &resistor, &wire };
        
        auto actions = SchematicMenuRegistry::instance().getActions(selection);
        
        // standard edit actions should be there (Copy, Cut etc)
        bool hasDelete = false;
        for (const auto& a : actions) {
            if (a.label == "Delete") hasDelete = true;
        }
        QVERIFY(hasDelete);
        
        // "Edit Net Label" should NOT be there for mixed selection
        bool hasEditNet = false;
        for (const auto& a : actions) {
            if (a.label.contains("Net Label")) hasEditNet = true;
        }
        QVERIFY(!hasEditNet);
    }

    void testEngineeringNotationRegex() {
        // Testing the regex used in SmartPropertiesDialog::validateAll
        QRegularExpression re(R"(^([\-+]?\d*\.?\d+)([kMGTunpfμ]?[ΩFHV]?)$)");
        
        QVERIFY(re.match("10k").hasMatch());
        QVERIFY(re.match("4.7u").hasMatch());
        QVERIFY(re.match("100nF").hasMatch());
        QVERIFY(re.match("1MΩ").hasMatch());
        QVERIFY(re.match("22").hasMatch());
        QVERIFY(re.match("-5.5V").hasMatch());
        
        QVERIFY(!re.match("abc").hasMatch());
        QVERIFY(!re.match("10 k").hasMatch()); // No spaces
        QVERIFY(!re.match("10xx").hasMatch());
    }

    void testDoubleClickRouting_PrioritizesLabels() {
        // This test simulates the logic we added to SchematicView::mouseDoubleClickEvent
        ResistorItem resistor;
        resistor.createLabels(QPointF(-20, -20), QPointF(-20, 20));
        
        SchematicItem* refLabel = nullptr;
        for (auto* child : resistor.childItems()) {
            if (auto* s = dynamic_cast<SchematicItem*>(child)) {
                if (s->itemType() == SchematicItem::LabelType) {
                    refLabel = s;
                    break;
                }
            }
        }
        
        QVERIFY(refLabel != nullptr);
        QVERIFY(refLabel->isSubItem());
        
        // Simulating the "bubbling" logic:
        auto getTarget = [](QGraphicsItem* item) -> SchematicItem* {
            SchematicItem* target = nullptr;
            QGraphicsItem* curr = item;
            while (curr) {
                if (auto* sItem = dynamic_cast<SchematicItem*>(curr)) {
                    if (sItem->itemType() == SchematicItem::LabelType) return sItem;
                    if (!sItem->isSubItem()) return sItem;
                    if (!target) target = sItem;
                }
                curr = curr->parentItem();
            }
            return target;
        };
        
        QCOMPARE(getTarget(refLabel), refLabel);
        QCOMPARE(getTarget(&resistor), &resistor);
    }

    void testSelectDrag_ConnectedWireChainRemainsAttachedRealtime() {
        ConfigManager::instance().setRealtimeWireUpdateEnabled(true);

        QGraphicsScene scene;
        TestSchematicView view;
        view.resize(900, 700);
        view.setScene(&scene);
        view.setCurrentTool("Select");

        auto* resistor = new ResistorItem(QPointF(150.0, 150.0), "10k", ResistorItem::US);
        scene.addItem(resistor);

        const QPointF leftPin = resistor->mapToScene(resistor->connectionPoints().first()); // (90, 150)
        const QPointF junction = leftPin + QPointF(-30.0, 0.0); // (60, 150)

        auto* stubWire = new WireItem();
        stubWire->setPoints({leftPin, junction});
        scene.addItem(stubWire);

        auto* trunkWire = new WireItem();
        trunkWire->setPoints({
            junction,
            junction + QPointF(0.0, 60.0),
            junction + QPointF(-60.0, 60.0)
        });
        scene.addItem(trunkWire);

        // Drag resistor down so the stub's shared junction must also move.
        const QPoint pressPos = sceneToView(view, resistor->scenePos());
        const QPoint releasePos = sceneToView(view, resistor->scenePos() + QPointF(0.0, 30.0));

        sendMousePress(view, pressPos);
        sendMouseMoveWithLeftButton(view, releasePos);
        sendMouseRelease(view, releasePos);

        const QPointF pinAfter = resistor->mapToScene(resistor->connectionPoints().first());
        const QPointF stubStartAfter = stubWire->mapToScene(stubWire->points().first());
        const QPointF stubJunctionAfter = stubWire->mapToScene(stubWire->points().last());
        const QPointF trunkStartAfter = trunkWire->mapToScene(trunkWire->points().first());

        QVERIFY2(near(stubStartAfter, pinAfter), "Stub wire must stay attached to dragged resistor pin.");
        QVERIFY2(near(trunkStartAfter, stubJunctionAfter), "Connected trunk wire endpoint must follow moved stub junction.");
    }

    void testSelectDrag_ResistorKeepsNearSegmentJunctionAttached() {
        ConfigManager::instance().setRealtimeWireUpdateEnabled(true);

        QGraphicsScene scene;
        TestSchematicView view;
        view.resize(900, 700);
        view.setScene(&scene);
        view.setCurrentTool("Select");

        auto* resistor = new ResistorItem(QPointF(220.0, 220.0), "10k", ResistorItem::US);
        scene.addItem(resistor);

        const QPointF leftPin = resistor->mapToScene(resistor->connectionPoints().first());
        const QPointF junction = leftPin + QPointF(-30.0, 0.0);

        // Intentionally keep a tiny offset to emulate "looks connected" legacy drawings.
        const qreal trunkOffsetY = 1.8;
        auto* trunkWire = new WireItem();
        trunkWire->setPoints({
            junction + QPointF(-60.0, trunkOffsetY),
            junction + QPointF(60.0, trunkOffsetY)
        });
        scene.addItem(trunkWire);

        auto* stubWire = new WireItem();
        stubWire->setPoints({leftPin, junction});
        scene.addItem(stubWire);

        const QPoint pressPos = sceneToView(view, resistor->scenePos());
        const QPoint releasePos = sceneToView(view, resistor->scenePos() + QPointF(0.0, 30.0));

        sendMousePress(view, pressPos);
        sendMouseMoveWithLeftButton(view, releasePos);
        sendMouseRelease(view, releasePos);

        const QPointF pinAfter = resistor->mapToScene(resistor->connectionPoints().first());
        const QPointF stubStartAfter = stubWire->mapToScene(stubWire->points().first());
        const QPointF stubJunctionAfter = stubWire->mapToScene(stubWire->points().last());
        const QList<QPointF> trunkPts = trunkWire->points();
        const QPointF trunkA = trunkWire->mapToScene(trunkPts[0]);
        const QPointF trunkB = trunkWire->mapToScene(trunkPts[1]);

        const QPointF vec = trunkB - trunkA;
        const qreal lenSq = vec.x() * vec.x() + vec.y() * vec.y();
        qreal u = 0.0;
        if (lenSq > 0.0) {
            u = ((stubJunctionAfter.x() - trunkA.x()) * vec.x() +
                 (stubJunctionAfter.y() - trunkA.y()) * vec.y()) / lenSq;
            if (u < 0.0) u = 0.0;
            if (u > 1.0) u = 1.0;
        }
        const QPointF projected = trunkA + u * vec;

        QVERIFY2(near(stubStartAfter, pinAfter), "Stub start must follow dragged resistor pin.");
        QVERIFY2(
            near(stubJunctionAfter, projected, 2.0),
            "Stub junction must stay attached to nearby trunk segment when dragging resistor.");
    }

    void testSelect_ClickOnComponentPrimitiveEdgeSelectsOwner() {
        QGraphicsScene scene;
        TestSchematicView view;
        view.resize(900, 700);
        view.setScene(&scene);
        view.setCurrentTool("Select");

        Flux::Model::SymbolDefinition symbol("EdgePickSymbol");
        symbol.setReferencePrefix("U");
        symbol.addPrimitive(Flux::Model::SymbolPrimitive::createLine(QPointF(-50.0, 0.0), QPointF(50.0, 0.0)));
        symbol.addPrimitive(Flux::Model::SymbolPrimitive::createPin(QPointF(50.0, 0.0), 1, "OUT", "Right", 20.0));

        auto* comp = new GenericComponentItem(symbol);
        comp->setPos(QPointF(260.0, 200.0));
        scene.addItem(comp);

        // Click exactly on the drawn primitive line (not the empty body area).
        const QPoint clickPos = sceneToView(view, comp->mapToScene(QPointF(0.0, 0.0)));
        sendMousePress(view, clickPos);
        sendMouseRelease(view, clickPos);

        QVERIFY2(comp->isSelected(), "Clicking on symbol primitive edge should select the owning component.");
    }

    void testSelect_ClickOnConnectedResistorPinPrefersComponentCapture() {
        QGraphicsScene scene;
        TestSchematicView view;
        view.resize(900, 700);
        view.setScene(&scene);
        view.setCurrentTool("Select");

        auto* resistor = new ResistorItem(QPointF(150.0, 150.0), "10k", ResistorItem::US);
        scene.addItem(resistor);

        const QPointF leftPin = resistor->mapToScene(resistor->connectionPoints().first());
        auto* wire = new WireItem();
        wire->setPoints({leftPin, leftPin + QPointF(-45.0, 0.0)});
        scene.addItem(wire);

        const QPoint clickPos = sceneToView(view, leftPin);
        sendMousePress(view, clickPos);
        sendMouseRelease(view, clickPos);

        QVERIFY2(resistor->isSelected(), "Clicking a connected resistor pin should capture/select resistor.");
        QVERIFY2(!wire->isSelected(), "Connected wire should not steal click selection from resistor pin.");
        QVERIFY2(view.currentTool() && view.currentTool()->name() == "Select",
                 "Select tool should stay active when clicking connected pin.");
    }

    void testSelect_DragTransistorKeepsTJunctionBranchesAttached() {
        ConfigManager::instance().setRealtimeWireUpdateEnabled(true);

        QGraphicsScene scene;
        TestSchematicView view;
        view.resize(1000, 700);
        view.setScene(&scene);
        view.setCurrentTool("Select");

        auto* transistor = new TransistorItem(QPointF(220.0, 220.0), "2N2222", TransistorItem::NPN);
        scene.addItem(transistor);

        // Use base pin for deterministic geometry.
        const QPointF basePin = transistor->mapToScene(transistor->connectionPoints().first());
        const QPointF elbow = basePin + QPointF(-30.0, 0.0);
        const QPointF tail = elbow + QPointF(0.0, 60.0);

        auto* baseWire = new WireItem();
        baseWire->setPoints({basePin, elbow, tail});
        scene.addItem(baseWire);

        // Endpoint-on-segment branch (T-junction) attached to the first base segment.
        const QPointF tJunction = basePin + QPointF(-15.0, 0.0);
        auto* branchWire = new WireItem();
        branchWire->setPoints({tJunction, tJunction + QPointF(0.0, -45.0)});
        scene.addItem(branchWire);

        const QPoint pressPos = sceneToView(view, transistor->mapToScene(QPointF(0.0, 0.0)));
        const QPoint releasePos = sceneToView(view, transistor->mapToScene(QPointF(0.0, 30.0)));

        sendMousePress(view, pressPos);
        sendMouseMoveWithLeftButton(view, releasePos);
        sendMouseRelease(view, releasePos);

        const QPointF branchStartAfter = branchWire->mapToScene(branchWire->points().first());
        const QList<QPointF> movedBasePts = baseWire->points();
        const QPointF segA = baseWire->mapToScene(movedBasePts[0]);
        const QPointF segB = baseWire->mapToScene(movedBasePts[1]);
        const QPointF segVec = segB - segA;
        const qreal lenSq = segVec.x() * segVec.x() + segVec.y() * segVec.y();
        qreal u = 0.0;
        if (lenSq > 0.0) {
            u = ((branchStartAfter.x() - segA.x()) * segVec.x() +
                 (branchStartAfter.y() - segA.y()) * segVec.y()) / lenSq;
            if (u < 0.0) u = 0.0;
            if (u > 1.0) u = 1.0;
        }
        const QPointF projected = segA + u * segVec;

        QVERIFY2(
            near(branchStartAfter, projected),
            "Dragging transistor must keep branch endpoint on moved base-wire segment (T-junction).");
        QVERIFY2(
            QLineF(branchStartAfter, tJunction).length() > 1.0,
            "Dragging transistor should move the branch T-junction endpoint.");
    }

    void testSelectDrag_SequentialMovesKeepWireEndpointsAttached() {
        ConfigManager::instance().setRealtimeWireUpdateEnabled(true);

        QGraphicsScene scene;
        TestSchematicView view;
        view.resize(1000, 700);
        view.setScene(&scene);
        view.setCurrentTool("Select");

        auto* resistor = new ResistorItem(QPointF(225.0, 225.0), "10k", ResistorItem::US);
        scene.addItem(resistor);

        const QPointF leftPin = resistor->mapToScene(resistor->connectionPoints().first());
        const QPointF rightPin = resistor->mapToScene(resistor->connectionPoints().last());
        const QPointF leftJunction = leftPin + QPointF(-45.0, 0.0);
        const QPointF rightJunction = rightPin + QPointF(45.0, 0.0);

        auto* leftStub = new WireItem();
        leftStub->setPoints({leftPin, leftJunction});
        scene.addItem(leftStub);

        auto* leftTrunk = new WireItem();
        leftTrunk->setPoints({leftJunction, leftJunction + QPointF(0.0, 60.0)});
        scene.addItem(leftTrunk);

        auto* rightStub = new WireItem();
        rightStub->setPoints({rightPin, rightJunction});
        scene.addItem(rightStub);

        auto* rightTrunk = new WireItem();
        rightTrunk->setPoints({rightJunction, rightJunction + QPointF(0.0, 60.0)});
        scene.addItem(rightTrunk);

        auto assertAttached = [&]() {
            const QPointF leftPinAfter = resistor->mapToScene(resistor->connectionPoints().first());
            const QPointF rightPinAfter = resistor->mapToScene(resistor->connectionPoints().last());

            const QPointF leftStubStart = leftStub->mapToScene(leftStub->points().first());
            const QPointF leftStubEnd = leftStub->mapToScene(leftStub->points().last());
            const QPointF leftTrunkStart = leftTrunk->mapToScene(leftTrunk->points().first());

            const QPointF rightStubStart = rightStub->mapToScene(rightStub->points().first());
            const QPointF rightStubEnd = rightStub->mapToScene(rightStub->points().last());
            const QPointF rightTrunkStart = rightTrunk->mapToScene(rightTrunk->points().first());

            QVERIFY2(near(leftStubStart, leftPinAfter), "Left stub must remain attached to left pin.");
            QVERIFY2(near(rightStubStart, rightPinAfter), "Right stub must remain attached to right pin.");
            QVERIFY2(near(leftStubEnd, leftTrunkStart), "Left stub junction must remain on left trunk.");
            QVERIFY2(near(rightStubEnd, rightTrunkStart), "Right stub junction must remain on right trunk.");
            QVERIFY2(wireSegmentsOrthogonal(leftStub), "Left stub must remain orthogonal after drag.");
            QVERIFY2(wireSegmentsOrthogonal(rightStub), "Right stub must remain orthogonal after drag.");
            QVERIFY2(leftStub->points().size() <= 3, "Left stub should not accumulate tail segments.");
            QVERIFY2(rightStub->points().size() <= 3, "Right stub should not accumulate tail segments.");
        };

        auto dragBy = [&](const QPointF& delta) {
            const QPoint pressPos = sceneToView(view, resistor->scenePos());
            const QPoint releasePos = sceneToView(view, resistor->scenePos() + delta);
            sendMousePress(view, pressPos);
            sendMouseMoveWithLeftButton(view, releasePos);
            sendMouseRelease(view, releasePos);
        };

        // First drag up, then right.
        dragBy(QPointF(0.0, -30.0));
        assertAttached();
        dragBy(QPointF(30.0, 0.0));
        assertAttached();

        // Then drag left, then down (second sequence).
        dragBy(QPointF(-30.0, 0.0));
        assertAttached();
        dragBy(QPointF(0.0, 30.0));
        assertAttached();
    }

    void testSelectDrag_HorizontalMovePrefersHorizontalFirstElbow() {
        ConfigManager::instance().setRealtimeWireUpdateEnabled(true);

        QGraphicsScene scene;
        TestSchematicView view;
        view.resize(900, 700);
        view.setScene(&scene);
        view.setCurrentTool("Select");

        auto* resistor = new ResistorItem(QPointF(150.0, 150.0), "10k", ResistorItem::US);
        scene.addItem(resistor);

        const QPointF leftPin = resistor->mapToScene(resistor->connectionPoints().first());
        const QPointF junction = leftPin + QPointF(-45.0, 0.0);

        auto* verticalWire = new WireItem();
        verticalWire->setPoints({junction, leftPin});
        scene.addItem(verticalWire);

        auto* horizontalWire = new WireItem();
        horizontalWire->setPoints({junction, junction + QPointF(60.0, 0.0)});
        scene.addItem(horizontalWire);

        const QPoint pressPos = sceneToView(view, resistor->scenePos());
        const QPoint releasePos = sceneToView(view, resistor->scenePos() + QPointF(45.0, 0.0));

        sendMousePress(view, pressPos);
        sendMouseMoveWithLeftButton(view, releasePos);
        sendMouseRelease(view, releasePos);

        const QPointF newPin = resistor->mapToScene(resistor->connectionPoints().first());
        const QPointF expectedElbow(newPin.x(), junction.y());
        const QPointF undesiredElbow(junction.x(), newPin.y());

        const QList<QPointF> pts = verticalWire->points();
        QVERIFY2(pts.size() >= 3, "Dragged vertical wire should keep an elbow after horizontal move.");
        QVERIFY2(containsPointNear(pts, verticalWire->mapFromScene(expectedElbow)),
                 "Elbow should align horizontally from junction to the moved pin.");
        QVERIFY2(!containsPointNear(pts, verticalWire->mapFromScene(undesiredElbow)),
                 "Elbow should not remain at the old pin x after horizontal move.");
    }

    void testConnectivity_JunctionDotsFollowRules() {
        QGraphicsScene scene;

        auto* wireA = new WireItem();
        wireA->setPoints({QPointF(0.0, 0.0), QPointF(30.0, 0.0)});
        scene.addItem(wireA);

        auto* wireB = new WireItem();
        wireB->setPoints({QPointF(30.0, 0.0), QPointF(30.0, 30.0)});
        scene.addItem(wireB);

        // Two endpoints meeting should not produce a junction dot.
        SchematicConnectivity::updateVisualConnections(&scene);
        QVERIFY2(wireA->junctions().isEmpty(), "2-way endpoint connection must not create junction dot.");
        QVERIFY2(wireB->junctions().isEmpty(), "2-way endpoint connection must not create junction dot.");

        // Clear and create a T-junction.
        wireA->setPoints({QPointF(0.0, 0.0), QPointF(60.0, 0.0)});
        wireB->setPoints({QPointF(30.0, 0.0), QPointF(30.0, 30.0)});
        wireA->clearJunctions();
        wireB->clearJunctions();
        SchematicConnectivity::updateVisualConnections(&scene);
        const QPointF tj(30.0, 0.0);
        QVERIFY2(containsPointNear(wireA->junctions(), tj), "T-junction must add dot on trunk wire.");
        QVERIFY2(containsPointNear(wireB->junctions(), tj), "T-junction must add dot on branch endpoint.");

        // Clear and create a 3-way endpoint.
        wireA->setPoints({QPointF(0.0, 0.0), QPointF(30.0, 0.0)});
        wireB->setPoints({QPointF(0.0, 0.0), QPointF(0.0, 30.0)});
        auto* wireC = new WireItem();
        wireC->setPoints({QPointF(0.0, 0.0), QPointF(-30.0, 0.0)});
        scene.addItem(wireC);
        wireA->clearJunctions();
        wireB->clearJunctions();
        wireC->clearJunctions();
        SchematicConnectivity::updateVisualConnections(&scene);
        const QPointF threeWay(0.0, 0.0);
        QVERIFY2(containsPointNear(wireA->junctions(), threeWay), "3-way endpoint must create junction dot on wire A.");
        QVERIFY2(containsPointNear(wireB->junctions(), threeWay), "3-way endpoint must create junction dot on wire B.");
        QVERIFY2(containsPointNear(wireC->junctions(), threeWay), "3-way endpoint must create junction dot on wire C.");
    }

    void testSelectDrag_AnchoredWireBetweenComponentsStaysAttached() {
        ConfigManager::instance().setRealtimeWireUpdateEnabled(true);

        QGraphicsScene scene;
        TestSchematicView view;
        view.resize(1000, 700);
        view.setScene(&scene);
        view.setCurrentTool("Select");

        auto* leftRes = new ResistorItem(QPointF(200.0, 200.0), "10k", ResistorItem::US);
        auto* rightRes = new ResistorItem(QPointF(420.0, 200.0), "10k", ResistorItem::US);
        scene.addItem(leftRes);
        scene.addItem(rightRes);

        const QList<QPointF> leftPins = leftRes->connectionPoints();
        const QList<QPointF> rightPins = rightRes->connectionPoints();
        const QPointF leftP0 = leftRes->mapToScene(leftPins.first());
        const QPointF leftP1 = leftRes->mapToScene(leftPins.last());
        const QPointF rightP0 = rightRes->mapToScene(rightPins.first());
        const QPointF rightP1 = rightRes->mapToScene(rightPins.last());

        const QPointF leftPin = (leftP0.x() > leftP1.x()) ? leftP0 : leftP1;
        const QPointF rightPin = (rightP0.x() < rightP1.x()) ? rightP0 : rightP1;

        const QPointF busY = leftPin + QPointF(0.0, -45.0);
        auto* linkWire = new WireItem();
        linkWire->setPoints({
            leftPin,
            busY,
            QPointF(rightPin.x(), busY.y()),
            rightPin
        });
        scene.addItem(linkWire);

        auto assertAnchored = [&]() {
            const QPointF leftPinAfter = leftRes->mapToScene(leftRes->connectionPoints().first());
            const QPointF leftPinAfter2 = leftRes->mapToScene(leftRes->connectionPoints().last());
            const QPointF rightPinAfter = rightRes->mapToScene(rightRes->connectionPoints().first());
            const QPointF rightPinAfter2 = rightRes->mapToScene(rightRes->connectionPoints().last());

            const QPointF leftAnchor = (leftPinAfter.x() > leftPinAfter2.x()) ? leftPinAfter : leftPinAfter2;
            const QPointF rightAnchor = (rightPinAfter.x() < rightPinAfter2.x()) ? rightPinAfter : rightPinAfter2;

            const QList<QPointF> pts = linkWire->points();
            const QPointF startScene = linkWire->mapToScene(pts.first());
            const QPointF endScene = linkWire->mapToScene(pts.last());

            QVERIFY2(near(startScene, leftAnchor), "Link wire must stay attached to moved left resistor pin.");
            QVERIFY2(near(endScene, rightAnchor), "Link wire must stay attached to stationary right resistor pin.");
            QVERIFY2(wireSegmentsOrthogonal(linkWire), "Link wire must remain orthogonal after drag.");
        };

        auto dragBy = [&](const QPointF& delta) {
            const QPoint pressPos = sceneToView(view, leftRes->scenePos());
            const QPoint releasePos = sceneToView(view, leftRes->scenePos() + delta);
            sendMousePress(view, pressPos);
            sendMouseMoveWithLeftButton(view, releasePos);
            sendMouseRelease(view, releasePos);
        };

        dragBy(QPointF(0.0, -30.0));
        assertAnchored();
        dragBy(QPointF(30.0, 0.0));
        assertAnchored();
    }

    void testSelectDrag_RebuildsSimpleLWireBetweenComponents() {
        ConfigManager::instance().setRealtimeWireUpdateEnabled(true);

        QGraphicsScene scene;
        TestSchematicView view;
        view.resize(1000, 700);
        view.setScene(&scene);
        view.setCurrentTool("Select");

        auto* leftRes = new ResistorItem(QPointF(225.0, 225.0), "10k", ResistorItem::US);
        auto* rightRes = new ResistorItem(QPointF(405.0, 285.0), "10k", ResistorItem::US);
        scene.addItem(leftRes);
        scene.addItem(rightRes);

        const QList<QPointF> leftPins = leftRes->connectionPoints();
        const QList<QPointF> rightPins = rightRes->connectionPoints();
        const QPointF leftP0 = leftRes->mapToScene(leftPins.first());
        const QPointF leftP1 = leftRes->mapToScene(leftPins.last());
        const QPointF rightP0 = rightRes->mapToScene(rightPins.first());
        const QPointF rightP1 = rightRes->mapToScene(rightPins.last());

        const QPointF leftPin = (leftP0.x() > leftP1.x()) ? leftP0 : leftP1;
        const QPointF rightPin = (rightP0.x() < rightP1.x()) ? rightP0 : rightP1;

        // Simple L wire (3 points).
        const QPointF elbow(leftPin.x(), rightPin.y());
        auto* linkWire = new WireItem();
        linkWire->setPoints({leftPin, elbow, rightPin});
        scene.addItem(linkWire);

        auto assertShape = [&]() {
            const QList<QPointF> pts = linkWire->points();
            QVERIFY2(pts.size() <= 3, "Simple L wire should remain at most 3 points.");
            QVERIFY2(wireSegmentsOrthogonal(linkWire), "Simple L wire must remain orthogonal.");
            QVERIFY2(pointsGridAligned(pts), "Simple L wire points must stay grid aligned.");

            const QPointF leftPinAfter = leftRes->mapToScene(leftRes->connectionPoints().first());
            const QPointF leftPinAfter2 = leftRes->mapToScene(leftRes->connectionPoints().last());
            const QPointF rightPinAfter = rightRes->mapToScene(rightRes->connectionPoints().first());
            const QPointF rightPinAfter2 = rightRes->mapToScene(rightRes->connectionPoints().last());

            const QPointF leftAnchor = (leftPinAfter.x() > leftPinAfter2.x()) ? leftPinAfter : leftPinAfter2;
            const QPointF rightAnchor = (rightPinAfter.x() < rightPinAfter2.x()) ? rightPinAfter : rightPinAfter2;

            const QPointF startScene = linkWire->mapToScene(pts.first());
            const QPointF endScene = linkWire->mapToScene(pts.last());
            QVERIFY2(near(startScene, leftAnchor), "L-wire start must stay attached to moved left pin.");
            QVERIFY2(near(endScene, rightAnchor), "L-wire end must stay attached to stationary right pin.");
        };

        auto dragBy = [&](const QPointF& delta) {
            const QPoint pressPos = sceneToView(view, leftRes->scenePos());
            const QPoint releasePos = sceneToView(view, leftRes->scenePos() + delta);
            sendMousePress(view, pressPos);
            sendMouseMoveWithLeftButton(view, releasePos);
            sendMouseRelease(view, releasePos);
        };

        dragBy(QPointF(0.0, -30.0));
        assertShape();
        dragBy(QPointF(30.0, 0.0));
        assertShape();
    }

    void testSelectDrag_WirePointsRemainGridAlignedAfterMoves() {
        ConfigManager::instance().setRealtimeWireUpdateEnabled(true);

        QGraphicsScene scene;
        TestSchematicView view;
        view.resize(1000, 700);
        view.setScene(&scene);
        view.setCurrentTool("Select");

        auto* resistor = new ResistorItem(QPointF(240.0, 240.0), "10k", ResistorItem::US);
        scene.addItem(resistor);

        const QPointF leftPin = resistor->mapToScene(resistor->connectionPoints().first());
        const QPointF junction = leftPin + QPointF(-30.0, 0.0);

        auto* wire = new WireItem();
        wire->setPoints({leftPin, junction, junction + QPointF(0.0, 45.0)});
        scene.addItem(wire);

        auto dragBy = [&](const QPointF& delta) {
            const QPoint pressPos = sceneToView(view, resistor->scenePos());
            const QPoint releasePos = sceneToView(view, resistor->scenePos() + delta);
            sendMousePress(view, pressPos);
            sendMouseMoveWithLeftButton(view, releasePos);
            sendMouseRelease(view, releasePos);
        };

        dragBy(QPointF(30.0, 0.0));
        QVERIFY2(pointsGridAligned(wire->points()), "Wire points must stay grid aligned after drag.");
        dragBy(QPointF(0.0, 30.0));
        QVERIFY2(pointsGridAligned(wire->points()), "Wire points must stay grid aligned after second drag.");
    }

    void testSelectDrag_MultiSegmentWirePreservesPointCount() {
        ConfigManager::instance().setRealtimeWireUpdateEnabled(true);

        QGraphicsScene scene;
        TestSchematicView view;
        view.resize(1000, 700);
        view.setScene(&scene);
        view.setCurrentTool("Select");

        auto* resistor = new ResistorItem(QPointF(225.0, 225.0), "10k", ResistorItem::US);
        scene.addItem(resistor);

        const QPointF leftPin = resistor->mapToScene(resistor->connectionPoints().first());
        const QPointF p1 = leftPin + QPointF(-30.0, 0.0);
        const QPointF p2 = p1 + QPointF(0.0, 30.0);
        const QPointF p3 = p2 + QPointF(-30.0, 0.0);
        const QPointF p4 = p3 + QPointF(0.0, 30.0);

        auto* wire = new WireItem();
        wire->setPoints({leftPin, p1, p2, p3, p4});
        scene.addItem(wire);

        const int originalCount = wire->points().size();

        auto dragBy = [&](const QPointF& delta) {
            const QPoint pressPos = sceneToView(view, resistor->scenePos());
            const QPoint releasePos = sceneToView(view, resistor->scenePos() + delta);
            sendMousePress(view, pressPos);
            sendMouseMoveWithLeftButton(view, releasePos);
            sendMouseRelease(view, releasePos);
        };

        dragBy(QPointF(30.0, 0.0));
        QCOMPARE(wire->points().size(), originalCount);
        dragBy(QPointF(0.0, 30.0));
        QCOMPARE(wire->points().size(), originalCount);
    }

    void testSelectDrag_LWireAvoidsObstacleByFlippingElbow() {
        ConfigManager::instance().setRealtimeWireUpdateEnabled(true);

        QGraphicsScene scene;
        TestSchematicView view;
        view.resize(1200, 800);
        view.setScene(&scene);
        view.setCurrentTool("Select");

        auto* leftRes = new ResistorItem(QPointF(225.0, 225.0), "10k", ResistorItem::US);
        auto* rightRes = new ResistorItem(QPointF(405.0, 285.0), "10k", ResistorItem::US);
        scene.addItem(leftRes);
        scene.addItem(rightRes);

        // Deterministic obstacle placed on the horizontal-first path.
        auto* obstacle = new TestObstacleItem(QRectF(-10.0, -10.0, 20.0, 20.0));
        obstacle->setPos(QPointF(330.0, 225.0));
        scene.addItem(obstacle);

        const QPointF leftPin = leftRes->mapToScene(leftRes->connectionPoints().last());
        const QPointF rightPin = rightRes->mapToScene(rightRes->connectionPoints().first());

        const QPointF elbowA(rightPin.x(), leftPin.y()); // horizontal-first
        auto* wire = new WireItem();
        wire->setPoints({leftPin, elbowA, rightPin});
        scene.addItem(wire);

        const QPoint pressPos = sceneToView(view, leftRes->scenePos());
        const QPoint releasePos = sceneToView(view, leftRes->scenePos() + QPointF(15.0, 0.0));
        sendMousePress(view, pressPos);
        sendMouseMoveWithLeftButton(view, releasePos);
        sendMouseRelease(view, releasePos);

        const QList<QPointF> pts = wire->points();
        QVERIFY2(pts.size() == 3, "L wire must remain 3 points after obstacle avoid.");

        const QRectF obstacleRect = obstacle->sceneBoundingRect().adjusted(-2, -2, 2, 2);
        auto segIntersects = [](const QPointF& a, const QPointF& b, const QRectF& rect) {
            if (rect.contains(a) || rect.contains(b)) return true;
            QLineF seg(a, b);
            QPointF inter;
            const QPointF tl = rect.topLeft();
            const QPointF tr = rect.topRight();
            const QPointF br = rect.bottomRight();
            const QPointF bl = rect.bottomLeft();
            if (seg.intersects(QLineF(tl, tr), &inter) == QLineF::BoundedIntersection) return true;
            if (seg.intersects(QLineF(tr, br), &inter) == QLineF::BoundedIntersection) return true;
            if (seg.intersects(QLineF(br, bl), &inter) == QLineF::BoundedIntersection) return true;
            if (seg.intersects(QLineF(bl, tl), &inter) == QLineF::BoundedIntersection) return true;
            return false;
        };

        const QPointF startScene = wire->mapToScene(pts.first());
        const QPointF elbowScene = wire->mapToScene(pts[1]);
        const QPointF endScene = wire->mapToScene(pts.last());
        const bool collides =
            segIntersects(startScene, elbowScene, obstacleRect) ||
            segIntersects(elbowScene, endScene, obstacleRect);

        QVERIFY2(!collides, "L wire should avoid obstacle after drag.");
    }

    void testSelectDrag_LWireNudgesWhenBothPathsBlocked() {
        ConfigManager::instance().setRealtimeWireUpdateEnabled(true);

        QGraphicsScene scene;
        TestSchematicView view;
        view.resize(1200, 800);
        view.setScene(&scene);
        view.setCurrentTool("Select");

        auto* leftRes = new ResistorItem(QPointF(225.0, 225.0), "10k", ResistorItem::US);
        auto* rightRes = new ResistorItem(QPointF(405.0, 285.0), "10k", ResistorItem::US);
        scene.addItem(leftRes);
        scene.addItem(rightRes);

        // Block both L paths.
        auto* obstacleA = new TestObstacleItem(QRectF(-10.0, -10.0, 20.0, 20.0));
        obstacleA->setPos(QPointF(330.0, 225.0));
        scene.addItem(obstacleA);

        auto* obstacleB = new TestObstacleItem(QRectF(-10.0, -10.0, 20.0, 20.0));
        obstacleB->setPos(QPointF(225.0, 270.0));
        scene.addItem(obstacleB);

        const QPointF leftPin = leftRes->mapToScene(leftRes->connectionPoints().last());
        const QPointF rightPin = rightRes->mapToScene(rightRes->connectionPoints().first());

        const QPointF elbowA(rightPin.x(), leftPin.y());
        auto* wire = new WireItem();
        wire->setPoints({leftPin, elbowA, rightPin});
        scene.addItem(wire);

        const QPoint pressPos = sceneToView(view, leftRes->scenePos());
        const QPoint releasePos = sceneToView(view, leftRes->scenePos() + QPointF(15.0, 0.0));
        sendMousePress(view, pressPos);
        sendMouseMoveWithLeftButton(view, releasePos);
        sendMouseRelease(view, releasePos);

        const QList<QPointF> pts = wire->points();
        QVERIFY2(pts.size() == 3, "L wire must remain 3 points after nudge.");

        const QRectF rectA = obstacleA->sceneBoundingRect().adjusted(-2, -2, 2, 2);
        const QRectF rectB = obstacleB->sceneBoundingRect().adjusted(-2, -2, 2, 2);
        auto segIntersects = [](const QPointF& a, const QPointF& b, const QRectF& rect) {
            if (rect.contains(a) || rect.contains(b)) return true;
            QLineF seg(a, b);
            QPointF inter;
            const QPointF tl = rect.topLeft();
            const QPointF tr = rect.topRight();
            const QPointF br = rect.bottomRight();
            const QPointF bl = rect.bottomLeft();
            if (seg.intersects(QLineF(tl, tr), &inter) == QLineF::BoundedIntersection) return true;
            if (seg.intersects(QLineF(tr, br), &inter) == QLineF::BoundedIntersection) return true;
            if (seg.intersects(QLineF(br, bl), &inter) == QLineF::BoundedIntersection) return true;
            if (seg.intersects(QLineF(bl, tl), &inter) == QLineF::BoundedIntersection) return true;
            return false;
        };

        const QPointF startScene = wire->mapToScene(pts.first());
        const QPointF elbowScene = wire->mapToScene(pts[1]);
        const QPointF endScene = wire->mapToScene(pts.last());
        const bool collides =
            segIntersects(startScene, elbowScene, rectA) ||
            segIntersects(elbowScene, endScene, rectA) ||
            segIntersects(startScene, elbowScene, rectB) ||
            segIntersects(elbowScene, endScene, rectB);

        QVERIFY2(!collides, "L wire should nudge to avoid obstacles when both paths blocked.");
    }

    void testSelectDrag_CollinearChainMovesWithEndpoint() {
        ConfigManager::instance().setRealtimeWireUpdateEnabled(true);

        QGraphicsScene scene;
        TestSchematicView view;
        view.resize(1000, 700);
        view.setScene(&scene);
        view.setCurrentTool("Select");

        auto* resistor = new ResistorItem(QPointF(225.0, 225.0), "10k", ResistorItem::US);
        scene.addItem(resistor);

        const QPointF leftPin = resistor->mapToScene(resistor->connectionPoints().first());
        const QPointF p1 = leftPin + QPointF(-30.0, 0.0);
        const QPointF p2 = p1 + QPointF(-30.0, 0.0);
        const QPointF p3 = p2 + QPointF(-30.0, 0.0);

        auto* wire = new WireItem();
        wire->setPoints({leftPin, p1, p2, p3});
        scene.addItem(wire);

        const QPoint pressPos = sceneToView(view, resistor->scenePos());
        const QPoint releasePos = sceneToView(view, resistor->scenePos() + QPointF(0.0, 30.0));
        sendMousePress(view, pressPos);
        sendMouseMoveWithLeftButton(view, releasePos);
        sendMouseRelease(view, releasePos);

        const QList<QPointF> pts = wire->points();
        QVERIFY2(pts.size() == 4, "Chain wire should preserve point count.");
        const qreal y0 = pts[0].y();
        QVERIFY2(qAbs(pts[1].y() - y0) < 0.5, "Collinear point 1 should move with endpoint.");
        QVERIFY2(qAbs(pts[2].y() - y0) < 0.5, "Collinear point 2 should move with endpoint.");
        QVERIFY2(qAbs(pts[3].y() - y0) < 0.5, "Collinear point 3 should move with endpoint.");
    }

    void testTransistor_PinsAreGridAligned() {
        TransistorItem npn(QPointF(225.0, 225.0), "2N2222", TransistorItem::NPN);
        for (const QPointF& pinLocal : npn.connectionPoints()) {
            const QPointF pinScene = npn.mapToScene(pinLocal);
            QVERIFY2(alignedToGrid(pinScene.x()), "NPN pin X must be grid aligned.");
            QVERIFY2(alignedToGrid(pinScene.y()), "NPN pin Y must be grid aligned.");
        }

        TransistorItem nmos(QPointF(225.0, 225.0), "2N7000", TransistorItem::NMOS);
        for (const QPointF& pinLocal : nmos.connectionPoints()) {
            const QPointF pinScene = nmos.mapToScene(pinLocal);
            QVERIFY2(alignedToGrid(pinScene.x()), "NMOS pin X must be grid aligned.");
            QVERIFY2(alignedToGrid(pinScene.y()), "NMOS pin Y must be grid aligned.");
        }
    }
};

QTEST_MAIN(TestUXLogic)
#include "test_ux_logic.moc"
