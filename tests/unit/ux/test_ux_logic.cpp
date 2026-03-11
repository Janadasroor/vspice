#include <QtTest>
#include "../../../schematic/editor/schematic_menu_registry.h"
#include "../../../schematic/editor/schematic_view.h"
#include "../../../schematic/items/generic_component_item.h"
#include "../../../schematic/items/resistor_item.h"
#include "../../../schematic/items/transistor_item.h"
#include "../../../schematic/items/wire_item.h"
#include "../../../schematic/tools/schematic_tool_registry_builtin.h"
#include "../../../schematic/dialogs/smart_properties_dialog.h"
#include "../../../core/config_manager.h"
#include "../../../core/assignment_validator.h"
#include "../../../symbols/models/symbol_definition.h"
#include "../../../symbols/models/symbol_primitive.h"
#include "../../../footprints/models/footprint_definition.h"
#include "../../../footprints/models/footprint_primitive.h"
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

} // namespace

class TestUXLogic : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // Initialize the registry
        SchematicMenuRegistry::instance().initializeDefaultActions();
        SchematicToolRegistryBuiltIn::registerBuiltInTools();
    }

    void testAssignmentValidator_ICRequiresExactPadCount() {
        Flux::Model::SymbolDefinition ic8("IC8");
        ic8.setReferencePrefix("U");
        for (int i = 0; i < 8; ++i) {
            ic8.addPrimitive(Flux::Model::SymbolPrimitive::createPin(QPointF(i * 2.54, 0), i + 1, QString::number(i + 1)));
        }

        Flux::Model::FootprintDefinition fp8("SOIC-8");
        for (int i = 0; i < 8; ++i) {
            fp8.addPrimitive(Flux::Model::FootprintPrimitive::createPad(QPointF(i * 1.27, 0), QString::number(i + 1)));
        }

        Flux::Model::FootprintDefinition fp7("SOIC-7");
        for (int i = 0; i < 7; ++i) {
            fp7.addPrimitive(Flux::Model::FootprintPrimitive::createPad(QPointF(i * 1.27, 0), QString::number(i + 1)));
        }

        const auto ok = Flux::Core::AssignmentValidator::validate(ic8, fp8);
        QVERIFY2(ok.valid, qPrintable(ok.message));

        const auto bad = Flux::Core::AssignmentValidator::validate(ic8, fp7);
        QVERIFY(!bad.valid);
        QCOMPARE(bad.severity, Flux::Core::AssignmentValidator::ValidationResult::Error);
        QVERIFY(bad.message.contains("IC mismatch"));
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
