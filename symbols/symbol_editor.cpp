// symbols/symbol_editor.cpp
//
// Production-ready Symbol Editor
// Key fixes vs. original:
//   1.  m_drawnItems is now 1:1 with m_symbol.primitives() — overlay labels
//       live in m_overlayItems so index arithmetic is always correct.
//   2.  Undo commands no longer store raw QList& references (dangling-pointer
//       hazard); they capture by value or hold a stable QPointer to the editor.
//   3.  RemovePrimitiveCommand restores items at their ORIGINAL index, not by
//       appending to the end (which broke subsequent operations).
//   4.  Arc primitive uses consistent data keys (x, y, width, height,
//       startAngle, spanAngle) both when creating and reading.
//   5.  onPropertyChanged() does targeted visual updates instead of full rebuild.
//   6.  Rotate/flip/move commands wrap applySymbolDefinition() via
//       UpdateSymbolCommand so they are fully undoable.
//   7.  clearScene() removes overlay items separately to avoid double-delete.
//   8.  drawGrid() removed (replaced by SymbolEditorView::drawBackground()).
//   9.  buildVisual() is a pure factory; ownership passes to caller.
//  10.  QPointer<SymbolEditorView> used in lambdas to prevent dangling captures.

#include "symbol_editor.h"
#include "symbol_library.h"
#include "kicad_symbol_importer.h"
#include "ltspice_symbol_importer.h"
#include "../core/library_index.h"
#include "../ui/property_editor.h"
#include <QGraphicsTextItem>
#include "theme_manager.h"
#include "symbol_commands.h"
#include "pin_table_dialog.h"
#include "pin_modes_dialog.h"
#include "ui/ai_datasheet_import_dialog.h"
#include "../schematic/dialogs/spice_subcircuit_import_dialog.h"
#include "../schematic/dialogs/subcircuit_picker_dialog.h"
#include "../core/text_resolver.h"
#include "../simulator/bridge/model_library_manager.h"
#include "ui/text_properties_dialog.h"

#include <QGraphicsDropShadowEffect>
#include <QFileDialog>
#include <QFile>
#include <QStyleOptionGraphicsItem>
#include <QPainterPathStroker>
#include <QTabWidget>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QTableWidget>
#include <QHeaderView>

namespace {
QString runThemedOpenFileDialog(QWidget* parent, const QString& title, const QString& filter) {
    QFileDialog dlg(parent, title);
    dlg.setFileMode(QFileDialog::ExistingFile);
    dlg.setNameFilter(filter);
    dlg.setOption(QFileDialog::DontUseNativeDialog, true);
    if (auto* theme = ThemeManager::theme(); theme && theme->type() == PCBTheme::Light) {
        dlg.setStyleSheet(
            "QWidget { selection-background-color: #e2e8f0; selection-color: #111111; }"
            "QLineEdit, QComboBox { background-color: #ffffff; color: #1f2937; border: 1px solid #cbd5e1; border-radius: 4px; padding: 4px 6px; }"
        );
    }
    if (dlg.exec() != QDialog::Accepted) return QString();
    return dlg.selectedFiles().value(0);
}

QString runThemedSaveFileDialog(QWidget* parent, const QString& title, const QString& filter, const QString& suggested) {
    QFileDialog dlg(parent, title);
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    dlg.setNameFilter(filter);
    if (!suggested.isEmpty()) dlg.selectFile(suggested);
    dlg.setOption(QFileDialog::DontUseNativeDialog, true);
    if (auto* theme = ThemeManager::theme(); theme && theme->type() == PCBTheme::Light) {
        dlg.setStyleSheet(
            "QWidget { selection-background-color: #e2e8f0; selection-color: #111111; }"
            "QLineEdit, QComboBox { background-color: #ffffff; color: #1f2937; border: 1px solid #cbd5e1; border-radius: 4px; padding: 4px 6px; }"
        );
    }
    if (dlg.exec() != QDialog::Accepted) return QString();
    return dlg.selectedFiles().value(0);
}
static SymbolDefinition buildImportedSubcktSymbolForEditor(const SpiceSubcircuitImportDialog::Result& res) {
    SymbolDefinition def(res.subcktName);
    def.setDescription(QString("Auto-generated symbol for .subckt %1").arg(res.subcktName));
    def.setCategory("Integrated Circuits");
    def.setReferencePrefix("U");
    def.setDefaultValue(res.subcktName);
    def.setModelSource("project");
    def.setModelPath(res.relativeIncludePath);
    def.setModelName(res.subcktName);
    def.setSpiceModelName(res.subcktName);

    QMap<int, QString> mapping;
    QList<SpiceSubcircuitImportDialog::Result::PinMapping> pinMappings = res.pinMappings;
    if (pinMappings.isEmpty()) {
        for (int i = 0; i < res.pins.size(); ++i) {
            SpiceSubcircuitImportDialog::Result::PinMapping mappingEntry;
            mappingEntry.subcktPin = res.pins.at(i);
            mappingEntry.symbolPinName = res.pins.at(i);
            mappingEntry.symbolPinNumber = i + 1;
            pinMappings.append(mappingEntry);
        }
    }

    std::sort(pinMappings.begin(), pinMappings.end(), [](const auto& a, const auto& b) {
        return a.symbolPinNumber < b.symbolPinNumber;
    });

    const int pinCount = pinMappings.size();
    const int leftCount = (pinCount + 1) / 2;
    const int rightCount = pinCount - leftCount;
    const int pinsPerSide = qMax(leftCount, rightCount);
    const qreal bodyWidth = 120.0;
    const qreal pinSpacing = 25.0;
    const qreal pinLength = 20.0;
    const qreal bodyHalfHeight = qMax<qreal>(40.0, pinsPerSide * pinSpacing * 0.5);

    def.addPrimitive(SymbolPrimitive::createRect(QRectF(-bodyWidth / 2.0, -bodyHalfHeight, bodyWidth, bodyHalfHeight * 2.0), false));
    def.addPrimitive(SymbolPrimitive::createText(res.subcktName, QPointF(-30.0, -bodyHalfHeight - 18.0), 10));

    for (int i = 0; i < pinCount; ++i) {
        const auto& pinMapping = pinMappings.at(i);
        const bool leftSide = (i < leftCount);
        const int sideIndex = leftSide ? i : (i - leftCount);
        const int countOnSide = leftSide ? leftCount : (pinCount - leftCount);
        const int displayIndex = leftSide ? sideIndex : (countOnSide - 1 - sideIndex);
        const qreal y = ((countOnSide - 1) * pinSpacing * -0.5) + (displayIndex * pinSpacing);
        const QPointF pos(leftSide ? (-bodyWidth / 2.0 - pinLength) : (bodyWidth / 2.0 + pinLength), y);
        const QString orientation = leftSide ? "Right" : "Left";
        const int symbolPinNumber = pinMapping.symbolPinNumber;
        const QString subcktPinName = pinMapping.subcktPin;
        const QString symbolPinName = pinMapping.symbolPinName.isEmpty() ? subcktPinName : pinMapping.symbolPinName;

        SymbolPrimitive pin = SymbolPrimitive::createPin(pos, symbolPinNumber, symbolPinName, orientation, pinLength);
        def.addPrimitive(pin);
        mapping.insert(symbolPinNumber, subcktPinName);
    }

    def.setSpiceNodeMapping(mapping);
    return def;
}

} // namespace

// --- Helper classes to suppress default selection drawing ---
class FilteredRectItem : public QGraphicsRectItem {
public:
    using QGraphicsRectItem::QGraphicsRectItem;
    void paint(QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w) override {
        QStyleOptionGraphicsItem opt = *o; opt.state &= ~QStyle::State_Selected;
        QGraphicsRectItem::paint(p, &opt, w);
    }
    QPainterPath shape() const override {
        QPainterPathStroker stroker;
        stroker.setWidth(10.0); // Easier hit target for thin outlines
        QPainterPath hit = stroker.createStroke(QGraphicsRectItem::shape());
        hit.addPath(QGraphicsRectItem::shape());
        return hit;
    }
};

class FilteredEllipseItem : public QGraphicsEllipseItem {
public:
    using QGraphicsEllipseItem::QGraphicsEllipseItem;
    void paint(QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w) override {
        QStyleOptionGraphicsItem opt = *o; opt.state &= ~QStyle::State_Selected;
        QGraphicsEllipseItem::paint(p, &opt, w);
    }
    QPainterPath shape() const override {
        QPainterPathStroker stroker;
        stroker.setWidth(10.0); // Easier hit target for thin outlines
        QPainterPath hit = stroker.createStroke(QGraphicsEllipseItem::shape());
        hit.addPath(QGraphicsEllipseItem::shape());
        return hit;
    }
};

class FilteredLineItem : public QGraphicsLineItem {
public:
    using QGraphicsLineItem::QGraphicsLineItem;
    void paint(QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w) override {
        QStyleOptionGraphicsItem opt = *o; opt.state &= ~QStyle::State_Selected;
        QGraphicsLineItem::paint(p, &opt, w);
    }
    QPainterPath shape() const override {
        QPainterPathStroker stroker;
        stroker.setWidth(10); // 10px hit area
        return stroker.createStroke(QGraphicsLineItem::shape());
    }
};

class FilteredPathItem : public QGraphicsPathItem {
public:
    using QGraphicsPathItem::QGraphicsPathItem;
    void paint(QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w) override {
        QStyleOptionGraphicsItem opt = *o; opt.state &= ~QStyle::State_Selected;
        QGraphicsPathItem::paint(p, &opt, w);
    }
    QPainterPath shape() const override {
        QPainterPathStroker stroker;
        stroker.setWidth(10); // 10px hit area
        return stroker.createStroke(QGraphicsPathItem::shape());
    }
};

class FilteredPolygonItem : public QGraphicsPolygonItem {
public:
    using QGraphicsPolygonItem::QGraphicsPolygonItem;
    void paint(QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w) override {
        QStyleOptionGraphicsItem opt = *o; opt.state &= ~QStyle::State_Selected;
        QGraphicsPolygonItem::paint(p, &opt, w);
    }
    QPainterPath shape() const override {
        QPainterPathStroker stroker;
        stroker.setWidth(10.0); // Easier hit target for thin outlines
        QPainterPath hit = stroker.createStroke(QGraphicsPolygonItem::shape());
        hit.addPath(QGraphicsPolygonItem::shape());
        return hit;
    }
};

class FilteredGroupItem : public QGraphicsItemGroup {
public:
    using QGraphicsItemGroup::QGraphicsItemGroup;
    void paint(QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w) override {
        QStyleOptionGraphicsItem opt = *o; opt.state &= ~QStyle::State_Selected;
        QGraphicsItemGroup::paint(p, &opt, w);
    }
};

class FilteredSimpleTextItem : public QGraphicsSimpleTextItem {
public:
    using QGraphicsSimpleTextItem::QGraphicsSimpleTextItem;
    void paint(QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w) override {
        QStyleOptionGraphicsItem opt = *o; opt.state &= ~QStyle::State_Selected;
        QGraphicsSimpleTextItem::paint(p, &opt, w);
    }
};

class FilteredPixmapItem : public QGraphicsPixmapItem {
public:
    using QGraphicsPixmapItem::QGraphicsPixmapItem;
    void paint(QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w) override {
        QStyleOptionGraphicsItem opt = *o; opt.state &= ~QStyle::State_Selected;
        QGraphicsPixmapItem::paint(p, &opt, w);
    }
};

#include <QGraphicsItem>
#include <QHeaderView>
#include <QSignalBlocker>
#include <QSet>
#include <QDir>
#include <algorithm>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QActionGroup>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QTimer>
#include <QScrollArea>
#include <QScrollBar>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QPainter>
#include <QKeyEvent>
#include <QDebug>
#include <QUndoStack>
#include <QUndoCommand>
#include <QTreeWidget>
#include <QRegularExpression>
#include "config_manager.h"

namespace {
QString resolveModelPathForEditor(const QString& rawPath, const QString& source, const QString& projectKey = QString()) {
    const QString trimmed = rawPath.trimmed();
    if (trimmed.isEmpty()) return QString();

    QFileInfo fi(trimmed);
    if (fi.isAbsolute()) {
        return fi.exists() ? fi.absoluteFilePath() : QString();
    }

    if (source == "project") {
        if (projectKey.trimmed().isEmpty()) return QString();
        QString candidate = QDir(projectKey).filePath(trimmed);
        if (QFileInfo::exists(candidate)) return QFileInfo(candidate).absoluteFilePath();
        return QString();
    }

    QStringList roots = ConfigManager::instance().libraryRoots();
    for (const QString& root : roots) {
        if (root.trimmed().isEmpty()) continue;
        QString candidate = QDir(root).filePath(trimmed);
        if (QFileInfo::exists(candidate)) return candidate;
    }

    QString baseName = QFileInfo(trimmed).fileName();
    QStringList modelDirs = ConfigManager::instance().modelPaths();
    for (const QString& dir : modelDirs) {
        QString candidate = QDir(dir).filePath(trimmed);
        if (QFileInfo::exists(candidate)) return candidate;
        if (!baseName.isEmpty()) {
            candidate = QDir(dir).filePath(baseName);
            if (QFileInfo::exists(candidate)) return candidate;
        }
    }

    return QString();
}

QString detectModelNameFromFile(const QString& filePath, const QString& prefix) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();

    QRegularExpression subcktRe("^\\s*\\.subckt\\s+([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpression modelRe("^\\s*\\.model\\s+([^\\s]+)", QRegularExpression::CaseInsensitiveOption);

    QString foundSubckt;
    QString foundModel;
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (foundSubckt.isEmpty()) {
            auto m = subcktRe.match(line);
            if (m.hasMatch()) foundSubckt = m.captured(1).trimmed();
        }
        if (foundModel.isEmpty()) {
            auto m = modelRe.match(line);
            if (m.hasMatch()) foundModel = m.captured(1).trimmed();
        }
        if (!foundSubckt.isEmpty() && !foundModel.isEmpty()) break;
    }

    const QString base = QFileInfo(filePath).completeBaseName();
    if (!base.isEmpty()) {
        if (foundSubckt.compare(base, Qt::CaseInsensitive) == 0) return foundSubckt;
        if (foundModel.compare(base, Qt::CaseInsensitive) == 0) return foundModel;
    }

    const QString p = prefix.trimmed().toUpper();
    const bool preferSubckt = (p == "X");
    const bool preferModel = (p == "D" || p == "Q" || p == "M" || p == "J");

    if (preferSubckt && !foundSubckt.isEmpty()) return foundSubckt;
    if (preferModel && !foundModel.isEmpty()) return foundModel;

    if (!foundSubckt.isEmpty()) return foundSubckt;
    if (!foundModel.isEmpty()) return foundModel;
    return QString();
}

QPair<QString, QString> classifyModelBindingPath(const QString& absolutePath, const QString& projectKey) {
    const QString cleanPath = QDir::cleanPath(QDir::fromNativeSeparators(absolutePath));
    if (cleanPath.isEmpty()) return {QString("none"), QString()};

    if (!projectKey.trimmed().isEmpty()) {
        const QString cleanProject = QDir::cleanPath(QDir::fromNativeSeparators(projectKey));
        const QString relativeProjectPath = QDir(cleanProject).relativeFilePath(cleanPath);
        if (!relativeProjectPath.startsWith("../") && relativeProjectPath != "..") {
            return {QString("project"), QDir::fromNativeSeparators(relativeProjectPath)};
        }
    }

    const QStringList roots = ConfigManager::instance().libraryRoots();
    for (const QString& root : roots) {
        if (root.trimmed().isEmpty()) continue;
        const QString cleanRoot = QDir::cleanPath(QDir::fromNativeSeparators(root));
        const QString relativeRootPath = QDir(cleanRoot).relativeFilePath(cleanPath);
        if (!relativeRootPath.startsWith("../") && relativeRootPath != "..") {
            return {QString("library"), QDir::fromNativeSeparators(relativeRootPath)};
        }
    }

    return {QString("absolute"), cleanPath};
}

QString normalizePinNameForStatus(QString text) {
    text = text.trimmed().toLower();
    text.replace(QRegularExpression("[^a-z0-9]+"), "");
    if (text == "plus" || text == "noninv" || text == "noninverting" || text == "inp" || text == "inplus") return "inplus";
    if (text == "minus" || text == "inv" || text == "inverting" || text == "inn" || text == "inminus") return "inminus";
    if (text == "vdd" || text == "vcc" || text == "vp" || text == "vplus" || text == "supplyplus") return "vplus";
    if (text == "vss" || text == "vee" || text == "vn" || text == "vminus" || text == "supplyminus") return "vminus";
    if (text == "gnd" || text == "ground" || text == "vss0") return "gnd";
    if (text == "out" || text == "output") return "out";
    return text;
}

struct MappingStatusInfo {
    QString label;
    QString detail;
    QColor color;
};
}
#include <QTableWidget>
#include <QListWidget>
#include <QTextEdit>
#include <QJsonDocument>
#include <QTreeWidgetItem>
#include <QFont>
#include <QPointer>
#include <QGuiApplication>
#include <QScreen>
#include <QShowEvent>
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include "config_manager.h"

// ─────────────────────────────────────────────────────────────────────────────
//  SymbolEditor – Construction
// ─────────────────────────────────────────────────────────────────────────────

namespace {
QString symbolEditorStateKey(const QString& projectKey) {
    const QString trimmed = projectKey.trimmed();
    if (trimmed.isEmpty()) return QStringLiteral("SymbolEditor");
    const QByteArray hash = QCryptographicHash::hash(trimmed.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QStringLiteral("SymbolEditor_") + QString::fromLatin1(hash);
}

struct WizardTemplateDef {
    QString id;
    QString name;
    QString description;
    QString kind; // "ic_dual", "ic_quad", "logic", "symbol"
    QString defaultCategory;
    QString defaultPrefix;
    QString defaultSymbolName;
    int pins = 0;
    qreal pitch = 10.0;
    qreal width = 50.0;
    QString gate; // and, nand, or, nor, xor, xnor, not, buf
    QJsonObject symbolJson; // used when kind == "symbol"
};

const QList<WizardTemplateDef>& builtinWizardTemplateDefs() {
    auto addDigitalBlockText = [](SymbolDefinition& def, const QString& textValue, const QPointF& pos,
                                  int size, const QString& hAlign = "center", const QString& vAlign = "center") {
        SymbolPrimitive text = SymbolPrimitive::createText(textValue, pos, size, QColor(Qt::black));
        text.data["hAlign"] = hAlign;
        text.data["vAlign"] = vAlign;
        def.addPrimitive(text);
    };

    auto addDigitalMarker = [](SymbolDefinition& def, const QString& pinName, const QPointF& anchor) {
        const QString upper = pinName.trimmed().toUpper();
        if (upper == "CLK" || upper == "CLOCK" || upper == "CK" || upper == "C") {
            def.addPrimitive(SymbolPrimitive::createLine(anchor + QPointF(-8, -6), anchor + QPointF(0, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(anchor + QPointF(-8, 6), anchor + QPointF(0, 0)));
        } else if (upper == "EN" || upper == "G" || upper == "GATE") {
            def.addPrimitive(SymbolPrimitive::createLine(anchor + QPointF(-8, -6), anchor + QPointF(0, -6)));
            def.addPrimitive(SymbolPrimitive::createLine(anchor + QPointF(-8, 6), anchor + QPointF(0, 6)));
            def.addPrimitive(SymbolPrimitive::createLine(anchor + QPointF(0, -6), anchor + QPointF(0, 6)));
        }
    };

    auto buildDigitalBlockDef = [&](const QString& symbolName,
                                    const QString& description,
                                    const QString& spiceModel,
                                    const QString& label,
                                    const QList<SymbolPrimitive>& pins,
                                    qreal bodyHeight,
                                    bool invertPrimaryOutput = false,
                                    bool invertSecondaryOutput = false) {
        SymbolDefinition def(symbolName);
        def.setDescription(description);
        def.setCategory("Logic");
        def.setReferencePrefix("U");
        def.setSpiceModelName(spiceModel);

        const qreal halfHeight = bodyHeight / 2.0;
        const qreal bodyLeft = -45.0;
        const qreal bodyWidth = 90.0;
        def.addPrimitive(SymbolPrimitive::createRect(QRectF(bodyLeft, -halfHeight, bodyWidth, bodyHeight), false));
        def.addPrimitive(SymbolPrimitive::createLine(QPointF(bodyLeft, -halfHeight + 15.0), QPointF(bodyLeft + bodyWidth, -halfHeight + 15.0)));
        addDigitalBlockText(def, spiceModel, QPointF(0, -halfHeight + 7.5), 8);
        addDigitalBlockText(def, label, QPointF(0, 6.0), 10);

        int outputIndex = 0;
        for (const SymbolPrimitive& pin : pins) {
            def.addPrimitive(pin);
            const QString direction = pin.data.value("signalDirection").toString();
            const QString pinName = pin.data.value("name").toString();
            const QPointF pinPos(pin.data.value("x").toDouble(), pin.data.value("y").toDouble());
            if (direction == "input") {
                addDigitalMarker(def, pinName, QPointF(bodyLeft, pinPos.y()));
            } else if (direction == "output") {
                const bool invertThis = (outputIndex == 0) ? invertPrimaryOutput : invertSecondaryOutput;
                if (invertThis) {
                    def.addPrimitive(SymbolPrimitive::createCircle(QPointF(bodyLeft + bodyWidth + 4.0, pinPos.y()), 3.0, false));
                }
                ++outputIndex;
            }
        }

        return def;
    };

    auto makeDigitalPin = [](const QPointF& pos, int number, const QString& name,
                             const QString& orientation, const QString& direction) {
        SymbolPrimitive pin = SymbolPrimitive::createPin(pos, number, name, orientation, 15.0);
        pin.data["signalDomain"] = "digital_event";
        pin.data["signalDirection"] = direction;
        return pin;
    };

    static const QList<WizardTemplateDef> defs = {
        {"ic_8pins", "IC 8 Pins (DIP/SOIC)", "Dual-inline 8-pin IC frame", "ic_dual", "IC", "U", "IC8", 8, 10.0, 50.0, ""},
        {"ic_14pins", "IC 14 Pins (DIP/SOIC)", "Dual-inline 14-pin IC frame", "ic_dual", "IC", "U", "IC14", 14, 10.0, 60.0, ""},
        {"ic_16pins", "IC 16 Pins (DIP/SOIC)", "Dual-inline 16-pin IC frame", "ic_dual", "IC", "U", "IC16", 16, 10.0, 65.0, ""},
        {"ic_20pins", "IC 20 Pins (DIP/SOIC)", "Dual-inline 20-pin IC frame", "ic_dual", "IC", "U", "IC20", 20, 10.0, 70.0, ""},
        {"ic_28pins", "IC 28 Pins (DIP/SOIC)", "Dual-inline 28-pin IC frame", "ic_dual", "IC", "U", "IC28", 28, 10.0, 80.0, ""},
        {"ic_40pins", "IC 40 Pins (DIP/SOIC)", "Dual-inline 40-pin IC frame", "ic_dual", "IC", "U", "IC40", 40, 10.0, 95.0, ""},
        {"ic_qfn_32", "IC 32 Pins (QFP/QFN)", "Quad package 32-pin IC frame", "ic_quad", "IC", "U", "IC32", 32, 10.0, 80.0, ""},
        {"and_2", "AND Gate (2-input)", "Digital 2-input AND gate", "logic", "Digital", "U", "AND2", 3, 10.0, 0.0, "and"},
        {"nand_2", "NAND Gate (2-input)", "Digital 2-input NAND gate", "logic", "Digital", "U", "NAND2", 3, 10.0, 0.0, "nand"},
        {"or_2", "OR Gate (2-input)", "Digital 2-input OR gate", "logic", "Digital", "U", "OR2", 3, 10.0, 0.0, "or"},
        {"nor_2", "NOR Gate (2-input)", "Digital 2-input NOR gate", "logic", "Digital", "U", "NOR2", 3, 10.0, 0.0, "nor"},
        {"xor_2", "XOR Gate (2-input)", "Digital 2-input XOR gate", "logic", "Digital", "U", "XOR2", 3, 10.0, 0.0, "xor"},
        {"xnor_2", "XNOR Gate (2-input)", "Digital 2-input XNOR gate", "logic", "Digital", "U", "XNOR2", 3, 10.0, 0.0, "xnor"},
        {"not_1", "NOT Gate (Inverter)", "Digital inverter", "logic", "Digital", "U", "NOT", 2, 10.0, 0.0, "not"},
        {"buf_1", "Buffer Gate", "Digital non-inverting buffer", "logic", "Digital", "U", "BUF", 2, 10.0, 0.0, "buf"},
        {"d_flipflop", "D Flip-Flop", "Edge-triggered D flip-flop with set/reset and Q/QN outputs", "symbol", "Logic", "U", "D_FlipFlop", 6, 10.0, 0.0, "",
            buildDigitalBlockDef(
                "D_FlipFlop",
                "Edge-triggered D flip-flop with asynchronous set/reset and complementary outputs",
                "DFF",
                "D FF",
                {
                    makeDigitalPin(QPointF(-60, -30.0), 1, "D", "Right", "input"),
                    makeDigitalPin(QPointF(-60, -15.0), 2, "CLK", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 15.0), 3, "SET", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 30.0), 4, "RESET", "Right", "input"),
                    makeDigitalPin(QPointF(60, 0.0), 5, "Q", "Left", "output"),
                    makeDigitalPin(QPointF(60, 15.0), 6, "QN", "Left", "output"),
                },
                90.0, false, true).toJson()},
        {"jk_flipflop", "JK Flip-Flop", "Edge-triggered JK flip-flop with set/reset and Q/QN outputs", "symbol", "Logic", "U", "JK_FlipFlop", 7, 10.0, 0.0, "",
            buildDigitalBlockDef(
                "JK_FlipFlop",
                "Edge-triggered JK flip-flop with asynchronous set/reset and complementary outputs",
                "JKFF",
                "JK FF",
                {
                    makeDigitalPin(QPointF(-60, -30.0), 1, "J", "Right", "input"),
                    makeDigitalPin(QPointF(-60, -15.0), 2, "K", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 0.0), 3, "CLK", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 15.0), 4, "SET", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 30.0), 5, "RESET", "Right", "input"),
                    makeDigitalPin(QPointF(60, 0.0), 6, "Q", "Left", "output"),
                    makeDigitalPin(QPointF(60, 15.0), 7, "QN", "Left", "output"),
                },
                90.0, false, true).toJson()},
        {"t_flipflop", "T Flip-Flop", "Edge-triggered toggle flip-flop with set/reset and Q/QN outputs", "symbol", "Logic", "U", "T_FlipFlop", 6, 10.0, 0.0, "",
            buildDigitalBlockDef(
                "T_FlipFlop",
                "Edge-triggered toggle flip-flop with asynchronous set/reset and complementary outputs",
                "TFF",
                "T FF",
                {
                    makeDigitalPin(QPointF(-60, -30.0), 1, "T", "Right", "input"),
                    makeDigitalPin(QPointF(-60, -15.0), 2, "CLK", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 15.0), 3, "SET", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 30.0), 4, "RESET", "Right", "input"),
                    makeDigitalPin(QPointF(60, 0.0), 5, "Q", "Left", "output"),
                    makeDigitalPin(QPointF(60, 15.0), 6, "QN", "Left", "output"),
                },
                90.0, false, true).toJson()},
        {"sr_flipflop", "SR Flip-Flop", "Edge-triggered set-reset flip-flop with set/reset and Q/QN outputs", "symbol", "Logic", "U", "SR_FlipFlop", 7, 10.0, 0.0, "",
            buildDigitalBlockDef(
                "SR_FlipFlop",
                "Edge-triggered set-reset flip-flop with asynchronous set/reset and complementary outputs",
                "SRFF",
                "SR FF",
                {
                    makeDigitalPin(QPointF(-60, -30.0), 1, "S", "Right", "input"),
                    makeDigitalPin(QPointF(-60, -15.0), 2, "R", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 0.0), 3, "CLK", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 15.0), 4, "SET", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 30.0), 5, "RESET", "Right", "input"),
                    makeDigitalPin(QPointF(60, 0.0), 6, "Q", "Left", "output"),
                    makeDigitalPin(QPointF(60, 15.0), 7, "QN", "Left", "output"),
                },
                90.0, false, true).toJson()},
        {"d_latch", "D Latch", "Level-sensitive D latch with enable, set/reset, and Q/QN outputs", "symbol", "Logic", "U", "D_Latch", 6, 10.0, 0.0, "",
            buildDigitalBlockDef(
                "D_Latch",
                "Level-sensitive D latch with asynchronous set/reset and complementary outputs",
                "DLATCH",
                "D LAT",
                {
                    makeDigitalPin(QPointF(-60, -30.0), 1, "D", "Right", "input"),
                    makeDigitalPin(QPointF(-60, -15.0), 2, "EN", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 15.0), 3, "SET", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 30.0), 4, "RESET", "Right", "input"),
                    makeDigitalPin(QPointF(60, 0.0), 5, "Q", "Left", "output"),
                    makeDigitalPin(QPointF(60, 15.0), 6, "QN", "Left", "output"),
                },
                90.0, false, true).toJson()},
        {"sr_latch", "SR Latch", "Level-sensitive SR latch with enable, set/reset, and Q/QN outputs", "symbol", "Logic", "U", "SR_Latch", 7, 10.0, 0.0, "",
            buildDigitalBlockDef(
                "SR_Latch",
                "Level-sensitive set-reset latch with enable, asynchronous set/reset, and complementary outputs",
                "SRLATCH",
                "SR LAT",
                {
                    makeDigitalPin(QPointF(-60, -30.0), 1, "S", "Right", "input"),
                    makeDigitalPin(QPointF(-60, -15.0), 2, "R", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 0.0), 3, "EN", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 15.0), 4, "SET", "Right", "input"),
                    makeDigitalPin(QPointF(-60, 30.0), 5, "RESET", "Right", "input"),
                    makeDigitalPin(QPointF(60, 0.0), 6, "Q", "Left", "output"),
                    makeDigitalPin(QPointF(60, 15.0), 7, "QN", "Left", "output"),
                },
                90.0, false, true).toJson()},
    };
    return defs;
}

QList<WizardTemplateDef> wizardTemplateDefsForProject(const QString& projectKey);
void ensureProjectWizardTemplatesFile(const QString& projectKey);

QString projectWizardTemplatesPath(const QString& projectKey) {
    const QString trimmed = projectKey.trimmed();
    if (trimmed.isEmpty()) return QString();

    QFileInfo info(trimmed);
    QString projectDir = trimmed;
    if (!info.isDir()) {
        projectDir = info.absolutePath();
    }
    if (projectDir.isEmpty()) return QString();

    return QDir(projectDir).filePath(".viospice/symbol_wizard_templates.json");
}

QString legacyGlobalWizardTemplatesPath() {
    return QDir::home().filePath(".viospice/symbol_wizard_templates.json");
}

QJsonObject wizardTemplateToJson(const WizardTemplateDef& tpl) {
    QJsonObject obj;
    obj["id"] = tpl.id;
    obj["name"] = tpl.name;
    obj["description"] = tpl.description;
    obj["kind"] = tpl.kind;
    obj["defaultCategory"] = tpl.defaultCategory;
    obj["defaultPrefix"] = tpl.defaultPrefix;
    obj["defaultSymbolName"] = tpl.defaultSymbolName;
    obj["pins"] = tpl.pins;
    obj["pitch"] = tpl.pitch;
    obj["width"] = tpl.width;
    obj["gate"] = tpl.gate;
    if (tpl.kind == "symbol" && !tpl.symbolJson.isEmpty()) {
        obj["symbol"] = tpl.symbolJson;
    }
    return obj;
}

bool wizardTemplateFromJson(const QJsonObject& obj, WizardTemplateDef& out) {
    const QString id = obj.value("id").toString().trimmed();
    const QString name = obj.value("name").toString().trimmed();
    const QString kind = obj.value("kind").toString().trimmed().toLower();
    if (id.isEmpty() || name.isEmpty() || kind.isEmpty()) return false;

    out.id = id;
    out.name = name;
    out.description = obj.value("description").toString();
    out.kind = kind;
    out.defaultCategory = obj.value("defaultCategory").toString("IC");
    out.defaultPrefix = obj.value("defaultPrefix").toString("U");
    out.defaultSymbolName = obj.value("defaultSymbolName").toString(name);
    out.pins = qMax(0, obj.value("pins").toInt(0));
    out.pitch = obj.value("pitch").toDouble(10.0);
    out.width = obj.value("width").toDouble(50.0);
    out.gate = obj.value("gate").toString().toLower();
    out.symbolJson = obj.value("symbol").toObject();
    return true;
}

QString sanitizeWizardTemplateId(const QString& text) {
    QString id = text.trimmed().toLower();
    id.replace(QRegularExpression("[^a-z0-9]+"), "_");
    while (id.contains("__")) id.replace("__", "_");
    if (id.startsWith('_')) id.remove(0, 1);
    if (id.endsWith('_')) id.chop(1);
    if (id.isEmpty()) id = "custom_symbol";
    if (!id.startsWith("custom_")) id = "custom_" + id;
    return id;
}

QString uniqueWizardTemplateId(const QString& projectKey, const QString& preferredId) {
    const QList<WizardTemplateDef> defs = wizardTemplateDefsForProject(projectKey);
    QSet<QString> usedIds;
    for (const WizardTemplateDef& tpl : defs) usedIds.insert(tpl.id);

    if (!usedIds.contains(preferredId)) return preferredId;
    for (int i = 2; i < 100000; ++i) {
        const QString candidate = QString("%1_%2").arg(preferredId).arg(i);
        if (!usedIds.contains(candidate)) return candidate;
    }
    return preferredId + "_" + QString::number(QDateTime::currentSecsSinceEpoch());
}

bool upsertWizardTemplate(const QString& projectKey, const WizardTemplateDef& tpl, QString* errorOut = nullptr) {
    const QString path = projectWizardTemplatesPath(projectKey);
    if (path.isEmpty()) {
        if (errorOut) *errorOut = "Wizard template path is empty.";
        return false;
    }

    ensureProjectWizardTemplatesFile(projectKey);

    QJsonObject root;
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (doc.isObject()) root = doc.object();
    }

    root["version"] = 1;
    QJsonArray arr = root.value("templates").toArray();
    bool replaced = false;
    for (int i = 0; i < arr.size(); ++i) {
        const QJsonObject obj = arr[i].toObject();
        if (obj.value("id").toString() == tpl.id) {
            arr[i] = wizardTemplateToJson(tpl);
            replaced = true;
            break;
        }
    }
    if (!replaced) arr.append(wizardTemplateToJson(tpl));
    root["templates"] = arr;

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorOut) *errorOut = QString("Failed to write template file:\n%1").arg(path);
        return false;
    }
    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    out.close();
    return true;
}

void ensureProjectWizardTemplatesFile(const QString& projectKey) {
    const QString path = projectWizardTemplatesPath(projectKey);
    if (path.isEmpty() || QFileInfo::exists(path)) return;

    const QFileInfo outInfo(path);
    QDir().mkpath(outInfo.absolutePath());

    QJsonArray arr;
    const QList<WizardTemplateDef>& defaults = builtinWizardTemplateDefs();
    for (const WizardTemplateDef& tpl : defaults) {
        arr.append(wizardTemplateToJson(tpl));
    }

    QJsonObject root;
    root["version"] = 1;
    root["templates"] = arr;

    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;
    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    out.close();
}

QList<WizardTemplateDef> wizardTemplateDefsForProject(const QString& projectKey) {
    QList<WizardTemplateDef> defs = builtinWizardTemplateDefs();
    const QString path = projectWizardTemplatesPath(projectKey);
    if (path.isEmpty()) return defs;

    ensureProjectWizardTemplatesFile(projectKey);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return defs;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) return defs;

    const QJsonArray arr = doc.object().value("templates").toArray();
    if (arr.isEmpty()) return defs;

    QMap<QString, int> byId;
    for (int i = 0; i < defs.size(); ++i) byId[defs[i].id] = i;

    for (const QJsonValue& v : arr) {
        if (!v.isObject()) continue;
        WizardTemplateDef parsed;
        if (!wizardTemplateFromJson(v.toObject(), parsed)) continue;
        if (byId.contains(parsed.id)) {
            defs[byId.value(parsed.id)] = parsed;
        } else {
            byId[parsed.id] = defs.size();
            defs.append(parsed);
        }
    }

    return defs;
}

const WizardTemplateDef* findWizardTemplate(const QString& id, const QList<WizardTemplateDef>& defs) {
    if (id.trimmed().isEmpty()) return nullptr;
    for (const WizardTemplateDef& def : defs) {
        if (def.id == id) return &def;
    }
    return nullptr;
}

SymbolDefinition buildLogicTemplateSymbol(const WizardTemplateDef& tpl,
                                          const QString& symbolName,
                                          const QString& prefix,
                                          const QString& category) {
    SymbolDefinition def;
    def.setName(symbolName);
    def.setReferencePrefix(prefix);
    def.setCategory(category);
    def.setDescription(tpl.description);

    const QString gate = tpl.gate.toLower();
    const bool unary = (gate == "not" || gate == "buf");
    const bool inverted = (gate == "nand" || gate == "nor" || gate == "xnor" || gate == "not");
    const QString displayLabel = gate.toUpper();
    const QString modelLabel = (gate == "buf") ? QString("BUF") : (gate == "not" ? QString("NOT") : displayLabel);
    const qreal bodyHeight = unary ? 60.0 : 60.0;
    const qreal halfHeight = bodyHeight / 2.0;
    const qreal bodyLeft = -45.0;
    const qreal bodyWidth = 90.0;

    auto makeLogicPin = [](const QPointF& pos, int number, const QString& name,
                           const QString& orientation, const QString& direction, qreal length = 15.0) {
        SymbolPrimitive pin = SymbolPrimitive::createPin(pos, number, name, orientation, length);
        pin.data["signalDomain"] = "digital_event";
        pin.data["signalDirection"] = direction;
        return pin;
    };

    def.addPrimitive(SymbolPrimitive::createRect(QRectF(bodyLeft, -halfHeight, bodyWidth, bodyHeight), false));
    def.addPrimitive(SymbolPrimitive::createLine(QPointF(bodyLeft, -halfHeight + 15.0), QPointF(bodyLeft + bodyWidth, -halfHeight + 15.0)));

    SymbolPrimitive modelText = SymbolPrimitive::createText(modelLabel, QPointF(0, -halfHeight + 7.5), 8, QColor(Qt::black));
    modelText.data["hAlign"] = "center";
    modelText.data["vAlign"] = "center";
    def.addPrimitive(modelText);

    SymbolPrimitive labelText = SymbolPrimitive::createText(displayLabel, QPointF(0, 6.0), 10, QColor(Qt::black));
    labelText.data["hAlign"] = "center";
    labelText.data["vAlign"] = "center";
    def.addPrimitive(labelText);

    if (unary) {
        def.addPrimitive(makeLogicPin(QPointF(-60, 0.0), 1, "A", "Right", "input"));
        def.addPrimitive(makeLogicPin(QPointF(60, 0.0), 2, "Y", "Left", "output", inverted ? 17.0 : 15.0));
    } else {
        def.addPrimitive(makeLogicPin(QPointF(-60, -15.0), 1, "A", "Right", "input"));
        def.addPrimitive(makeLogicPin(QPointF(-60, 15.0), 2, "B", "Right", "input"));
        def.addPrimitive(makeLogicPin(QPointF(60, 0.0), 3, "Y", "Left", "output", inverted ? 17.0 : 15.0));
    }

    if (inverted) {
        def.addPrimitive(SymbolPrimitive::createCircle(QPointF(bodyLeft + bodyWidth + 4.0, 0.0), 3.0, false));
    }

    return def;
}

SymbolDefinition buildIcTemplateSymbol(const WizardTemplateDef& tpl,
                                       const QString& symbolName,
                                       const QString& prefix,
                                       const QString& category) {
    SymbolDefinition def;
    def.setName(symbolName);
    def.setReferencePrefix(prefix);
    def.setCategory(category);
    def.setDescription(tpl.description);

    const bool quad = (tpl.kind == "ic_quad");
    const int pins = qMax(quad ? 4 : 2, tpl.pins);
    const qreal pitch = tpl.pitch > 0.0 ? tpl.pitch : 10.0;
    const qreal width = tpl.width > 0.0 ? tpl.width : 50.0;

    if (!quad) {
        const int half = qMax(1, pins / 2);
        const qreal bodyHeight = qMax(2.0 * pitch, half * pitch + pitch);
        def.addPrimitive(SymbolPrimitive::createRect(QRectF(-width / 2.0, -bodyHeight / 2.0, width, bodyHeight), false));

        for (int i = 0; i < half; ++i) {
            const qreal y = -bodyHeight / 2.0 + pitch + i * pitch;
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(-width / 2.0 - 15.0, y), i + 1, QString::number(i + 1), "Right", 15.0));
        }
        for (int i = 0; i < half; ++i) {
            const qreal y = bodyHeight / 2.0 - pitch - i * pitch;
            const int n = half + i + 1;
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(width / 2.0 + 15.0, y), n, QString::number(n), "Left", 15.0));
        }
    } else {
        const int perSide = qMax(1, pins / 4);
        const qreal side = qMax(2.0 * pitch, perSide * pitch + pitch);
        def.addPrimitive(SymbolPrimitive::createRect(QRectF(-side / 2.0, -side / 2.0, side, side), false));

        int pinNum = 1;
        for (int i = 0; i < perSide; ++i) {
            const qreal y = -side / 2.0 + pitch + i * pitch;
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(-side / 2.0 - 15.0, y), pinNum, QString::number(pinNum), "Right", 15.0));
            ++pinNum;
        }
        for (int i = 0; i < perSide; ++i) {
            const qreal x = -side / 2.0 + pitch + i * pitch;
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(x, side / 2.0 + 15.0), pinNum, QString::number(pinNum), "Up", 15.0));
            ++pinNum;
        }
        for (int i = 0; i < perSide; ++i) {
            const qreal y = side / 2.0 - pitch - i * pitch;
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(side / 2.0 + 15.0, y), pinNum, QString::number(pinNum), "Left", 15.0));
            ++pinNum;
        }
        for (int i = 0; i < perSide; ++i) {
            const qreal x = side / 2.0 - pitch - i * pitch;
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(x, -side / 2.0 - 15.0), pinNum, QString::number(pinNum), "Down", 15.0));
            ++pinNum;
        }
    }

    return def;
}

void translatePrimitive(SymbolPrimitive& prim, qreal dx, qreal dy) {
    switch (prim.type) {
    case SymbolPrimitive::Line:
        prim.data["x1"] = prim.data.value("x1").toDouble() + dx;
        prim.data["y1"] = prim.data.value("y1").toDouble() + dy;
        prim.data["x2"] = prim.data.value("x2").toDouble() + dx;
        prim.data["y2"] = prim.data.value("y2").toDouble() + dy;
        break;
    case SymbolPrimitive::Bezier:
        prim.data["x1"] = prim.data.value("x1").toDouble() + dx;
        prim.data["y1"] = prim.data.value("y1").toDouble() + dy;
        prim.data["x2"] = prim.data.value("x2").toDouble() + dx;
        prim.data["y2"] = prim.data.value("y2").toDouble() + dy;
        prim.data["x3"] = prim.data.value("x3").toDouble() + dx;
        prim.data["y3"] = prim.data.value("y3").toDouble() + dy;
        prim.data["x4"] = prim.data.value("x4").toDouble() + dx;
        prim.data["y4"] = prim.data.value("y4").toDouble() + dy;
        break;
    case SymbolPrimitive::Rect:
    case SymbolPrimitive::Arc:
    case SymbolPrimitive::Text:
    case SymbolPrimitive::Pin:
    case SymbolPrimitive::Image:
        prim.data["x"] = prim.data.value("x").toDouble() + dx;
        prim.data["y"] = prim.data.value("y").toDouble() + dy;
        break;
    case SymbolPrimitive::Circle: {
        const bool hasCenterX = prim.data.contains("centerX");
        const bool hasCenterY = prim.data.contains("centerY");
        const bool hasCx = prim.data.contains("cx");
        const bool hasCy = prim.data.contains("cy");
        if (hasCenterX) prim.data["centerX"] = prim.data.value("centerX").toDouble() + dx;
        if (hasCenterY) prim.data["centerY"] = prim.data.value("centerY").toDouble() + dy;
        if (hasCx) prim.data["cx"] = prim.data.value("cx").toDouble() + dx;
        if (hasCy) prim.data["cy"] = prim.data.value("cy").toDouble() + dy;
        break;
    }
    case SymbolPrimitive::Polygon: {
        QJsonArray points = prim.data.value("points").toArray();
        for (int i = 0; i < points.size(); ++i) {
            QJsonObject p = points[i].toObject();
            p["x"] = p.value("x").toDouble() + dx;
            p["y"] = p.value("y").toDouble() + dy;
            points[i] = p;
        }
        prim.data["points"] = points;
        break;
    }
    default:
        break;
    }
}
}

SymbolEditor::SymbolEditor(QWidget* parent)
    : QMainWindow(parent)
    , m_undoStack(new QUndoStack(this)) {
    setObjectName("SymbolEditor");
    setupUI();
    setProjectKey(QString());
    applyTheme();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &SymbolEditor::applyTheme);
}

SymbolEditor::SymbolEditor(const SymbolDefinition& symbol, QWidget* parent)
    : QMainWindow(parent)
    , m_symbol(symbol)
    , m_undoStack(new QUndoStack(this)) {
    setObjectName("SymbolEditor");
    setupUI();
    setProjectKey(QString());
    setSymbolDefinition(symbol);
    applyTheme();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &SymbolEditor::applyTheme);
}

SymbolEditor::~SymbolEditor() {
    // Overlay items are not in m_drawnItems, so we must clean them separately.
    removeOverlayItems();
    // m_drawnItems are owned by the QGraphicsScene; no manual delete needed.
}

// ─────────────────────────────────────────────────────────────────────────────
//  SymbolEditor – Scene helpers
// ─────────────────────────────────────────────────────────────────────────────

void SymbolEditor::applyTheme() {
    PCBTheme* theme = ThemeManager::theme();
    if (!theme) return;
    
    theme->applyToWidget(this);

    QString bg = theme->windowBackground().name();
    QString panelBg = theme->panelBackground().name();
    QString fg = theme->textColor().name();
    QString textSec = theme->textSecondary().name();
    QString border = theme->panelBorder().name();
    QString accent = theme->accentColor().name();
    QString inputBg = (theme->type() == PCBTheme::Light) ? "#ffffff" : "#121212";
    QString btnBg = (theme->type() == PCBTheme::Light) ? "#f8fafc" : "#2d2d30";
    QString btnHover = (theme->type() == PCBTheme::Light) ? "#e2e8f0" : "#3c3c3c";
    QString selBg = (theme->type() == PCBTheme::Light) ? "#e2e8f0" : "#3c3c3c";
    QString selText = (theme->type() == PCBTheme::Light) ? "#111111" : "#ffffff";
    QString btnPressed = (theme->type() == PCBTheme::Light) ? "#e2e8f0" : accent;

    if (theme->type() == PCBTheme::Light) {
        QPalette pal = palette();
        pal.setColor(QPalette::Button, QColor(inputBg));
        pal.setColor(QPalette::ButtonText, QColor(fg));
        pal.setColor(QPalette::Highlight, QColor(selBg));
        pal.setColor(QPalette::HighlightedText, QColor(selText));
        setPalette(pal);

        const QString comboStyle = QString(
            "QComboBox { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 4px 8px; }"
            "QComboBox::drop-down { border: none; }"
            "QComboBox QAbstractItemView { background: %1; color: %2; selection-background-color: %4; selection-color: %5; }"
        ).arg(inputBg, fg, border, selBg, selText);

        const QString spinStyle = QString(
            "QSpinBox, QDoubleSpinBox { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 4px 8px; }"
            "QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { border: none; }"
        ).arg(inputBg, fg, border);

        for (auto* cb : findChildren<QComboBox*>()) {
            cb->setStyleSheet(comboStyle);
            QPalette p = cb->palette();
            p.setColor(QPalette::Button, QColor(inputBg));
            p.setColor(QPalette::Base, QColor(inputBg));
            p.setColor(QPalette::Window, QColor(inputBg));
            p.setColor(QPalette::Text, QColor(fg));
            p.setColor(QPalette::ButtonText, QColor(fg));
            p.setColor(QPalette::Highlight, QColor(selBg));
            p.setColor(QPalette::HighlightedText, QColor(selText));
            cb->setPalette(p);
        }
        for (auto* sb : findChildren<QSpinBox*>()) {
            sb->setStyleSheet(spinStyle);
            QPalette p = sb->palette();
            p.setColor(QPalette::Button, QColor(inputBg));
            p.setColor(QPalette::Base, QColor(inputBg));
            p.setColor(QPalette::Window, QColor(inputBg));
            p.setColor(QPalette::Text, QColor(fg));
            p.setColor(QPalette::ButtonText, QColor(fg));
            p.setColor(QPalette::Highlight, QColor(selBg));
            p.setColor(QPalette::HighlightedText, QColor(selText));
            sb->setPalette(p);
        }
        for (auto* dsb : findChildren<QDoubleSpinBox*>()) {
            dsb->setStyleSheet(spinStyle);
            QPalette p = dsb->palette();
            p.setColor(QPalette::Button, QColor(inputBg));
            p.setColor(QPalette::Base, QColor(inputBg));
            p.setColor(QPalette::Window, QColor(inputBg));
            p.setColor(QPalette::Text, QColor(fg));
            p.setColor(QPalette::ButtonText, QColor(fg));
            p.setColor(QPalette::Highlight, QColor(selBg));
            p.setColor(QPalette::HighlightedText, QColor(selText));
            dsb->setPalette(p);
        }
    }

    setStyleSheet(QString(
        "QWidget { selection-background-color: %10; selection-color: %11; }"
        "QMainWindow { background-color: %1; }"
        "QDockWidget { color: %3; font-weight: bold; }"
        "QDockWidget::title { background-color: %2; padding: 6px; border-bottom: 1px solid %5; }"
        "QGroupBox { border: 1px solid %5; margin-top: 15px; padding-top: 15px; color: %6; font-size: 12px; font-weight: bold; border-radius: 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }"
        "QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox { background-color: %7; border: 1px solid %5; padding: 4px 8px; color: %3; border-radius: 3px; }"
        "QLineEdit:focus, QComboBox:focus { border-color: %6; }"
        "QLineEdit::selection, QComboBox::selection, QSpinBox::selection, QDoubleSpinBox::selection { background: %10; color: %11; }"
        "QComboBox QAbstractItemView { background: %7; selection-background-color: %10; selection-color: %11; }"
        "QPushButton { background-color: %8; border: 1px solid %5; padding: 6px 12px; color: %3; border-radius: 4px; }"
        "QPushButton:hover { background-color: %9; border-color: %6; }"
        "QPushButton:pressed { background-color: %12; color: %3; border-color: %5; }"
        "QPushButton:default { background-color: %8; border-color: %5; }"
        "QPushButton:default:pressed { background-color: %12; border-color: %5; }"
        "QScrollArea { border: none; background-color: %1; }"
    ).arg(bg, panelBg, fg, textSec, border, accent, inputBg, btnBg, btnHover, selBg, selText, btnPressed));

    QString toolbarStyle = theme->toolbarStylesheet();
    if (theme->type() == PCBTheme::Light) {
        QString toolBtnHover = "#f1f5f9";
        QString toolBtnChecked = "#e2e8f0";
        toolbarStyle = QString(
            "QToolBar {"
            "   background: %1;"
            "   border: none;"
            "   border-bottom: 1px solid %2;"
            "   padding: 6px;"
            "   spacing: 6px;"
            "}"
            "QToolButton {"
            "   background-color: transparent;"
            "   border: 1px solid transparent;"
            "   border-radius: 6px;"
            "   padding: 6px;"
            "   color: %7;"
            "}"
            "QToolButton:hover {"
            "   background-color: %3;"
            "   border-color: %2;"
            "}"
            "QToolButton:checked, QToolButton:pressed {"
            "   background-color: %4;"
            "   border: 1px solid %2;"
            "}"
            "QToolBar QComboBox {"
            "   background-color: %5;"
            "   color: %7;"
            "   border: 1px solid %2;"
            "   border-radius: 4px;"
            "   padding: 3px 8px;"
            "}"
            "QToolBar QComboBox QAbstractItemView {"
            "   background: %5;"
            "   color: %7;"
            "   selection-background-color: %4;"
            "   selection-color: %7;"
            "}"
        ).arg(bg, border, toolBtnHover, toolBtnChecked, inputBg, fg, textSec);
    }
    toolbarStyle += QString("QToolButton { color: %1; } QToolButton:checked { color: %1; }")
                        .arg(theme->type() == PCBTheme::Light ? textSec : fg);
    if (m_toolbar) m_toolbar->setStyleSheet(toolbarStyle);
    if (m_leftToolbar) m_leftToolbar->setStyleSheet(toolbarStyle);
    if (m_leftToolbar) {
        for (QAction* action : m_leftToolbar->actions()) {
            const QString iconPath = action ? action->property("iconPath").toString() : QString();
            if (!iconPath.isEmpty()) {
                action->setIcon(getThemeIcon(iconPath));
            }
        }
    }
    
    if (m_statusBar) m_statusBar->setStyleSheet(theme->statusBarStylesheet());
    
    for (auto dock : findChildren<QDockWidget*>()) {
        dock->setStyleSheet(theme->dockStylesheet());
    }

    if (m_libraryTree) {
        m_libraryTree->setStyleSheet(QString(
            "QTreeWidget { background-color: %1; border: 1px solid %2; border-radius: 4px; color: %3; }"
            "QTreeWidget::item { padding: 4px; }"
            "QTreeWidget::item:selected { background-color: %4; color: white; }"
        ).arg(inputBg, border, fg, accent));
    }
    
    if (m_libSearchEdit) {
        m_libSearchEdit->setStyleSheet(QString("QLineEdit { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 4px; }")
            .arg(inputBg, fg, border));
    }

    if (m_pinTable) {
        m_pinTable->setStyleSheet(QString(
            "QTableWidget { background-color: %1; color: %2; gridline-color: %3; border: 1px solid %3; }"
            "QHeaderView::section { background-color: %4; color: %2; padding: 4px; border: 1px solid %3; font-weight: bold; }"
        ).arg(inputBg, fg, border, panelBg));
    }

    if (m_codePreview) {
        m_codePreview->setStyleSheet(QString("background-color: %1; color: %2; border: none;").arg(
            (theme->type() == PCBTheme::Light) ? "#f8fafc" : "#0d1117",
            (theme->type() == PCBTheme::Light) ? "#334155" : "#d1d5db"
        ));
    }
    
    if (m_srcList) {
        m_srcList->setStyleSheet(QString("QListWidget { background-color: %1; color: %2; font-size: 11px; border: none; }").arg(
            (theme->type() == PCBTheme::Light) ? "#f8fafc" : "#0d1117",
            (theme->type() == PCBTheme::Light) ? "#ef4444" : "#ff6b6b"
        ));
    }

    if (m_libPreviewView) {
        m_libPreviewView->setBackgroundBrush(theme->type() == PCBTheme::Light ? QBrush(QColor("#f8fafc")) : QBrush(QColor("#121212")));
        m_libPreviewView->setStyleSheet(QString("background-color: %1; border: 1px solid %2; border-radius: 4px; margin-top: 5px;")
            .arg((theme->type() == PCBTheme::Light) ? "#f8fafc" : "#121212", border));
    }

    if (m_view) {
        m_view->viewport()->update();
    }
}

QColor SymbolEditor::themeLineColor() const {
    PCBTheme* theme = ThemeManager::theme();
    switch (m_colorPreset) {
    case 1: return QColor(245, 248, 255);  // High Contrast
    case 2: return QColor(175, 245, 220);  // Emerald
    case 3: return QColor(255, 214, 148);  // Amber CAD
    case 4: return QColor(220, 220, 220);  // Mono Print
    default: break;                        // Theme
    }
    return theme ? theme->schematicLine() : Qt::white;
}

QColor SymbolEditor::themeTextColor() const {
    PCBTheme* theme = ThemeManager::theme();
    switch (m_colorPreset) {
    case 1: return QColor(255, 255, 255);  // High Contrast
    case 2: return QColor(230, 255, 245);  // Emerald
    case 3: return QColor(255, 240, 212);  // Amber CAD
    case 4: return QColor(245, 245, 245);  // Mono Print
    default: break;                        // Theme
    }
    return theme ? theme->textColor() : QColor(235, 235, 235);
}

QColor SymbolEditor::themePinLabelColor() const {
    PCBTheme* theme = ThemeManager::theme();
    switch (m_colorPreset) {
    case 1: return QColor(86, 186, 255);   // High Contrast
    case 2: return QColor(88, 212, 176);   // Emerald
    case 3: return QColor(255, 184, 96);   // Amber CAD
    case 4: return QColor(200, 200, 200);  // Mono Print
    default: break;                        // Theme
    }
    return theme ? theme->accentColor().lighter(120) : QColor(140, 190, 255);
}

int SymbolEditor::primitiveIndex(QGraphicsItem* item) const {
    while (item) {
        if (item->data(10).toString() == "inherited") return -1; // Block inherited items

        // Robust path: map through the tracked visual list first.
        const int drawnIdx = m_drawnItems.indexOf(item);
        if (drawnIdx >= 0) {
            int inheritedCount = m_symbol.effectivePrimitives().size() - m_symbol.primitives().size();
            int localIdx = drawnIdx - inheritedCount;
            if (localIdx >= 0 && localIdx < m_symbol.primitives().size()) return localIdx;
        }

        bool ok = false;
        int idx = item->data(1).toInt(&ok);
        if (ok) {
            // idx is the global index in m_drawnItems (effectivePrimitives)
            // we need the local index in m_symbol.primitives()
            int inheritedCount = m_symbol.effectivePrimitives().size() - m_symbol.primitives().size();
            int localIdx = idx - inheritedCount;
            if (localIdx >= 0 && localIdx < m_symbol.primitives().size()) return localIdx;
            return -1;
        }
        item = item->parentItem();
    }
    return -1;
}

void SymbolEditor::removeOverlayItems() {
    for (QGraphicsItem* item : m_overlayItems) {
        if (m_scene) m_scene->removeItem(item);
        delete item;
    }
    m_overlayItems.clear();
}

void SymbolEditor::clearResizeHandles() {
    for (QGraphicsRectItem* h : m_resizeHandles) {
        if (!h) continue;
        if (m_scene) m_scene->removeItem(h);
        delete h;
    }
    m_resizeHandles.clear();
}

void SymbolEditor::clearScene() {
    m_overlayItems.clear();
    clearResizeHandles();
    m_drawnItems.clear();
    m_previewItem = nullptr;
    m_polyPoints.clear();
    if (m_scene) {
        m_scene->clear();
    }
}

void SymbolEditor::updateOverlayLabels() {
    removeOverlayItems();

    SymbolDefinition def = symbolDefinition();
    QRectF bounds = def.boundingRect();
    if (bounds.isNull() || bounds.width() < 10)
        bounds = QRectF(-20, -20, 40, 40);

    auto makeLabel = [&](const QString& text, const QColor& color,
                         const QPointF& defaultPos, const QPointF& savedPos, const QString& type) -> QGraphicsSimpleTextItem* {
        auto* lbl = new QGraphicsSimpleTextItem(text);
        lbl->setBrush(color);
        lbl->setFont(QFont("SansSerif", 10, QFont::Bold));
        
        // Use saved position if it's not (0,0), otherwise use default
        if (savedPos != QPointF(0, 0)) {
            lbl->setPos(savedPos);
        } else {
            lbl->setPos(defaultPos);
        }

        lbl->setFlag(QGraphicsItem::ItemIsSelectable, true);
        lbl->setFlag(QGraphicsItem::ItemIsMovable,    true);
        lbl->setData(0, "label");
        lbl->setData(1, type); // "reference" or "name"
        m_scene->addItem(lbl);
        m_overlayItems.append(lbl);
        return lbl;
    };

    makeLabel(def.referencePrefix() + "?",
              themePinLabelColor(),
              QPointF(bounds.left(), bounds.top() - 25),
              def.referencePos(),
              "reference");

    makeLabel(def.name(),
              themeTextColor().lighter(105),
              QPointF(bounds.left(), bounds.bottom() + 5),
              def.namePos(),
              "name");
}

void SymbolEditor::updateResizeHandles() {
    clearResizeHandles();
    if (!m_scene || m_currentTool != Select) return;

    const QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() != 1) return;

    const int idx = primitiveIndex(selected.first());
    if (idx < 0 || idx >= m_symbol.primitives().size()) return;
    const SymbolPrimitive& prim = m_symbol.primitives().at(idx);
    QList<QPair<QString, QPointF>> handles;
    qreal handleSize = 8.0;
    if (prim.type == SymbolPrimitive::Rect || prim.type == SymbolPrimitive::Arc) {
        const qreal x = prim.data.value("x").toDouble();
        const qreal y = prim.data.value("y").toDouble();
        const qreal w = prim.data.contains("width") ? prim.data.value("width").toDouble() : prim.data.value("w").toDouble();
        const qreal h = prim.data.contains("height") ? prim.data.value("height").toDouble() : prim.data.value("h").toDouble();
        QRectF r(x, y, w, h);
        r = r.normalized();
        if (r.isNull()) return;
        const qreal minDim = qMin(r.width(), r.height());
        handleSize = qBound<qreal>(3.0, minDim * 0.22, 8.0);
        const qreal edgeOffset = (minDim < 16.0) ? qBound<qreal>(1.0, handleSize * 0.7, 3.5) : 0.0;
        handles = {
            {"tl", r.topLeft() + QPointF(-edgeOffset, -edgeOffset)},
            {"tr", r.topRight() + QPointF(edgeOffset, -edgeOffset)},
            {"br", r.bottomRight() + QPointF(edgeOffset, edgeOffset)},
            {"bl", r.bottomLeft() + QPointF(-edgeOffset, edgeOffset)}
        };
    } else if (prim.type == SymbolPrimitive::Line) {
        const QPointF p1(prim.data.value("x1").toDouble(), prim.data.value("y1").toDouble());
        const QPointF p2(prim.data.value("x2").toDouble(), prim.data.value("y2").toDouble());
        const qreal len = QLineF(p1, p2).length();
        handleSize = qBound<qreal>(3.0, len * 0.12, 7.5);
        handles = {{"p1", p1}, {"p2", p2}};
    } else if (prim.type == SymbolPrimitive::Circle) {
        const qreal cx = prim.data.contains("centerX") ? prim.data.value("centerX").toDouble() : prim.data.value("cx").toDouble();
        const qreal cy = prim.data.contains("centerY") ? prim.data.value("centerY").toDouble() : prim.data.value("cy").toDouble();
        const qreal r = prim.data.contains("radius") ? prim.data.value("radius").toDouble() : prim.data.value("r").toDouble();
        if (r <= 0.0) return;
        const qreal d = r * 2.0;
        handleSize = qBound<qreal>(3.0, d * 0.22, 7.5);
        // For very small circles place handles slightly outside the perimeter.
        const qreal radialOffset = (r < 10.0) ? qBound<qreal>(1.5, handleSize * 0.9, 3.5) : 0.0;
        const qreal rr = r + radialOffset;
        handles = {
            {"east", QPointF(cx + rr, cy)},
            {"west", QPointF(cx - rr, cy)},
            {"north", QPointF(cx, cy - rr)},
            {"south", QPointF(cx, cy + rr)}
        };
    } else {
        return;
    }

    const qreal hs = handleSize;
    for (const auto& h : handles) {
        auto* handle = new QGraphicsRectItem(h.second.x() - hs / 2.0, h.second.y() - hs / 2.0, hs, hs);
        handle->setBrush(QColor(96, 165, 250));
        handle->setPen(QPen(QColor(255, 255, 255), 1.0));
        handle->setZValue(3000);
        handle->setData(0, "resize_handle");
        handle->setData(1, h.first);
        handle->setData(2, idx);
        handle->setFlag(QGraphicsItem::ItemIsSelectable, false);
        handle->setFlag(QGraphicsItem::ItemIsMovable, false);
        m_scene->addItem(handle);
        m_resizeHandles.append(handle);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  SymbolEditor – Visual factory
// ─────────────────────────────────────────────────────────────────────────────

void SymbolEditor::applyShapeStyle(QAbstractGraphicsShapeItem* shape,
                                   const SymbolPrimitive& prim) const {
    qreal width = prim.data.value("lineWidth").toDouble();
    if (width <= 0.0) width = 1.5;

    Qt::PenStyle penStyle = Qt::SolidLine;
    const QString s = prim.data.value("lineStyle").toString();
    if (s == "Dash")    penStyle = Qt::DashLine;
    else if (s == "Dot")      penStyle = Qt::DotLine;
    else if (s == "DashDot")  penStyle = Qt::DashDotLine;

    shape->setPen(QPen(themeLineColor(), width, penStyle));

    if (prim.data.value("filled").toBool()) {
        QColor fill(prim.data.value("fillColor").toString());
        if (!fill.isValid()) fill = QColor(0, 122, 204, 50);
        shape->setBrush(fill);
    } else {
        shape->setBrush(QColor(255, 255, 255, 15));
    }
}

void SymbolEditor::updateGuideAnchors() {
    QList<QPointF> anchors;

    for (const auto& prim : m_symbol.primitives()) {
        switch (prim.type) {
        case SymbolPrimitive::Pin:
            anchors.append(QPointF(prim.data["x"].toDouble(),
                                   prim.data["y"].toDouble()));
            break;
        case SymbolPrimitive::Line:
            anchors.append(QPointF(prim.data["x1"].toDouble(),
                                   prim.data["y1"].toDouble()));
            anchors.append(QPointF(prim.data["x2"].toDouble(),
                                   prim.data["y2"].toDouble()));
            break;
        case SymbolPrimitive::Rect: {
            double x  = prim.data["x"].toDouble();
            double y  = prim.data["y"].toDouble();
            double w  = prim.data["w"].toDouble();
            double h  = prim.data["h"].toDouble();
            double x2 = x + w, y2 = y + h;
            double mx = x + w / 2.0, my = y + h / 2.0;
            // 4 corners
            anchors.append(QPointF(x,  y));
            anchors.append(QPointF(x2, y));
            anchors.append(QPointF(x,  y2));
            anchors.append(QPointF(x2, y2));
            // 4 edge midpoints + center
            anchors.append(QPointF(mx, y));
            anchors.append(QPointF(mx, y2));
            anchors.append(QPointF(x,  my));
            anchors.append(QPointF(x2, my));
            anchors.append(QPointF(mx, my));
            break;
        }
        case SymbolPrimitive::Circle:
            anchors.append(QPointF(prim.data["cx"].toDouble(),
                                   prim.data["cy"].toDouble()));
            break;
        case SymbolPrimitive::Arc: {
            double x  = prim.data["x"].toDouble();
            double y  = prim.data["y"].toDouble();
            double w  = prim.data["w"].toDouble();
            double h  = prim.data["h"].toDouble();
            anchors.append(QPointF(x + w / 2.0, y + h / 2.0));
            break;
        }
        case SymbolPrimitive::Bezier:
            anchors.append(QPointF(prim.data["x1"].toDouble(),
                                   prim.data["y1"].toDouble()));
            anchors.append(QPointF(prim.data["x4"].toDouble(),
                                   prim.data["y4"].toDouble()));
            break;
        case SymbolPrimitive::Text:
            anchors.append(QPointF(prim.data["x"].toDouble(),
                                   prim.data["y"].toDouble()));
            break;
        case SymbolPrimitive::Polygon: {
            QJsonArray pts = prim.data["points"].toArray();
            for (const auto& v : pts) {
                QJsonObject p = v.toObject();
                anchors.append(QPointF(p["x"].toDouble(),
                                       p["y"].toDouble()));
            }
            break;
        }
        default:
            break;
        }
    }

    if (m_view) m_view->setGuideAnchorPoints(anchors);
}

QGraphicsItem* SymbolEditor::buildVisual(const SymbolPrimitive& prim, int index) const {
    QGraphicsItem* visual = nullptr;
    const QColor lineColor = themeLineColor();
    const QColor textDefaultColor = themeTextColor();
    const QColor pinLabelColor = themePinLabelColor();
    const QPen   defaultPen(lineColor, 1.5);

    switch (prim.type) {

    case SymbolPrimitive::Line: {
        auto* item = new FilteredLineItem(
            QLineF(prim.data.value("x1").toDouble(), prim.data.value("y1").toDouble(),
                   prim.data.value("x2").toDouble(), prim.data.value("y2").toDouble()));
        // Reuse applyShapeStyle logic for lines (no brush needed)
        qreal w = prim.data.value("lineWidth").toDouble();
        if (w <= 0) w = 1.5;
        Qt::PenStyle ps = Qt::SolidLine;
        const QString ls = prim.data.value("lineStyle").toString();
        if (ls == "Dash")    ps = Qt::DashLine;
        else if (ls == "Dot")     ps = Qt::DotLine;
        else if (ls == "DashDot") ps = Qt::DashDotLine;
        item->setPen(QPen(lineColor, w, ps));
        visual = item;
        break;
    }

    case SymbolPrimitive::Rect: {
        const qreal w = prim.data.contains("width")
                      ? prim.data.value("width").toDouble()
                      : prim.data.value("w").toDouble();
        const qreal h = prim.data.contains("height")
                      ? prim.data.value("height").toDouble()
                      : prim.data.value("h").toDouble();
        auto* item = new FilteredRectItem(
            prim.data.value("x").toDouble(),
            prim.data.value("y").toDouble(), w, h);
        applyShapeStyle(item, prim);
        item->setZValue(-1);
        visual = item;
        break;
    }

    case SymbolPrimitive::Circle: {
        // Support both (centerX/centerY) and (cx/cy) and bare (x/y) key variants
        auto dbl = [&](const char* a, const char* b) {
            return prim.data.contains(a)
                 ? prim.data.value(a).toDouble()
                 : prim.data.value(b).toDouble();
        };
        const qreal cx = dbl("centerX", "cx");
        const qreal cy = dbl("centerY", "cy");
        const qreal r  = dbl("radius",  "r");
        auto* item = new FilteredEllipseItem(cx - r, cy - r, r * 2, r * 2);
        applyShapeStyle(item, prim);
        visual = item;
        break;
    }

    case SymbolPrimitive::Arc: {
        // Keys: x, y, width, height, startAngle (degrees*16), spanAngle (degrees*16)
        const qreal x  = prim.data.value("x").toDouble();
        const qreal y  = prim.data.value("y").toDouble();
        const qreal w  = prim.data.contains("width")  ? prim.data.value("width").toDouble()
                                                       : prim.data.value("w").toDouble();
        const qreal h  = prim.data.contains("height") ? prim.data.value("height").toDouble()
                                                       : prim.data.value("h").toDouble();
        const int sa   = prim.data.value("startAngle").toInt(0);
        const int span = prim.data.value("spanAngle").toInt(180 * 16);
        QPainterPath path;
        QRectF rect(x, y, w, h);
        path.arcMoveTo(rect, sa / 16.0);
        path.arcTo(rect, sa / 16.0, span / 16.0);
        auto* item = new FilteredPathItem(path);
        qreal lw = prim.data.value("lineWidth").toDouble();
        if (lw <= 0) lw = 1.5;
        item->setPen(QPen(lineColor, lw));
        visual = item;
        break;
    }

    case SymbolPrimitive::Text: {
        QString rawText = prim.data.value("text").toString();
        QMap<QString, QString> vars;
        vars["REFERENCE"] = m_symbol.referencePrefix() + "?";
        vars["VALUE"]     = m_symbol.defaultValue().isEmpty() ? "Value" : m_symbol.defaultValue();
        vars["NAME"]      = m_symbol.name();
        vars["DATE"]      = QDate::currentDate().toString(Qt::ISODate);
        
        QString resolved = TextResolver::resolve(rawText, vars);
        auto* item = new FilteredSimpleTextItem(resolved);
        const qreal anchorX = prim.data.value("x").toDouble();
        const qreal anchorY = prim.data.value("y").toDouble();
        int fs = prim.data.value("fontSize").toInt(10);
        if (fs <= 0) fs = 10;
        item->setFont(QFont("SansSerif", fs));
        
        QColor c = textDefaultColor;
        if (prim.data.contains("color")) {
            c = QColor(prim.data["color"].toString());
        }
        item->setBrush(c);

        // KiCad importer may provide text justification and rotation.
        const QRectF tb = item->boundingRect();
        qreal dx = 0.0;
        const QString hAlign = prim.data.value("hAlign").toString("left").toLower();
        const QString vAlign = prim.data.value("vAlign").toString("baseline").toLower();
        if (hAlign == "center") dx = -tb.width() * 0.5;
        else if (hAlign == "right") dx = -tb.width();
        qreal py = anchorY;
        if (vAlign == "center") py -= tb.height() * 0.5;
        else if (vAlign == "bottom") py -= tb.height();
        else if (vAlign == "baseline") py -= QFontMetricsF(item->font()).ascent();
        // "top" keeps anchorY as top edge.
        item->setPos(anchorX + dx, py);

        const qreal rotDeg = prim.data.value("rotation").toDouble(0.0);
        if (std::abs(rotDeg) > 1e-6) {
            item->setTransformOriginPoint(0.0, 0.0);
            item->setRotation(-rotDeg); // scene Y is flipped relative to KiCad
        }
        visual = item;
        break;
    }

        case SymbolPrimitive::Pin: {
            auto* group = new FilteredGroupItem();
            const qreal px = prim.data.value("x").toDouble();
            const qreal py = prim.data.value("y").toDouble();
            qreal len = prim.data.value("length").toDouble();
            if (len <= 0) len = 15.0; // match schematic default
    
            bool isVisible = prim.data.value("visible").toBool(true);
            if (!isVisible) group->setOpacity(0.3);
    
            const QString orient = prim.data.value("orientation").toString("Right");

        // endPt = where the lead meets the body edge
        // Convention: orientation string describes the direction the line extends FROM the connection point.
        QPointF endPt;
        if      (orient == "Left")  endPt = QPointF(px - len, py);
        else if (orient == "Up")    endPt = QPointF(px, py - len);
        else if (orient == "Down")  endPt = QPointF(px, py + len);
        else                        endPt = QPointF(px + len, py); // Right

        // Pin body line
        auto* line = new FilteredLineItem(px, py, endPt.x(), endPt.y());
        QPen pinLeadPen(lineColor, 2.0, isVisible ? Qt::SolidLine : Qt::DashLine);
        pinLeadPen.setCapStyle(Qt::FlatCap); // Avoid visual overshoot into body edge
        line->setPen(pinLeadPen);
        group->addToGroup(line);

        // Draw Pin Shapes (Inverted, Clock, etc.)
        QString shape = prim.data.value("pinShape").toString("Line");
        if (shape == "Inverted" || shape == "Inverted Clock" || shape == "Falling Edge Clock") {
            // Circle at the body end of the lead
            qreal cr = 3.0;
            QPointF cPos = endPt;
            auto* circle = new FilteredEllipseItem(cPos.x() - cr, cPos.y() - cr, cr * 2, cr * 2);
            circle->setPen(QPen(lineColor, 1.5));
            circle->setBrush(ThemeManager::theme() ? ThemeManager::theme()->panelBackground() : Qt::black);
            group->addToGroup(circle);
            
            // Shorten the line so it doesn't cross the circle
            QLineF l(px, py, endPt.x(), endPt.y());
            if (l.length() > cr) l.setLength(l.length() - cr);
            line->setLine(l);
        }
        
        if (shape == "Clock" || shape == "Inverted Clock" || shape == "Falling Edge Clock") {
            // Wedge at the body end
            QPointF p1, p2, p3;
            qreal w = 4.0;
            QPointF wedgeBase = endPt;

            if (shape != "Clock") {
                qreal offset = 6.0;
                if      (orient == "Left")  wedgeBase.setX(endPt.x() + offset);
                else if (orient == "Right") wedgeBase.setX(endPt.x() - offset);
                else if (orient == "Up")    wedgeBase.setY(endPt.y() + offset);
                else                        wedgeBase.setY(endPt.y() - offset);
            }

            if (orient == "Left" || orient == "Right") {
                qreal dir = (orient == "Left") ? 1 : -1;
                p1 = wedgeBase + QPointF(0, -w);
                p2 = wedgeBase + QPointF(dir * w, 0);
                p3 = wedgeBase + QPointF(0, w);
            } else {
                qreal dir = (orient == "Up") ? 1 : -1;
                p1 = wedgeBase + QPointF(-w, 0);
                p2 = wedgeBase + QPointF(0, dir * w);
                p3 = wedgeBase + QPointF(w, 0);
            }
            auto* wedge = new FilteredPathItem();
            QPainterPath wp; wp.moveTo(p1); wp.lineTo(p2); wp.lineTo(p3);
            wedge->setPath(wp);
            wedge->setPen(QPen(lineColor, 1.5));
            group->addToGroup(wedge);
        }

        if (shape == "Input Low" || shape == "Output Low") {
            // Small L-shape or bar (simplified as small offset line)
            // OrCAD/KiCad style: inverted circle is most common for active-low.
            // For now let's use a small dot or specific bar if requested.
        }

        // Connection-point dot at pin tip (px, py), matching Schematic Editor style
        QColor dotBrush = ThemeManager::theme() ? ThemeManager::theme()->schematicComponent() : QColor(12, 12, 12);
        auto* dot = new FilteredEllipseItem(px - 2.5, py - 2.5, 5.0, 5.0);
        dot->setBrush(dotBrush);
        dot->setPen(QPen(lineColor, 2.0));
        group->addToGroup(dot);

        // Pin number parsing
        QJsonValue numVal = prim.data["number"];
        if (numVal.isUndefined()) numVal = prim.data["num"];
        QString numStr = numVal.isString() ? numVal.toString() : QString::number(numVal.toInt());
        
        QColor textColor = pinLabelColor;

        // Pin number label
        QString fullNumStr = numStr;
        QString stacked = prim.data.value("stackedNumbers").toString();
        if (!stacked.isEmpty()) {
            int count = stacked.split(",", Qt::SkipEmptyParts).size();
            fullNumStr += QString(" [+%1]").arg(count);
        }

        auto* numLabel = new FilteredSimpleTextItem(fullNumStr);
        numLabel->setBrush(textColor);
        int nsz = prim.data.value("numSize").toInt(7);
        numLabel->setFont(QFont("Monospace", nsz > 0 ? nsz : 7));
        numLabel->setParentItem(group);

        // Pin name label
        QString nameStr = prim.data.value("name").toString();
        // Strip KiCad overline wrappers to get clean name
        nameStr.replace("~{", "");
        nameStr.replace("}", "");
        if (nameStr.startsWith("~")) nameStr = nameStr.mid(1);
        if (nameStr.startsWith("\\overline{") && nameStr.endsWith("}")) {
            nameStr = nameStr.mid(10, nameStr.length() - 11);
        }

        auto* nameLabel = new FilteredSimpleTextItem(nameStr);
        nameLabel->setBrush(textColor);
        int asz = prim.data.value("nameSize").toInt(7);
        nameLabel->setFont(QFont("SansSerif", asz > 0 ? asz : 7));
        nameLabel->setParentItem(group);

        if (prim.data.value("hideNum").toBool()) numLabel->hide();
        if (prim.data.value("hideName").toBool()) nameLabel->hide();

        // Position labels: number centered along lead, name inside body (at endPt side)
        const QRectF nb = nameLabel->boundingRect();
        const QRectF numB = numLabel->boundingRect();
        
        if (orient == "Left") {
            // Line extends left from px: number centered
            numLabel->setPos(px - len/2.0 - numB.width()/2.0, py - numB.height() - 2);
            nameLabel->setPos(endPt.x() - nb.width() - 4, py - nb.height()/2.0);
        } else if (orient == "Right") {
            // Line extends right from px: number centered
            numLabel->setPos(px + len/2.0 - numB.width()/2.0, py - numB.height() - 2);
            nameLabel->setPos(endPt.x() + 4, py - nb.height()/2.0);
        } else if (orient == "Up") {
            // Line extends up from py
            numLabel->setPos(px + 4, py - len/2.0 - numB.height()/2.0);
            nameLabel->setTransformOriginPoint(nb.center());
            nameLabel->setRotation(-90);
            nameLabel->setPos(endPt.x() - nb.height()/2.0, endPt.y() - nb.width() - 4);
        } else if (orient == "Down") {
            // Line extends down from py
            numLabel->setPos(px + 4, py + len/2.0 - numB.height()/2.0);
            nameLabel->setTransformOriginPoint(nb.center());
            nameLabel->setRotation(-90);
            nameLabel->setPos(endPt.x() - nb.height()/2.0, endPt.y() + 4);
        }

        visual = group;
        break;
    }

    case SymbolPrimitive::Polygon: {
        QPolygonF poly;
        const QJsonArray pts = prim.data.value("points").toArray();
        poly.reserve(pts.size());
        for (const auto& v : pts) {
            const QJsonObject o = v.toObject();
            poly.append(QPointF(o.value("x").toDouble(), o.value("y").toDouble()));
        }
        auto* item = new FilteredPolygonItem(poly);
        applyShapeStyle(item, prim);
        visual = item;
        break;
    }

    case SymbolPrimitive::Bezier: {
        QPointF p1(prim.data["x1"].toDouble(), prim.data["y1"].toDouble());
        QPointF p2(prim.data["x2"].toDouble(), prim.data["y2"].toDouble());
        QPointF p3(prim.data["x3"].toDouble(), prim.data["y3"].toDouble());
        QPointF p4(prim.data["x4"].toDouble(), prim.data["y4"].toDouble());
        
        QPainterPath path;
        path.moveTo(p1);
        path.cubicTo(p2, p3, p4);
        
        auto* item = new FilteredPathItem(path);
        qreal lw = prim.data.value("lineWidth").toDouble();
        if (lw <= 0) lw = 1.5;
        item->setPen(QPen(lineColor, lw));
        visual = item;
        break;
    }

    case SymbolPrimitive::Image: {
        QByteArray ba = QByteArray::fromBase64(prim.data["image"].toString().toLatin1());
        QPixmap pix;
        pix.loadFromData(ba);
        if (pix.isNull()) break;

        qreal x = prim.data["x"].toDouble();
        qreal y = prim.data["y"].toDouble();
        qreal w = prim.data.contains("width") ? prim.data["width"].toDouble() : prim.data["w"].toDouble();
        qreal h = prim.data.contains("height") ? prim.data["height"].toDouble() : prim.data["h"].toDouble();

        auto* item = new FilteredPixmapItem(pix.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        item->setPos(x, y);
        visual = item;
        break;
    }

    default:
        break;
    }

    if (visual) {
        visual->setFlag(QGraphicsItem::ItemIsSelectable);
        visual->setFlag(QGraphicsItem::ItemIsMovable);
        visual->setData(1, index);
        if (prim.type == SymbolPrimitive::Pin) {
            visual->setData(2, "pin");
            visual->setData(3, prim.data.value("x").toDouble());
            visual->setData(4, prim.data.value("y").toDouble());
        }
    }
    return visual;
}

// ─────────────────────────────────────────────────────────────────────────────
//  SymbolEditor – setSymbolDefinition / applySymbolDefinition
// ─────────────────────────────────────────────────────────────────────────────

void SymbolEditor::setSymbolDefinition(const SymbolDefinition& def) {
    m_undoStack->clear();
    applySymbolDefinition(def);
    markSubcktMappingSynchronized();
}

void SymbolEditor::applySymbolDefinition(const SymbolDefinition& def) {
    if (m_livePreview) m_livePreview->setSymbol(def);
    // 1. Capture current selection indices
    QList<int> selectedIndices;
    for (QGraphicsItem* item : m_scene->selectedItems()) {
        int idx = primitiveIndex(item);
        if (idx != -1) selectedIndices.append(idx);
    }

    // 2. Clear and rebuild scene
    clearScene();
    m_symbol = def;

    // Sync UI metadata
    m_nameEdit->setText(def.name());
    m_descriptionEdit->setText(def.description());
    m_categoryCombo->setCurrentText(def.category());
    m_prefixEdit->setText(def.referencePrefix());
    m_footprintEdit->setText(def.defaultFootprint());
    {
        const QString src = def.modelSource().isEmpty() ? "none" : def.modelSource().toLower();
        int srcIdx = m_modelSourceCombo->findData(src);
        if (srcIdx < 0) srcIdx = m_modelSourceCombo->findData("none");
        m_modelSourceCombo->setCurrentIndex(srcIdx >= 0 ? srcIdx : 0);
    }
    m_modelPathEdit->setText(def.modelPath());
    m_modelNameEdit->setText(def.modelName());

    // Update Unit Selector if needed
    if (m_unitCombo) {
        m_unitCombo->blockSignals(true);
        int prevUnit = m_currentUnit;
        m_unitCombo->clear();
        m_unitCombo->addItem("All Units", 0);
        for (int u = 1; u <= def.unitCount(); ++u) {
            m_unitCombo->addItem(QString("Unit %1").arg(QChar('A' + u - 1)), u);
        }
        int idx = m_unitCombo->findData(prevUnit);
        m_unitCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        m_currentUnit = m_unitCombo->currentData().toInt();
        m_unitCombo->blockSignals(false);
    }

    // Rebuild primitive visuals
    QList<SymbolPrimitive> effective = def.effectivePrimitives();
    QList<SymbolPrimitive> local = def.primitives();
    
    m_drawnItems.reserve(effective.size());
    for (int i = 0; i < effective.size(); ++i) {
        // Filter by unit
        if (m_currentUnit != 0 && effective[i].unit() != 0 && effective[i].unit() != m_currentUnit) {
            continue;
        }
        
        // Filter by style
        if (m_currentStyle != 0 && effective[i].bodyStyle() != 0 && effective[i].bodyStyle() != m_currentStyle) {
            continue;
        }

        // Is this primitive local or inherited?
        bool isInherited = (i < (effective.size() - local.size()));
        
        QGraphicsItem* visual = buildVisual(effective[i], i);
        if (visual) {
            if (isInherited) {
                visual->setOpacity(0.5);
                visual->setFlag(QGraphicsItem::ItemIsSelectable, false);
                visual->setFlag(QGraphicsItem::ItemIsMovable, false);
                visual->setData(10, "inherited"); // Tag for logic
            }
            m_scene->addItem(visual);
            m_drawnItems.append(visual);
            
            // 3. Restore selection (only for local items)
            if (!isInherited && selectedIndices.contains(i - (effective.size() - local.size()))) {
                visual->setSelected(true);
            }
        }
    }

    updateOverlayLabels();
    updateCodePreview();
    updatePinTable();
    updateSubcktMappingTable();
    updateGuideAnchors();
    updateResizeHandles();
}

SymbolDefinition SymbolEditor::symbolDefinition() const {
    SymbolDefinition def = m_symbol;
    def.setName(m_nameEdit->text());
    def.setDescription(m_descriptionEdit->text());
    def.setCategory(m_categoryCombo->currentText());
    def.setReferencePrefix(m_prefixEdit->text());
    def.setModelSource(m_modelSourceCombo->currentData().toString());
    def.setModelPath(m_modelPathEdit->text());
    def.setModelName(m_modelNameEdit->text());
    if (m_subcktMappingTable) {
        QMap<int, QString> mapping;
        for (int row = 0; row < m_subcktMappingTable->rowCount(); ++row) {
            QTableWidgetItem* pinNumberItem = m_subcktMappingTable->item(row, 0);
            QTableWidgetItem* subcktPinItem = m_subcktMappingTable->item(row, 2);
            if (!pinNumberItem || !subcktPinItem) continue;
            const int pinNumber = pinNumberItem->text().toInt();
            const QString subcktPin = subcktPinItem->text().trimmed();
            if (pinNumber > 0 && !subcktPin.isEmpty()) {
                mapping.insert(pinNumber, subcktPin);
            }
        }
        def.setSpiceNodeMapping(mapping);
    }
    return def;
}

QStringList SymbolEditor::currentSymbolPinNames() const {
    QStringList pinNames;
    QList<const SymbolPrimitive*> pinPrimitives;
    for (const SymbolPrimitive& prim : m_symbol.primitives()) {
        if (prim.type == SymbolPrimitive::Pin) pinPrimitives.append(&prim);
    }

    std::sort(pinPrimitives.begin(), pinPrimitives.end(), [](const SymbolPrimitive* a, const SymbolPrimitive* b) {
        return a->data.value("number").toInt() < b->data.value("number").toInt();
    });

    for (const SymbolPrimitive* prim : pinPrimitives) {
        pinNames << prim->data.value("name").toString().trimmed();
    }
    return pinNames;
}

QString SymbolEditor::currentSymbolPinSignature() const {
    QStringList parts;
    QList<const SymbolPrimitive*> pinPrimitives;
    for (const SymbolPrimitive& prim : m_symbol.primitives()) {
        if (prim.type == SymbolPrimitive::Pin) pinPrimitives.append(&prim);
    }

    std::sort(pinPrimitives.begin(), pinPrimitives.end(), [](const SymbolPrimitive* a, const SymbolPrimitive* b) {
        return a->data.value("number").toInt() < b->data.value("number").toInt();
    });

    for (const SymbolPrimitive* prim : pinPrimitives) {
        parts << QString("%1:%2")
                     .arg(prim->data.value("number").toInt())
                     .arg(prim->data.value("name").toString().trimmed());
    }
    return parts.join("|");
}

void SymbolEditor::markSubcktMappingSynchronized() {
    m_subcktMappingPinSignature = currentSymbolPinSignature();
    refreshSubcktMappingStatus();
}

QStringList SymbolEditor::currentSubcktPinNames() const {
    QStringList pins;
    const QString modelName = m_modelNameEdit ? m_modelNameEdit->text().trimmed() : QString();
    if (modelName.isEmpty()) return pins;

    const SimSubcircuit* sub = ModelLibraryManager::instance().findSubcircuit(modelName);
    if (!sub) return pins;
    for (const std::string& pin : sub->pinNames) {
        pins << QString::fromStdString(pin);
    }
    return pins;
}

QStringList SymbolEditor::buildSmartSubcktMapping(const QStringList& subcktPins) const {
    QStringList mapping;
    const QStringList symbolPins = currentSymbolPinNames();
    for (int i = 0; i < symbolPins.size(); ++i) mapping << QString();

    QMap<QString, int> subcktIndexByNormalized;
    for (int i = 0; i < subcktPins.size(); ++i) {
        const QString normalized = normalizePinNameForStatus(subcktPins.at(i));
        if (!normalized.isEmpty() && !subcktIndexByNormalized.contains(normalized)) {
            subcktIndexByNormalized.insert(normalized, i);
        }
    }

    QSet<int> usedSubcktIndexes;
    for (int i = 0; i < symbolPins.size(); ++i) {
        const QString normalized = normalizePinNameForStatus(symbolPins.at(i));
        if (normalized.isEmpty()) continue;
        auto it = subcktIndexByNormalized.constFind(normalized);
        if (it != subcktIndexByNormalized.constEnd() && !usedSubcktIndexes.contains(it.value())) {
            mapping[i] = subcktPins.at(it.value());
            usedSubcktIndexes.insert(it.value());
        }
    }

    for (int i = 0; i < mapping.size() && i < subcktPins.size(); ++i) {
        if (mapping[i].isEmpty() && !usedSubcktIndexes.contains(i)) {
            mapping[i] = subcktPins.at(i);
            usedSubcktIndexes.insert(i);
        }
    }

    return mapping;
}

void SymbolEditor::applySubcktMappingList(const QStringList& mappedPins) {
    QMap<int, QString> mapping;
    QList<const SymbolPrimitive*> pinPrimitives;
    for (const SymbolPrimitive& prim : m_symbol.primitives()) {
        if (prim.type == SymbolPrimitive::Pin) pinPrimitives.append(&prim);
    }

    std::sort(pinPrimitives.begin(), pinPrimitives.end(), [](const SymbolPrimitive* a, const SymbolPrimitive* b) {
        return a->data.value("number").toInt() < b->data.value("number").toInt();
    });

    for (int i = 0; i < pinPrimitives.size(); ++i) {
        const int pinNumber = pinPrimitives.at(i)->data.value("number").toInt();
        const QString pinName = pinPrimitives.at(i)->data.value("name").toString().trimmed();
        const QString mappedPin = (i < mappedPins.size()) ? mappedPins.at(i).trimmed() : QString();
        mapping.insert(pinNumber, mappedPin.isEmpty() ? pinName : mappedPin);
    }

    m_symbol.setSpiceNodeMapping(mapping);
    updateSubcktMappingTable();
    markSubcktMappingSynchronized();
    updateCodePreview();
}

void SymbolEditor::openSubcircuitPicker() {
    const QString currentModel = m_modelNameEdit ? m_modelNameEdit->text().trimmed() : QString();
    SubcircuitPickerDialog dlg(currentModel, currentSymbolPinNames(), this);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString selected = dlg.selectedModel().trimmed();
    if (selected.isEmpty()) return;

    if (m_modelNameEdit) m_modelNameEdit->setText(selected);
    if (m_modelSourceCombo && m_modelPathEdit) {
        const QString libraryPath = ModelLibraryManager::instance().findLibraryPath(selected).trimmed();
        if (!libraryPath.isEmpty()) {
            const auto binding = classifyModelBindingPath(libraryPath, m_projectKey);
            const int sourceIndex = m_modelSourceCombo->findData(binding.first);
            if (sourceIndex >= 0) m_modelSourceCombo->setCurrentIndex(sourceIndex);
            m_modelPathEdit->setText(binding.second);
        }
    }

    if (dlg.applySmartMapRequested() || dlg.applyByOrderRequested()) {
        const QStringList subcktPins = dlg.applySmartMapRequested()
            ? dlg.selectedSuggestedMapping()
            : dlg.selectedSubcktPins();
        applySubcktMappingList(subcktPins);
    }

    updateCodePreview();
}

void SymbolEditor::onApplySmartSubcktMapping() {
    const QStringList subcktPins = currentSubcktPinNames();
    if (subcktPins.isEmpty()) {
        QMessageBox::information(this, "Smart Map", "Select a valid subcircuit model before applying smart mapping.");
        return;
    }
    applySubcktMappingList(buildSmartSubcktMapping(subcktPins));
}

void SymbolEditor::onApplyOrderedSubcktMapping() {
    const QStringList subcktPins = currentSubcktPinNames();
    if (subcktPins.isEmpty()) {
        QMessageBox::information(this, "Map By Order", "Select a valid subcircuit model before applying ordered mapping.");
        return;
    }
    applySubcktMappingList(subcktPins);
}

void SymbolEditor::onClearSubcktMapping() {
    QMap<int, QString> mapping;
    QList<const SymbolPrimitive*> pinPrimitives;
    for (const SymbolPrimitive& prim : m_symbol.primitives()) {
        if (prim.type == SymbolPrimitive::Pin) pinPrimitives.append(&prim);
    }
    std::sort(pinPrimitives.begin(), pinPrimitives.end(), [](const SymbolPrimitive* a, const SymbolPrimitive* b) {
        return a->data.value("number").toInt() < b->data.value("number").toInt();
    });
    for (const SymbolPrimitive* prim : pinPrimitives) {
        const int pinNumber = prim->data.value("number").toInt();
        const QString pinName = prim->data.value("name").toString().trimmed();
        mapping.insert(pinNumber, pinName);
    }
    m_symbol.setSpiceNodeMapping(mapping);
    updateSubcktMappingTable();
    markSubcktMappingSynchronized();
    updateCodePreview();
}

void SymbolEditor::onAIDatasheetImport() {
    AIDatasheetImportDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        const auto pins = dlg.generatedPins();
        if (!pins.isEmpty()) {
            const SymbolDefinition oldDef = m_symbol;
            for (const auto& pin : pins) {
                m_symbol.addPrimitive(pin);
            }
            // Mark as modified and refresh preview/view
            applySymbolDefinition(m_symbol);
            m_undoStack->push(new UpdateSymbolCommand(this, oldDef, m_symbol, tr("AI Pin Generation")));
        }
    }
}

void SymbolEditor::onImportSpiceSubcircuit() {
    SpiceSubcircuitImportDialog dlg(m_projectKey, QString(), this);
    if (dlg.exec() != QDialog::Accepted) return;

    const auto res = dlg.result();
    SymbolDefinition imported = buildImportedSubcktSymbolForEditor(res);
    const SymbolDefinition oldDef = m_symbol;
    applySymbolDefinition(imported);
    m_undoStack->clear();
    m_undoStack->setClean();
    m_lastSaveTarget = SaveTarget::None;
    statusBar()->showMessage(QString("Imported SPICE subcircuit %1 into Symbol Editor").arg(res.subcktName), 4000);
}

// ─────────────────────────────────────────────────────────────────────────────
//  SymbolEditor – UI Setup
// ─────────────────────────────────────────────────────────────────────────────

QIcon SymbolEditor::getThemeIcon(const QString& path, bool tinted) {
    QIcon icon(path);
    if (!tinted || !ThemeManager::theme()) {
        return icon;
    }

    // Use a larger pixmap for better scaling/High-DPI support
    QPixmap pixmap = icon.pixmap(QSize(64, 64));
    if (pixmap.isNull()) return icon;

    QPainter painter(&pixmap);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), ThemeManager::theme()->textColor());
    painter.end();
    return QIcon(pixmap);
}

void SymbolEditor::setupUI() {
    setWindowTitle("Symbol Editor");
    resize(1280, 850);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(800, 600);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

    // Ensure standard window decorations and working maximize/minimize buttons (XFCE needs explicit hints)
    setWindowFlags(windowFlags()
                   | Qt::Window
                   | Qt::WindowTitleHint
                   | Qt::WindowSystemMenuHint
                   | Qt::WindowMaximizeButtonHint
                   | Qt::WindowMinimizeButtonHint
                   | Qt::WindowCloseButtonHint);

    // ── Main Canvas ─────────────────────────────────────────────────────────
    m_scene = new QGraphicsScene(this);
    m_scene->setSceneRect(-500, -500, 1000, 1000);
    connect(m_scene, &QGraphicsScene::selectionChanged, this, &SymbolEditor::onSelectionChanged);

    m_view = new SymbolEditorView();
    m_view->setScene(m_scene);
    m_view->setGridSize(15.0); // Match initial UI default
    setCentralWidget(m_view);

    connectViewSignals();

    // ── Menus & Toolbars ───────────────────────────────────────────────────
    createMenuBar();
    createToolBar();
    addToolBar(Qt::TopToolBarArea, m_toolbar);
    addToolBar(Qt::LeftToolBarArea, m_leftToolbar);

    // ── Docks ───────────────────────────────────────────────────────────────
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks | QMainWindow::VerticalTabs);

    // ZONE 1: THE INSPECTOR (Right Side - Properties & Metadata)
    // ---------------------------------------------------------
    createSymbolInfoPanel();
    // ... metadata setup ...
    auto* infoContainer = new QWidget();
    auto* infoLayout = new QVBoxLayout(infoContainer);
    auto* infoGroup = new QGroupBox("Identity");
    auto* infoForm = new QFormLayout(infoGroup);
    infoForm->addRow("Name:", m_nameEdit);
    infoForm->addRow("Prefix:", m_prefixEdit);
    infoForm->addRow("Category:", m_categoryCombo);
    
    QHBoxLayout* fpLayout = new QHBoxLayout();
    m_footprintEdit = new QLineEdit();
    m_footprintEdit->setPlaceholderText("Associated Footprint");
    QPushButton* fpBrowse = new QPushButton("...");
    fpBrowse->setFixedWidth(30);
    fpLayout->addWidget(m_footprintEdit);
    fpLayout->addWidget(fpBrowse);
    infoForm->addRow("Footprint:", fpLayout);
    connect(fpBrowse, &QPushButton::clicked, this, &SymbolEditor::onBrowseFootprint);
    infoForm->addRow("Desc:", m_descriptionEdit);
    infoLayout->addWidget(infoGroup);

    auto* modelGroup = new QGroupBox("Model Binding");
    auto* modelForm = new QFormLayout(modelGroup);
    modelForm->addRow("Source:", m_modelSourceCombo);

    QHBoxLayout* modelPathLayout = new QHBoxLayout();
    QPushButton* modelBrowse = new QPushButton("...");
    modelBrowse->setFixedWidth(30);
    modelPathLayout->addWidget(m_modelPathEdit);
    modelPathLayout->addWidget(modelBrowse);
    modelForm->addRow("Model File:", modelPathLayout);
    QHBoxLayout* modelNameLayout = new QHBoxLayout();
    QPushButton* pickSubckt = new QPushButton("Pick...");
    modelNameLayout->addWidget(m_modelNameEdit);
    modelNameLayout->addWidget(pickSubckt);
    modelForm->addRow("Model Name:", modelNameLayout);
    infoLayout->addWidget(modelGroup);

    connect(modelBrowse, &QPushButton::clicked, this, [this]() {
        QString path = runThemedOpenFileDialog(this, "Select Model File", "SPICE Models (*.lib *.sub *.cmp *.cir *.sp *.txt);;All Files (*)");
        if (!path.isEmpty()) {
            m_modelPathEdit->setText(path);
            tryAutoDetectModelName();
        }
    });

    connect(pickSubckt, &QPushButton::clicked, this, &SymbolEditor::openSubcircuitPicker);

    auto updateModelPathState = [this]() {
        const QString src = m_modelSourceCombo->currentData().toString();
        const bool enablePath = (src != "none");
        m_modelPathEdit->setEnabled(enablePath);
    };
    connect(m_modelSourceCombo, &QComboBox::currentIndexChanged, this, updateModelPathState);
    updateModelPathState();

    auto* actionGroup = new QGroupBox("Component Actions");
    auto* actionLayout = new QVBoxLayout(actionGroup);
    
    auto* placeBtn = new QPushButton("Place in Schematic");
    connect(placeBtn, &QPushButton::clicked, this, &SymbolEditor::onPlaceInSchematic);
    actionLayout->addWidget(placeBtn);

    auto* importSubcktBtn = new QPushButton("Import SPICE Subcircuit");
    connect(importSubcktBtn, &QPushButton::clicked, this, &SymbolEditor::onImportSpiceSubcircuit);
    actionLayout->addWidget(importSubcktBtn);

    auto* imgBtn = new QPushButton("Import Image");
    connect(imgBtn, &QPushButton::clicked, this, &SymbolEditor::onImportImage);
    actionLayout->addWidget(imgBtn);

    auto* fieldsBtn = new QPushButton("Custom Fields");
    connect(fieldsBtn, &QPushButton::clicked, this, &SymbolEditor::onManageCustomFields);
    actionLayout->addWidget(fieldsBtn);
    infoLayout->addWidget(actionGroup);
    infoLayout->addStretch();
    // Info panel is embedded into the right-side tab set (no separate dock).

    // Create single Properties dock with tabs on the right side
    m_propsDock = new QDockWidget("Properties", this);
    m_propsDock->setObjectName("PropertiesDock");
    
    m_propsTabWidget = new QTabWidget();
    m_propsTabWidget->setTabPosition(QTabWidget::East);
    m_propsTabWidget->setDocumentMode(true);
    m_propsTabWidget->tabBar()->setExpanding(false);
    m_propsTabWidget->tabBar()->setUsesScrollButtons(true);
    m_propsTabWidget->tabBar()->setElideMode(Qt::ElideRight);
    m_propsTabWidget->tabBar()->setFixedWidth(28);
    
    // Apply styling for vertical tabs on right
    bool isLightTheme = ThemeManager::theme() && ThemeManager::theme()->type() == PCBTheme::Light;
    QString tabStyle;
    if (isLightTheme) {
        tabStyle = R"(
            QTabWidget::pane { border: 1px solid #cbd5e1; border-right: none; background-color: #ffffff; }
            QTabBar::tab {
                background-color: #f1f5f9;
                color: #64748b;
                border: 1px solid #cbd5e1;
                border-left: none;
                border-top-right-radius: 4px;
                border-bottom-right-radius: 4px;
                padding: 2px 2px;
                min-width: 24px;
                min-height: 64px;
                font-weight: 600;
                margin: 0;
            }
            QTabBar::tab:selected {
                background-color: #ffffff;
                color: #1f2937;
                border-right: 3px solid #10b981;
            }
            QTabBar::tab:hover:!selected {
                background-color: #e2e8f0;
            }
        )";
    } else {
        tabStyle = R"(
            QTabWidget::pane { border: 1px solid #3f3f46; border-right: none; background-color: #1e1e1e; }
            QTabBar::tab {
                background-color: #2d2d2d;
                color: #9ca3af;
                border: 1px solid #3f3f46;
                border-left: none;
                border-top-right-radius: 4px;
                border-bottom-right-radius: 4px;
                padding: 2px 2px;
                min-width: 24px;
                min-height: 64px;
                font-weight: 600;
                margin: 0;
            }
            QTabBar::tab:selected {
                background-color: #1e1e1e;
                color: #e5e7eb;
                border-right: 3px solid #10b981;
            }
            QTabBar::tab:hover:!selected {
                background-color: #3f3f46;
            }
        )";
    }
    m_propsTabWidget->setStyleSheet(tabStyle);
    
    // Tab 1: Selection Properties
    m_propertyEditor = new PropertyEditor();
    connect(m_propertyEditor, &PropertyEditor::propertyChanged, this, &SymbolEditor::onPropertyChanged);
    m_propsTabWidget->addTab(m_propertyEditor, "Selection");
    
    // Tab 2: Symbol Metadata (editable)
    m_propsTabWidget->addTab(infoContainer, "Metadata");
    
    // Tab 3: Gemini Assistant
    auto* aiWidget = new QWidget();
    auto* aiLayout = new QVBoxLayout(aiWidget);
    aiLayout->setContentsMargins(0, 0, 0, 0);
    m_aiPanel = new GeminiPanel(m_scene, this);
    m_aiPanel->setMode("symbol");
    connect(m_aiPanel, &GeminiPanel::symbolJsonGenerated, this, &SymbolEditor::onAiSymbolGenerated);
    aiLayout->addWidget(m_aiPanel);
    m_propsTabWidget->addTab(aiWidget, "Gemini AI");
    
    // Tab 4: Live Preview
    m_livePreview = new SymbolPreviewWidget();
    m_propsTabWidget->addTab(m_livePreview, "Live Preview");
    
    m_propsDock->setWidget(m_propsTabWidget);
    addDockWidget(Qt::RightDockWidgetArea, m_propsDock);
    m_propsDock->raise();

    // ZONE 2: THE NAVIGATOR (Left Side - Assets & Wizards)
    // ----------------------------------------------------
    auto* libDock = new QDockWidget("Library Browser", this);
    libDock->setObjectName("LibraryDock");
    createLibraryBrowser();
    auto* libContainer = new QWidget();
    auto* libLayout = new QVBoxLayout(libContainer);
    libLayout->addWidget(m_libSearchEdit);
    libLayout->addWidget(m_libraryTree);
    libDock->setWidget(libContainer);
    addDockWidget(Qt::LeftDockWidgetArea, libDock);

    auto* wizDock = new QDockWidget("IC Wizard", this);
    wizDock->setObjectName("WizardDock");
    createWizardPanel();
    auto* wizContainer = new QWidget();
    auto* wizLayout = new QVBoxLayout(wizContainer);
    auto* browseGroup = new QGroupBox("Template Browser");
    auto* browseLayout = new QVBoxLayout(browseGroup);
    auto* browseForm = new QFormLayout();
    browseForm->addRow("Search:", m_wizardTemplateSearchEdit);
    browseForm->addRow("Template:", m_wizardTemplateCombo);
    browseLayout->addLayout(browseForm);
    browseLayout->addWidget(m_wizardTemplateInfoLabel);
    browseLayout->addWidget(m_wizardTemplateDescLabel);
    browseLayout->addWidget(m_wizardPreviewView);
    wizLayout->addWidget(browseGroup);

    auto* applyTplBtn = new QPushButton("Apply Template");
    connect(applyTplBtn, &QPushButton::clicked, this, &SymbolEditor::onWizardApplyTemplate);
    wizLayout->addWidget(applyTplBtn);
    auto* wizBtn = new QPushButton("Generate Symbol");
    connect(wizBtn, &QPushButton::clicked, this, &SymbolEditor::onWizardGenerate);
    wizLayout->addWidget(wizBtn);
    auto* saveTplBtn = new QPushButton("Save Current as Template");
    connect(saveTplBtn, &QPushButton::clicked, this, &SymbolEditor::onWizardSaveTemplate);
    wizLayout->addWidget(saveTplBtn);
    auto* importBtn = new QPushButton("Import KiCad Symbol");
    connect(importBtn, &QPushButton::clicked, this, &SymbolEditor::onImportKicadSymbol);
    wizLayout->addWidget(importBtn);
    auto* importLtBtn = new QPushButton("Import LTspice Symbol");
    connect(importLtBtn, &QPushButton::clicked, this, &SymbolEditor::onImportLtspiceSymbol);
    wizLayout->addWidget(importLtBtn);
    auto* importSubcktBtn2 = new QPushButton("Import SPICE Subcircuit");
    connect(importSubcktBtn2, &QPushButton::clicked, this, &SymbolEditor::onImportSpiceSubcircuit);
    wizLayout->addWidget(importSubcktBtn2);
    wizLayout->addStretch();
    wizDock->setWidget(wizContainer);
    addDockWidget(Qt::LeftDockWidgetArea, wizDock);

    tabifyDockWidget(libDock, wizDock);
    libDock->raise();

    // ZONE 3: THE CONSOLE (Bottom - Data & Validation)
    // ------------------------------------------------
    auto* pinDock = new QDockWidget("Pin Management", this);
    pinDock->setObjectName("PinDock");
    createPinTable();
    auto* pinContainer = new QWidget();
    auto* pinLayout = new QVBoxLayout(pinContainer);
    pinLayout->setContentsMargins(4, 4, 4, 4);
    pinLayout->addWidget(m_pinTable);
    pinDock->setWidget(pinContainer);
    addDockWidget(Qt::BottomDockWidgetArea, pinDock);

    auto* codeDock = new QDockWidget("JSON Source", this);
    codeDock->setObjectName("CodeDock");
    m_codePreview = new QTextEdit();
    m_codePreview->setReadOnly(true);
    m_codePreview->setFont(QFont("Monospace", 9));
    codeDock->setWidget(m_codePreview);
    addDockWidget(Qt::BottomDockWidgetArea, codeDock);

    auto* srcDock = new QDockWidget("Rule Checker", this);
    srcDock->setObjectName("SRCDock");
    m_srcList = new QListWidget();
    connect(m_srcList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        int idx = item->data(Qt::UserRole).toInt();
        if (idx >= 0 && idx < m_drawnItems.size()) {
            m_scene->clearSelection();
            m_drawnItems[idx]->setSelected(true);
            m_view->centerOn(m_drawnItems[idx]);
        }
    });
    srcDock->setWidget(m_srcList);
    addDockWidget(Qt::BottomDockWidgetArea, srcDock);

    tabifyDockWidget(pinDock, codeDock);
    tabifyDockWidget(codeDock, srcDock);
    pinDock->raise();

    m_statusBar = new QStatusBar(this);
    pinLayout->setSpacing(6);

    auto* pinOps = new QHBoxLayout();
    auto* renumberBtn = new QPushButton("Renumber 1..N");
    connect(renumberBtn, &QPushButton::clicked, this, &SymbolEditor::onPinRenumberSequential);

    m_pinBulkOrientation = new QComboBox();
    m_pinBulkOrientation->addItems({"Right", "Left", "Up", "Down"});
    auto* applyOrientationBtn = new QPushButton("Apply Orientation");
    connect(applyOrientationBtn, &QPushButton::clicked, this, &SymbolEditor::onPinApplyOrientation);

    m_pinBulkType = new QComboBox();
    m_pinBulkType->addItems({"Input", "Output", "Bidirectional", "Tri-state", "Passive", "Free", "Unspecified", "Power Input", "Power Output", "Open Collector", "Open Emitter"});
    auto* applyTypeBtn = new QPushButton("Apply Type");
    connect(applyTypeBtn, &QPushButton::clicked, this, &SymbolEditor::onPinApplyType);
    auto* distributeBtn = new QPushButton("Distribute Selected");
    connect(distributeBtn, &QPushButton::clicked, this, &SymbolEditor::onPinDistributeSelected);
    auto* sortByNumBtn = new QPushButton("Auto-sort by Number");
    connect(sortByNumBtn, &QPushButton::clicked, this, &SymbolEditor::onPinSortByNumber);

    pinOps->addWidget(renumberBtn);
    pinOps->addSpacing(8);
    pinOps->addWidget(new QLabel("Orientation:"));
    pinOps->addWidget(m_pinBulkOrientation);
    pinOps->addWidget(applyOrientationBtn);
    pinOps->addSpacing(8);
    pinOps->addWidget(new QLabel("Type:"));
    pinOps->addWidget(m_pinBulkType);
    pinOps->addWidget(applyTypeBtn);
    pinOps->addSpacing(8);
    pinOps->addWidget(distributeBtn);
    pinOps->addWidget(sortByNumBtn);
    pinOps->addStretch();

    pinLayout->addLayout(pinOps);
    pinLayout->addWidget(m_pinTable);
    pinDock->setWidget(pinContainer);
    addDockWidget(Qt::BottomDockWidgetArea, pinDock);

    tabifyDockWidget(libDock, wizDock);
    tabifyDockWidget(pinDock, codeDock);
    setStatusBar(m_statusBar);
    createStatusBar();

    // Panel toggle icons (match schematic editor panel toggles)
    auto makePanelIcon = [](const QString& type) -> QIcon {
        QPixmap px(32, 32);
        px.fill(Qt::transparent);
        QPainter p(&px);
        p.setRenderHint(QPainter::Antialiasing);
        QColor color = Qt::white;
        if (ThemeManager::theme()) {
            color = ThemeManager::theme()->textColor();
        }
        p.setPen(QPen(color, 2));
        p.drawRect(6, 8, 20, 16);
        p.setBrush(color);
        if (type == "left") {
            p.drawRect(6, 8, 6, 16);
        } else if (type == "bottom") {
            p.drawRect(6, 18, 20, 6);
        } else if (type == "right") {
            p.drawRect(20, 8, 6, 16);
        }
        p.end();
        return QIcon(px);
    };

    m_toolbar->addSeparator();

    // Left sidebar toggle (hides both Library + Wizard docks)
    {
        QAction* leftToggle = new QAction(makePanelIcon("left"), "Toggle Left Sidebar", this);
        leftToggle->setToolTip("Toggle Left Sidebar");
        leftToggle->setCheckable(true);
        leftToggle->setChecked(true);
        m_toolbar->addAction(leftToggle);
        connect(leftToggle, &QAction::toggled, this, [libDock, wizDock](bool visible) {
            if (libDock) libDock->setVisible(visible);
            if (wizDock) wizDock->setVisible(visible);
        });
        // Sync check state if dock is closed externally
        auto syncLeft = [leftToggle, libDock]() {
            if (libDock) leftToggle->setChecked(libDock->isVisible());
        };
        if (libDock) connect(libDock, &QDockWidget::visibilityChanged, this, syncLeft);
    }

    // Bottom panel toggle (hides Pin + JSON Source + Rule Checker docks)
    {
        QAction* bottomToggle = new QAction(makePanelIcon("bottom"), "Toggle Bottom Panel", this);
        bottomToggle->setToolTip("Toggle Bottom Panel");
        bottomToggle->setCheckable(true);
        bottomToggle->setChecked(true);
        m_toolbar->addAction(bottomToggle);
        connect(bottomToggle, &QAction::toggled, this, [pinDock, codeDock, srcDock](bool visible) {
            if (pinDock) pinDock->setVisible(visible);
            if (codeDock) codeDock->setVisible(visible);
            if (srcDock) srcDock->setVisible(visible);
        });
        auto syncBottom = [bottomToggle, pinDock]() {
            if (pinDock) bottomToggle->setChecked(pinDock->isVisible());
        };
        if (pinDock) connect(pinDock, &QDockWidget::visibilityChanged, this, syncBottom);
    }

    // Right sidebar toggle
    if (m_propsDock) {
        QAction* propsToggle = m_propsDock->toggleViewAction();
        propsToggle->setIcon(makePanelIcon("right"));
        propsToggle->setText("Toggle Right Sidebar");
        propsToggle->setToolTip("Toggle Right Sidebar");
        m_toolbar->addAction(propsToggle);
    }
}

#include <QApplication>

void SymbolEditor::onPlaceInSchematic() {
    SymbolDefinition def = symbolDefinition();
    if (!def.isValid()) {
        QMessageBox::warning(this, "Place Symbol", "Current symbol is empty or invalid.");
        return;
    }
    Q_EMIT placeInSchematicRequested(def);

    // Find and raise SchematicEditor window
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (widget->inherits("SchematicEditor")) {
            widget->show();
            widget->raise();
            widget->activateWindow();
            break;
        }
    }
}

void SymbolEditor::connectViewSignals() {
    // Escape / right-click → finalize drawing or revert to Select tool
    connect(m_view, &SymbolEditorView::rightClicked, this, [this]() {
        if (m_currentTool == Polygon && m_polyPoints.size() > 2) {
            SymbolPrimitive prim = SymbolPrimitive::createPolygon(m_polyPoints, false);
            QGraphicsItem* visual = buildVisual(prim, m_symbol.primitives().size());
            if (visual) m_undoStack->push(new AddPrimitiveCommand(this, prim, visual));
        } else if (m_currentTool == Bezier && m_polyPoints.size() >= 2) {
            // If we have at least start and end, we could finalize but Bezier really needs 4.
        } else if (m_currentTool == Pen && m_penPoints.size() >= 2) {
            finalizePenPath();
        }

        // Switch to select tool
        m_currentTool = Select;
        m_view->setCurrentTool(0);
        m_view->setDragMode(QGraphicsView::RubberBandDrag);
        
        // Cleanup state
        m_polyPoints.clear();
        clearPenState();
        
        // Update toolbar check state
        if (m_selectAction) m_selectAction->setChecked(true);
    });

    connect(m_view, &SymbolEditorView::contextMenuRequested, this, &SymbolEditor::onCanvasContextMenu);

    connect(m_view, &SymbolEditorView::rotateCWRequested,  this, &SymbolEditor::onRotateCW);
    connect(m_view, &SymbolEditorView::rotateCCWRequested, this, &SymbolEditor::onRotateCCW);
    connect(m_view, &SymbolEditorView::flipHRequested,     this, &SymbolEditor::onFlipH);
    connect(m_view, &SymbolEditorView::flipVRequested,     this, &SymbolEditor::onFlipV);
    connect(m_view, &SymbolEditorView::coordinatesChanged, this, &SymbolEditor::updateCoordinates);
    connect(m_view, &SymbolEditorView::itemErased,         this, &SymbolEditor::onItemErased);
    
     // Pen tool signals
     connect(m_view, &SymbolEditorView::penPointAdded, this, &SymbolEditor::onPenPointAdded);
     connect(m_view, &SymbolEditorView::penHandleDragged, this, &SymbolEditor::onPenHandleDragged);
     connect(m_view, &SymbolEditorView::penPointFinished, this, &SymbolEditor::onPenPointFinished);
     connect(m_view, &SymbolEditorView::penPathClosed, this, &SymbolEditor::onPenPathClosed);
     connect(m_view, &SymbolEditorView::penClicked, this, &SymbolEditor::onPenClicked);
     connect(m_view, &SymbolEditorView::penDoubleClicked, this, &SymbolEditor::onPenDoubleClicked);
     
     // Bezier edit signals (Select mode)
     connect(m_view, &SymbolEditorView::bezierEditPointClicked, this, &SymbolEditor::onBezierEditPointClicked);
     connect(m_view, &SymbolEditorView::bezierEditPointDragged, this, &SymbolEditor::onBezierEditPointDragged);
     connect(m_view, &SymbolEditorView::rectResizeStarted, this, &SymbolEditor::onRectResizeStarted);
     connect(m_view, &SymbolEditorView::rectResizeUpdated, this, &SymbolEditor::onRectResizeUpdated);
     connect(m_view, &SymbolEditorView::rectResizeFinished, this, &SymbolEditor::onRectResizeFinished);

    // Items dragged in Select mode → move primitives via undo command
    connect(m_view, &SymbolEditorView::itemsMoved, this, [this](QPointF delta) {
        QList<int> indices;
        bool referenceMoved = false;
        bool nameMoved = false;
        QPointF newRefPos, newNamePos;

        for (QGraphicsItem* item : m_scene->selectedItems()) {
            if (item->data(0).toString() == "label") {
                QString type = item->data(1).toString();
                if (type == "reference") { referenceMoved = true; newRefPos = item->pos(); }
                else if (type == "name") { nameMoved = true; newNamePos = item->pos(); }
                continue;
            }

            int idx = primitiveIndex(item);
            if (idx != -1 && !indices.contains(idx))
                indices.append(idx);
        }
        
        if (indices.isEmpty() && !referenceMoved && !nameMoved) return;

        SymbolDefinition oldDef = symbolDefinition();
        SymbolDefinition newDef = oldDef;
        const QSet<int> selectedSet = QSet<int>(indices.begin(), indices.end());

        if (referenceMoved) newDef.setReferencePos(newRefPos);
        if (nameMoved) newDef.setNamePos(newNamePos);

        for (int idx : indices) {
            SymbolPrimitive& prim = newDef.primitives()[idx];
            qreal localDx = delta.x();
            qreal localDy = delta.y();

            // Smart guide snap for moved pins: align to nearby unselected pin X/Y.
            if (prim.type == SymbolPrimitive::Pin) {
                const qreal oldX = oldDef.primitives()[idx].data.value("x").toDouble();
                const qreal oldY = oldDef.primitives()[idx].data.value("y").toDouble();
                qreal newX = oldX + localDx;
                qreal newY = oldY + localDy;
                const qreal threshold = m_view ? qMax<qreal>(2.0, m_view->gridSize() * 0.4) : 4.0;
                qreal bestX = threshold + 1.0;
                qreal bestY = threshold + 1.0;
                bool snapX = false;
                bool snapY = false;
                qreal targetX = newX;
                qreal targetY = newY;

                for (int j = 0; j < oldDef.primitives().size(); ++j) {
                    if (selectedSet.contains(j)) continue;
                    const SymbolPrimitive& other = oldDef.primitives().at(j);
                    if (other.type != SymbolPrimitive::Pin) continue;
                    const qreal ox = other.data.value("x").toDouble();
                    const qreal oy = other.data.value("y").toDouble();

                    const qreal dxAbs = qAbs(newX - ox);
                    if (dxAbs < bestX && dxAbs <= threshold) {
                        bestX = dxAbs;
                        targetX = ox;
                        snapX = true;
                    }
                    const qreal dyAbs = qAbs(newY - oy);
                    if (dyAbs < bestY && dyAbs <= threshold) {
                        bestY = dyAbs;
                        targetY = oy;
                        snapY = true;
                    }
                }

                if (snapX) localDx = targetX - oldX;
                if (snapY) localDy = targetY - oldY;
            }

            auto shift = [&](const char* k, qreal d) {
                if (prim.data.contains(k)) prim.data[k] = prim.data[k].toDouble() + d;
            };
            shift("x",  localDx); shift("y",  localDy);
            shift("x1", localDx); shift("y1", localDy);
            shift("x2", localDx); shift("y2", localDy);
            shift("cx", localDx); shift("cy", localDy);
            shift("centerX", localDx); shift("centerY", localDy);

            if (prim.type == SymbolPrimitive::Polygon) {
                QJsonArray pts = prim.data["points"].toArray();
                QJsonArray newPts;
                for (auto v : pts) {
                    QJsonObject o = v.toObject();
                    o["x"] = o["x"].toDouble() + localDx;
                    o["y"] = o["y"].toDouble() + localDy;
                    newPts.append(o);
                }
                prim.data["points"] = newPts;
            }
        }
        m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Move Items"));
    });

    // Single click in a drawing tool
    connect(m_view, &SymbolEditorView::pointClicked, this, [this](QPointF pos) {
        // Double check snapping for safety
        pos = m_view->snapToGrid(pos);

        if (m_currentTool == Pin) {
            // Count existing pins in the symbol, not all primitives
            int pinCount = 0;
            for (const auto& p : m_symbol.primitives()) {
                if (p.type == SymbolPrimitive::Pin) ++pinCount;
            }
            int pinNum = pinCount + 1;
            SymbolPrimitive prim = SymbolPrimitive::createPin(pos, pinNum,
                                                              QString::number(pinNum),
                                                              m_previewOrientation);
            prim.setUnit(m_currentUnit);
            prim.setBodyStyle(m_currentStyle);
            QGraphicsItem* visual = buildVisual(prim, m_symbol.primitives().size());
            if (visual)
                m_undoStack->push(new AddPrimitiveCommand(this, prim, visual));

        } else if (m_currentTool == Text) {
            TextPropertiesDialog dlg(this);
            if (dlg.exec() == QDialog::Accepted && !dlg.text().isEmpty()) {
                SymbolPrimitive prim = SymbolPrimitive::createText(dlg.text(), pos, dlg.fontSize(), dlg.color());
                prim.setUnit(m_currentUnit);
                prim.setBodyStyle(m_currentStyle);
                QGraphicsItem* visual = buildVisual(prim, m_symbol.primitives().size());
                if (visual)
                    m_undoStack->push(new AddPrimitiveCommand(this, prim, visual));
            }

        } else if (m_currentTool == Line || m_currentTool == Circle || m_currentTool == Arc || m_currentTool == Rect) {
            m_polyPoints.append(pos);
            if (m_polyPoints.size() == 2) {
                QPointF p1 = m_polyPoints[0];
                QPointF p2 = m_polyPoints[1];
                SymbolPrimitive prim;
                
                if (m_currentTool == Line) {
                    prim = SymbolPrimitive::createLine(p1, p2);
                } else if (m_currentTool == Rect) {
                    prim = SymbolPrimitive::createRect(QRectF(p1, p2).normalized(), false);
                } else if (m_currentTool == Circle) {
                    prim = SymbolPrimitive::createCircle(p1, QLineF(p1, p2).length(), false);
                } else { // Arc
                    qreal rx = qAbs(p2.x() - p1.x());
                    qreal ry = qAbs(p2.y() - p1.y());
                    prim = SymbolPrimitive::createArc(QRectF(p1.x()-rx, p1.y()-ry, rx*2, ry*2), 0, 180 * 16);
                }
                prim.setUnit(m_currentUnit);
                prim.setBodyStyle(m_currentStyle);

                QGraphicsItem* visual = buildVisual(prim, m_symbol.primitives().size());
                m_polyPoints.clear();
                if (visual) m_undoStack->push(new AddPrimitiveCommand(this, prim, visual));
            }

        } else if (m_currentTool == Polygon) {
            m_polyPoints.append(pos);
            // Close polygon if user clicks near the first point
            if (m_polyPoints.size() > 2
                && QLineF(pos, m_polyPoints.first()).length() < 8.0) {
                m_polyPoints.removeLast();
                SymbolPrimitive prim = SymbolPrimitive::createPolygon(m_polyPoints, false);
                prim.setUnit(m_currentUnit);
                prim.setBodyStyle(m_currentStyle);
                QGraphicsItem* visual = buildVisual(prim, m_symbol.primitives().size());
                m_polyPoints.clear();
                if (m_previewItem) {
                    m_scene->removeItem(m_previewItem);
                    delete m_previewItem;
                    m_previewItem = nullptr;
                }
                if (visual)
                    m_undoStack->push(new AddPrimitiveCommand(this, prim, visual));
            }

        } else if (m_currentTool == Bezier) {
            m_polyPoints.append(pos);
            if (m_polyPoints.size() == 4) {
                SymbolPrimitive prim = SymbolPrimitive::createBezier(m_polyPoints[0], m_polyPoints[2], m_polyPoints[3], m_polyPoints[1]);
                prim.setUnit(m_currentUnit);
                prim.setBodyStyle(m_currentStyle);
                QGraphicsItem* visual = buildVisual(prim, m_symbol.primitives().size());
                m_polyPoints.clear();
                if (m_previewItem) {
                    m_scene->removeItem(m_previewItem);
                    delete m_previewItem;
                    m_previewItem = nullptr;
                }
                if (visual) m_undoStack->push(new AddPrimitiveCommand(this, prim, visual));
            }

        } else if (m_currentTool == Anchor) {
            // Shift the whole symbol so 'pos' becomes (0,0)
            SymbolDefinition oldDef = symbolDefinition();
            SymbolDefinition newDef = oldDef;
            
            // Shift Metadata Labels
            newDef.setReferencePos(oldDef.referencePos() - pos);
            newDef.setNamePos(oldDef.namePos() - pos);

            for (SymbolPrimitive& prim : newDef.primitives()) {
                auto shift = [&](const char* k, qreal d) {
                    if (prim.data.contains(k)) prim.data[k] = prim.data[k].toDouble() - d;
                };
                shift("x",  pos.x()); shift("y",  pos.y());
                shift("x1", pos.x()); shift("y1", pos.y());
                shift("x2", pos.x()); shift("y2", pos.y());
                shift("x3", pos.x()); shift("y3", pos.y());
                shift("x4", pos.x()); shift("y4", pos.y());
                shift("cx", pos.x()); shift("cy", pos.y());
                shift("centerX", pos.x()); shift("centerY", pos.y());
                
                if (prim.type == SymbolPrimitive::Polygon) {
                    QJsonArray pts = prim.data["points"].toArray();
                    QJsonArray newPts;
                    for (auto v : pts) {
                        QJsonObject o = v.toObject();
                        o["x"] = o["x"].toDouble() - pos.x();
                        o["y"] = o["y"].toDouble() - pos.y();
                        newPts.append(o);
                    }
                    prim.data["points"] = newPts;
                }
            }
            m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Set Anchor"));
        }
    });

    // Drag preview while drawing
    connect(m_view, &SymbolEditorView::mouseMoved, this, [this](QPointF pos) {
        if (m_previewItem) {
            m_scene->removeItem(m_previewItem);
            delete m_previewItem;
            m_previewItem = nullptr;
        }
        
        // No preview for Select or Erase
        if (m_currentTool == Select || m_currentTool == Erase || m_currentTool == Anchor) return;

        const QPen previewPen(Qt::cyan, 1, Qt::DashLine);
        QPointF start = m_polyPoints.isEmpty() ? pos : m_polyPoints.first();
        QPointF end = pos;

        switch (m_currentTool) {
        case Line:
            m_previewItem = m_scene->addLine(QLineF(start, end), previewPen);
            break;
        case Rect: {
            QRectF r = QRectF(start, end).normalized();
            m_previewItem = m_scene->addRect(r, previewPen);
            break;
        }
        case Circle: {
            qreal rad = QLineF(start, end).length();
            m_previewItem = m_scene->addEllipse(
                start.x()-rad, start.y()-rad, rad*2, rad*2, previewPen);
            break;
        }
        case Bezier: {
            if (m_polyPoints.isEmpty()) {
                m_previewItem = m_scene->addLine(QLineF(pos, pos), previewPen);
            } else if (m_polyPoints.size() == 1) {
                m_previewItem = m_scene->addLine(QLineF(m_polyPoints[0], pos), previewPen);
            } else if (m_polyPoints.size() == 2) {
                QPainterPath path;
                path.moveTo(m_polyPoints[0]);
                path.cubicTo(pos, pos, m_polyPoints[1]);
                m_previewItem = m_scene->addPath(path, previewPen);
            } else if (m_polyPoints.size() == 3) {
                QPainterPath path;
                path.moveTo(m_polyPoints[0]);
                path.cubicTo(m_polyPoints[2], pos, m_polyPoints[1]);
                m_previewItem = m_scene->addPath(path, previewPen);
            }
            break;
        }
        case Arc: {
            qreal rx = qAbs(end.x() - start.x());
            qreal ry = qAbs(end.y() - start.y());
            QRectF r(start.x()-rx, start.y()-ry, rx*2, ry*2);
            QPainterPath path;
            path.arcMoveTo(r, 0);
            path.arcTo(r, 0, 180);
            m_previewItem = m_scene->addPath(path, previewPen);
            break;
        }
        case Polygon: {
            if (m_polyPoints.isEmpty()) break;
            QPolygonF poly = m_polyPoints;
            poly.append(end);
            m_previewItem = m_scene->addPolygon(poly, previewPen);
            break;
        }
        case ZoomArea: {
            QRectF r = QRectF(start, end).normalized();
            m_previewItem = m_scene->addRect(r, previewPen);
            break;
        }
        case Pin:
            updatePinPreview(pos);
            break;
        default: break;
        }
    });

    // Drag finish – commit shape
    connect(m_view, &SymbolEditorView::drawingFinished, this, [this](QPointF start, QPointF end) {
        if (m_currentTool == ZoomArea) {
            QRectF r = QRectF(start, end).normalized();
            if (r.width() > 5 && r.height() > 5)
                m_view->fitInView(r, Qt::KeepAspectRatio);
        }
    });
}

// ─────────────────────────────────────────────────────────────────────────────
//  SymbolEditor – Toolbar / Panel creation
// ─────────────────────────────────────────────────────────────────────────────

#include <QMenuBar>

void SymbolEditor::createMenuBar() {
    QMenuBar* mb = menuBar();
    mb->setStyleSheet("QMenuBar { background-color: #2d2d30; color: #ccc; } "
                      "QMenuBar::item:selected { background-color: #3e3e42; }");

    // --- File Menu ---
    QMenu* fileMenu = mb->addMenu("&File");
    fileMenu->addAction(getThemeIcon(":/icons/tool_new.svg", false), "&New Symbol", QKeySequence::New, this, &SymbolEditor::onNewSymbol);
    fileMenu->addAction(getThemeIcon(":/icons/tool_save.svg", false), "&Save", QKeySequence::Save, this, &SymbolEditor::onSave);
    fileMenu->addAction(getThemeIcon(":/icons/tool_save_as.svg", false), "Save As...", QKeySequence(), this, &SymbolEditor::onExportVioSym);
    fileMenu->addAction(getThemeIcon(":/icons/tool_gear.svg", false), "Save as Wizard Template...", QKeySequence(), this, &SymbolEditor::onWizardSaveTemplate);
    fileMenu->addAction(getThemeIcon(":/icons/tool_refresh.svg"), "Refresh Libraries", QKeySequence::Refresh, this, &SymbolEditor::onRefreshLibraries);
    fileMenu->addSeparator();
    fileMenu->addAction(getThemeIcon(":/icons/nav_schematic.svg", false), "&Place in Schematic", QKeySequence(), this, &SymbolEditor::onPlaceInSchematic);
    fileMenu->addSeparator();
    fileMenu->addAction("Close", QKeySequence::Close, this, &QWidget::close);

    // --- Edit Menu ---
    QMenu* editMenu = mb->addMenu("&Edit");
    editMenu->addAction(m_undoStack->createUndoAction(this, "&Undo"));
    editMenu->addAction(m_undoStack->createRedoAction(this, "&Redo"));
    editMenu->addSeparator();
    editMenu->addAction(getThemeIcon(":/icons/tool_delete.svg"), "&Delete", QKeySequence::Delete, this, &SymbolEditor::onDelete);
    QAction* selectAllAct = editMenu->addAction("Select &All");
    selectAllAct->setShortcut(QKeySequence::SelectAll);
    connect(selectAllAct, &QAction::triggered, this, [this]() {
        for (auto* it : m_scene->items()) it->setSelected(true);
    });

    // --- AI Menu ---
    QMenu* aiMenu = mb->addMenu("&AI");
    aiMenu->addAction(getThemeIcon(":/icons/ai_wizard.png", false), "Generate Pins from Datasheet Text...", QKeySequence(), this, &SymbolEditor::onAIDatasheetImport);
    aiMenu->addAction(getThemeIcon(":/icons/tool_sheet.svg"), "Import SPICE Subcircuit...", QKeySequence(), this, &SymbolEditor::onImportSpiceSubcircuit);

    // --- View Menu (The core request) ---
    QMenu* viewMenu = mb->addMenu("&View");
    
    // Helper to add dock toggle
    auto addDockToggle = [&](const QString& title, const QString& dockName) {
        QDockWidget* dock = findChild<QDockWidget*>(dockName);
        if (dock) {
            QAction* act = viewMenu->addAction(title);
            act->setCheckable(true);
            act->setChecked(dock->isVisible());
            connect(act, &QAction::toggled, dock, &QDockWidget::setVisible);
            connect(dock, &QDockWidget::visibilityChanged, act, &QAction::setChecked);
        }
    };

    addDockToggle("Show Properties",  "PropertiesDock");
    addDockToggle("Show Library Browser", "LibraryDock");
    addDockToggle("Show IC Wizard", "WizardDock");
    viewMenu->addSeparator();
    addDockToggle("Show Pin Table", "PinDock");
    addDockToggle("Show JSON Preview", "CodeDock");
    addDockToggle("Show Rule Checker", "SRCDock");
    
    viewMenu->addSeparator();
    QAction* resetLayoutAct = viewMenu->addAction("Reset Layout");
    connect(resetLayoutAct, &QAction::triggered, this, [this]() {
        // Find and show all docks
        for (auto* dock : findChildren<QDockWidget*>()) {
            dock->show();
        }
        
        // Logical zones
        QDockWidget* props = findChild<QDockWidget*>("PropertiesDock");
        QDockWidget* lib = findChild<QDockWidget*>("LibraryDock");
        QDockWidget* wiz = findChild<QDockWidget*>("WizardDock");
        QDockWidget* pin = findChild<QDockWidget*>("PinDock");
        QDockWidget* code = findChild<QDockWidget*>("CodeDock");
        QDockWidget* src = findChild<QDockWidget*>("SRCDock");

        if (props) {
            addDockWidget(Qt::RightDockWidgetArea, props);
            props->raise();
        }
        if (lib && wiz) {
            addDockWidget(Qt::LeftDockWidgetArea, lib);
            tabifyDockWidget(lib, wiz);
            lib->raise();
        }
        if (pin && code && src) {
            addDockWidget(Qt::BottomDockWidgetArea, pin);
            tabifyDockWidget(pin, code);
            tabifyDockWidget(code, src);
            pin->raise();
        }
    });
    viewMenu->addAction("Zoom Fit", QKeySequence("F"), this, &SymbolEditor::onZoomFit);
    viewMenu->addAction("Zoom Selection", QKeySequence("Ctrl+0"), this, &SymbolEditor::onZoomSelection);
    QAction* snapCursorCrosshairAct = viewMenu->addAction("Snap Cursor Crosshair");
    snapCursorCrosshairAct->setCheckable(true);
    snapCursorCrosshairAct->setChecked(false);
    connect(snapCursorCrosshairAct, &QAction::toggled, this, [this](bool on) {
        if (m_view) m_view->setSnapCursorCrosshairEnabled(on);
        if (statusBar()) {
            statusBar()->showMessage(on ? "Snap cursor crosshair: ON" : "Snap cursor crosshair: OFF", 1800);
        }
    });
    viewMenu->addSeparator();
    auto* toggleMaxAct = viewMenu->addAction("Toggle Maximize");
    toggleMaxAct->setShortcut(QKeySequence("F11"));
    connect(toggleMaxAct, &QAction::triggered, this, [this]() {
        if (this->isMaximized()) {
            this->showNormal();
            return;
        }
        this->setWindowState(this->windowState() | Qt::WindowMaximized);
        if (!this->isMaximized()) {
            if (QScreen* s = this->window()->screen()) {
                this->setGeometry(s->availableGeometry());
            }
        }
    });
    
    viewMenu->addSeparator();
    QAction* resetDefaultLayoutAct = viewMenu->addAction("Reset Default Layout");
    connect(resetDefaultLayoutAct, &QAction::triggered, this, [this]() {
        // Restore visibility of primary docks
        for (auto* dock : findChildren<QDockWidget*>()) {
            if (dock->objectName() != "CodeDock" && dock->objectName() != "SRCDock") {
                dock->show();
            }
        }
        statusBar()->showMessage("Workspace layout reset", 2000);
    });
}

void SymbolEditor::createToolBar() {
    m_toolbar = new QToolBar("Utilities", this);
    m_toolbar->setObjectName("TopToolbar");
    m_toolbar->setIconSize(QSize(20, 20));
    m_toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_toolbar->setMovable(false);
    m_toolbar->setStyleSheet(
        "QToolBar#TopToolbar {"
        "  background: #252526;"
        "  border: 1px solid #3e3e42;"
        "  border-radius: 8px;"
        "  margin: 5px;"
        "  padding: 2px;"
        "}"
        "QToolBar#TopToolbar QToolButton {"
        "  border-radius: 4px;"
        "  padding: 4px;"
        "  margin: 1px;"
        "  background: transparent;"
        "  color: #cccccc;"
        "}"
        "QToolBar#TopToolbar QToolButton:hover {"
        "  background: #3e3e42;"
        "  border: 1px solid #555;"
        "}"
        "QToolBar#TopToolbar QToolButton:checked, QToolBar#TopToolbar QToolButton:pressed {"
        "  background: #094771;"
        "}"
        "QToolBar#TopToolbar QLabel {"
        "  color: #888;"
        "  font-size: 11px;"
        "  padding: 0 2px;"
        "}"
        "QToolBar#TopToolbar QComboBox {"
        "  background-color: #1e1e1e;"
        "  color: #cccccc;"
        "  border: 1px solid #3c3c3c;"
        "  border-radius: 3px;"
        "  padding: 2px 6px;"
        "  font-size: 11px;"
        "}"
    );

    m_leftToolbar = new QToolBar("Drawing Tools", this);
    m_leftToolbar->setOrientation(Qt::Vertical);
    m_leftToolbar->setIconSize(QSize(22, 22));
    m_leftToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_leftToolbar->setMovable(false);
    m_leftToolbar->setObjectName("LeftToolBar");
    m_leftToolbar->setStyleSheet(
        "QToolBar#LeftToolBar {"
        "  background-color: #1e1e1e;"
        "  border-right: 1px solid #3c3c3c;"
        "  padding: 6px 4px;"
        "  spacing: 2px;"
        "}"
        "QToolBar#LeftToolBar QToolButton {"
        "  background: transparent;"
        "  border: 1px solid transparent;"
        "  border-radius: 4px;"
        "  padding: 5px;"
        "  margin: 1px 2px;"
        "  color: #cccccc;"
        "}"
        "QToolBar#LeftToolBar QToolButton:hover {"
        "  border-color: #555;"
        "  background-color: #3c3c3c;"
        "}"
        "QToolBar#LeftToolBar QToolButton:checked {"
        "  background-color: #094771;"
        "  border-color: #094771;"
        "  color: white;"
        "}"
    );

    auto* toolGroup = new QActionGroup(this);
    toolGroup->setExclusive(true);

    // Helper: add a checkable drawing tool to the left toolbar
    auto addTool = [&](const QString& name, const QString& icon,
                       Tool tool, const QString& shortcut = "") -> QAction* {
        QAction* act = m_leftToolbar->addAction(getThemeIcon(icon), name);
        act->setCheckable(true);
        act->setData(static_cast<int>(tool));
        act->setProperty("iconPath", icon);
        if (tool == Select) m_selectAction = act;
        if (!shortcut.isEmpty()) {
            act->setShortcut(QKeySequence(shortcut));
            act->setShortcutContext(Qt::WidgetWithChildrenShortcut);
            act->setToolTip(name + " (" + shortcut + ")");
            this->addAction(act);
        } else {
            act->setToolTip(name);
        }
        toolGroup->addAction(act);
        m_toolActions[name] = act;
        connect(act, &QAction::triggered, this, &SymbolEditor::onToolSelected);
        return act;
    };

    addTool("Select",  ":/icons/tool_select.svg",  Select,  "Escape")->setChecked(true);
    m_leftToolbar->addSeparator();
    addTool("Pin",     ":/icons/tool_pin.svg",     Pin,     "P");
    addTool("Line",    ":/icons/tool_line.svg",    Line,    "L");
    addTool("Rect",    ":/icons/tool_rect.svg",    Rect,    "R");
    addTool("Circle",  ":/icons/tool_circle.svg",  Circle,  "C");
    addTool("Arc",     ":/icons/tool_arc.svg",     Arc,     "A");
    addTool("Polygon", ":/icons/tool_polygon.svg", Polygon, "G");
    addTool("Bezier",  ":/icons/tool_bezier.svg",  Bezier,  "B");
    addTool("Pen",     ":/icons/tool_pen.svg",     Pen,     "Y");
    addTool("Image",   ":/icons/tool_image.svg",   SymbolEditor::Image,  "I");
    addTool("Text",    ":/icons/tool_text.svg",    Text,    "T");
    addTool("Erase",   ":/icons/tool_erase.svg",   Erase,   "E");
    // Anchor tool
    addTool("Anchor",  ":/icons/tool_anchor.svg",  Anchor,  "H");

    // ── Top toolbar ──────────────────────────────────────────────────────────

    QAction* newSym = m_toolbar->addAction(getThemeIcon(":/icons/tool_new.svg", false), "New Symbol");
    newSym->setShortcut(QKeySequence::New);
    newSym->setToolTip("New Symbol (Ctrl+N)");
    connect(newSym, &QAction::triggered, this, &SymbolEditor::onNewSymbol);

    QAction* saveAction = m_toolbar->addAction(getThemeIcon(":/icons/tool_save.svg", false), "Save");
    saveAction->setShortcut(QKeySequence::Save);
    saveAction->setToolTip("Save (Ctrl+S)");
    connect(saveAction, &QAction::triggered, this, &SymbolEditor::onSave);

    QAction* exportAction = m_toolbar->addAction(getThemeIcon(":/icons/tool_save_as.svg", false), "Save As...");
    exportAction->setToolTip("Save symbol to a .viosym file");
    connect(exportAction, &QAction::triggered, this, &SymbolEditor::onExportVioSym);

    QAction* saveTplAction = m_toolbar->addAction(getThemeIcon(":/icons/tool_gear.svg", false), "Save Wizard Template");
    saveTplAction->setToolTip("Save current symbol as IC Wizard template");
    connect(saveTplAction, &QAction::triggered, this, &SymbolEditor::onWizardSaveTemplate);

    QAction* runAction = m_toolbar->addAction(getThemeIcon(":/icons/tool_run.png", false), "Run Simulation");
    runAction->setToolTip("Run spice simulation on current symbol (F5)");
    runAction->setShortcut(QKeySequence("F5"));
    // connect(runAction, &QAction::triggered, this, &SomeAction);

    auto* openSchAct = m_toolbar->addAction(getThemeIcon(":/icons/nav_schematic.svg", false), "Open in Schematic");
    openSchAct->setToolTip("Place this symbol in the current schematic");
    connect(openSchAct, &QAction::triggered, this, &SymbolEditor::onPlaceInSchematic);

    auto* importImgAct = m_toolbar->addAction(getThemeIcon(":/icons/tool_image.svg", false), "Import Image");
    importImgAct->setToolTip("Import a bitmap image into the symbol");
    connect(importImgAct, &QAction::triggered, this, &SymbolEditor::onImportImage);

    m_toolbar->addSeparator();

    m_undoAction = m_undoStack->createUndoAction(this, "Undo");
    m_undoAction->setIcon(getThemeIcon(":/icons/undo.svg"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_undoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_toolbar->addAction(m_undoAction);

    m_redoAction = m_undoStack->createRedoAction(this, "Redo");
    m_redoAction->setIcon(getThemeIcon(":/icons/redo.svg"));
    m_redoAction->setShortcut(QKeySequence::Redo);
    m_redoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_toolbar->addAction(m_redoAction);

    m_deleteAction = new QAction(getThemeIcon(":/icons/tool_delete.svg", false), "Delete", this);
    m_deleteAction->setShortcut(QKeySequence::Delete);
    connect(m_deleteAction, &QAction::triggered, this, &SymbolEditor::onDelete);
    m_toolbar->addAction(m_deleteAction);

    m_toolbar->addSeparator();

    auto* zoomIn  = m_toolbar->addAction(getThemeIcon(":/icons/view_zoom_in.svg"),  "Zoom In");
    auto* zoomOut = m_toolbar->addAction(getThemeIcon(":/icons/view_zoom_out.svg"), "Zoom Out");
    auto* zoomFit = m_toolbar->addAction(getThemeIcon(":/icons/view_fit.svg"),      "Zoom Fit");
    connect(zoomIn,  &QAction::triggered, this, &SymbolEditor::onZoomIn);
    connect(zoomOut, &QAction::triggered, this, &SymbolEditor::onZoomOut);
    connect(zoomFit, &QAction::triggered, this, &SymbolEditor::onZoomFit);

    m_toolbar->addSeparator();

    auto* rotateCWAct = m_toolbar->addAction(getThemeIcon(":/icons/tool_rotate.svg"), "Rotate 90° CW");
    rotateCWAct->setShortcut(QKeySequence("Ctrl+R"));
    rotateCWAct->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(rotateCWAct, &QAction::triggered, this, &SymbolEditor::onRotateCW);
    this->addAction(rotateCWAct);

    auto* rotateCCWAct = m_toolbar->addAction(getThemeIcon(":/icons/tool_rotate_ccw.svg"), "Rotate 90° CCW");
    rotateCCWAct->setShortcut(QKeySequence("Ctrl+Shift+R"));
    rotateCCWAct->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(rotateCCWAct, &QAction::triggered, this, &SymbolEditor::onRotateCCW);
    this->addAction(rotateCCWAct);

    auto* flipH = m_toolbar->addAction(getThemeIcon(":/icons/flip_h.svg"), "Flip Horizontal");
    flipH->setShortcut(QKeySequence("X"));
    connect(flipH, &QAction::triggered, this, &SymbolEditor::onFlipH);
    this->addAction(flipH);

    auto* flipV = m_toolbar->addAction(getThemeIcon(":/icons/flip_v.svg"), "Flip Vertical");
    flipV->setShortcut(QKeySequence("Y"));
    connect(flipV, &QAction::triggered, this, &SymbolEditor::onFlipV);
    this->addAction(flipV);

    auto* srcAct = m_toolbar->addAction(getThemeIcon(":/icons/tool_src.png", false), "Run SRC");
    srcAct->setToolTip("Run Symbol Rule Checker (F7)");
    srcAct->setShortcut(QKeySequence("F7"));
    connect(srcAct, &QAction::triggered, this, &SymbolEditor::onRunSRC);

}

void SymbolEditor::createStatusBar() {
    m_statusBar->setStyleSheet(
        "QStatusBar {"
        "  background-color: #007acc;"
        "  color: white;"
        "  font-size: 11px;"
        "  padding: 0px 8px;"
        "  border: none;"
        "  min-height: 22px;"
        "  max-height: 22px;"
        "}"
        "QStatusBar QLabel {"
        "  color: white;"
        "  font-size: 11px;"
        "}"
    );

    m_coordLabel = new QLabel("X: 0.00  Y: 0.00 mm");
    m_coordLabel->setMinimumWidth(200);
    m_statusBar->addWidget(m_coordLabel);

    m_statusBar->addPermanentWidget(new QLabel(" Unit: "));
    auto* unitCombo = new QComboBox();
    unitCombo->addItems({"mm", "mil", "in"});
    if (auto* theme = ThemeManager::theme(); theme && theme->type() != PCBTheme::Light) {
        unitCombo->setStyleSheet("QComboBox { background: #1e1e1e; color: white; border: 1px solid #3c3c3c; font-size: 10px; height: 18px; }");
    }
    connect(unitCombo, &QComboBox::currentTextChanged, this, [this](const QString& unit) {
        m_view->setProperty("currentUnit", unit);
        updateCoordinates(m_view->mapToScene(m_view->mapFromGlobal(QCursor::pos())));
    });
    m_statusBar->addPermanentWidget(unitCombo);

    m_statusBar->addPermanentWidget(new QLabel("  ⊞ "));
    m_gridLabel = new QLabel("Grid: 15.0");
    m_statusBar->addPermanentWidget(m_gridLabel);
    
    m_statusBar->showMessage("Ready", 3000);
}

void SymbolEditor::updatePinPreview(QPointF pos) {
    if (m_currentTool != Pin) return;

    if (m_previewItem) {
        if (m_previewItem->scene()) m_scene->removeItem(m_previewItem);
        delete m_previewItem;
        m_previewItem = nullptr;
    }

    SymbolPrimitive tempPin = SymbolPrimitive::createPin(pos, 0, "", m_previewOrientation);
    m_previewItem = buildVisual(tempPin, -1);
    if (m_previewItem) {
        m_previewItem->setOpacity(0.5);
        m_previewItem->setZValue(1000);
        // Ensure preview doesn't interfere with mouse events
        m_previewItem->setAcceptedMouseButtons(Qt::NoButton);
        m_previewItem->setFlag(QGraphicsItem::ItemIsSelectable, false);
        m_previewItem->setFlag(QGraphicsItem::ItemIsMovable, false);
        m_scene->addItem(m_previewItem);
    }
}

void SymbolEditor::updateCoordinates(QPointF pos) {
    if (m_coordLabel) {
        QString unit = m_view->property("currentUnit").toString();
        if (unit.isEmpty()) unit = "mm";

        double factor = 1.0;
        if (unit == "mil") factor = 39.3701;
        else if (unit == "in") factor = 0.0393701;

        m_coordLabel->setText(QString("X: %1  Y: %2 %3")
            .arg(pos.x() * factor, 0, 'f', unit == "mm" ? 2 : 1)
            .arg(pos.y() * factor, 0, 'f', unit == "mm" ? 2 : 1)
            .arg(unit));
    }
}

void SymbolEditor::onGridSizeChanged(const QString& size) {
    bool ok;
    double val = size.toDouble(&ok);
    if (ok && val > 0) {
        m_view->setGridSize(val);
        if (m_gridLabel) m_gridLabel->setText(QString("Grid: %1").arg(val));
    }
}

void SymbolEditor::onCopyToAlternateStyle() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;

    SymbolDefinition oldDef = symbolDefinition();
    SymbolDefinition newDef = oldDef;
    int count = 0;

    for (auto* item : selected) {
        int idx = primitiveIndex(item);
        if (idx != -1) {
            SymbolPrimitive copy = oldDef.primitives().at(idx);
            copy.setBodyStyle(2); // Assign to Alternate Style
            newDef.addPrimitive(copy);
            count++;
        }
    }

    if (count > 0) {
        m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Copy to Alternate Style"));
        statusBar()->showMessage(QString("Copied %1 items to Alternate Style").arg(count), 3000);
    }
}

void SymbolEditor::onUnitChanged(int index) {
    if (!m_unitCombo) return;
    m_currentUnit = m_unitCombo->itemData(index).toInt();
    applySymbolDefinition(m_symbol);
    statusBar()->showMessage(QString("Switched to %1").arg(m_unitCombo->currentText()), 2000);
}

void SymbolEditor::createSymbolInfoPanel() {
    m_nameEdit        = new QLineEdit();
    m_descriptionEdit = new QLineEdit();
    m_categoryCombo   = new QComboBox();
    m_prefixEdit      = new QLineEdit("U");
    m_modelSourceCombo = new QComboBox();
    m_modelPathEdit = new QLineEdit();
    m_modelNameEdit = new QLineEdit();

    m_nameEdit->setPlaceholderText("Enter symbol name");
    m_descriptionEdit->setPlaceholderText("Description");
    m_categoryCombo->setEditable(true);
    m_categoryCombo->addItems({"Passives", "Semiconductors",
                               "Integrated Circuits", "Connectors",
                               "Power", "Other"});
    m_prefixEdit->setMaximumWidth(50);
    m_modelPathEdit->setPlaceholderText("e.g. sub/my_model.lib or cmp/standard.cmp");
    m_modelNameEdit->setPlaceholderText("Model/Subckt name (e.g. 2N3904)");
    m_modelSourceCombo->addItem("None", "none");
    m_modelSourceCombo->addItem("Library Root (sym/sub/cmp/lib)", "library");
    m_modelSourceCombo->addItem("Project Relative", "project");
    m_modelSourceCombo->addItem("Absolute File", "absolute");

    // Live updates for canvas labels when sidebar edits change
    connect(m_nameEdit, &QLineEdit::textChanged, this, &SymbolEditor::updateOverlayLabels);
    connect(m_prefixEdit, &QLineEdit::textChanged, this, &SymbolEditor::updateOverlayLabels);
    connect(m_nameEdit, &QLineEdit::textChanged, this, &SymbolEditor::updateCodePreview);
    connect(m_prefixEdit, &QLineEdit::textChanged, this, &SymbolEditor::updateCodePreview);
    connect(m_descriptionEdit, &QLineEdit::textChanged, this, &SymbolEditor::updateCodePreview);
    connect(m_categoryCombo, &QComboBox::currentTextChanged, this, &SymbolEditor::updateCodePreview);
    connect(m_modelNameEdit, &QLineEdit::textChanged, this, &SymbolEditor::refreshSubcktMappingStatus);
}

QWidget* SymbolEditor::createSymbolMetadataWidget() {
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(4, 4, 4, 4);
    
    // Apply light theme styling
    bool isLightTheme = ThemeManager::theme() && ThemeManager::theme()->type() == PCBTheme::Light;
    QString lightStyle;
    if (isLightTheme) {
        lightStyle = R"(
            QWidget { background-color: #f8fafc; color: #1f2937; }
            QGroupBox { font-weight: bold; border: 1px solid #cbd5e1; border-radius: 4px; margin-top: 8px; padding-top: 8px; background-color: #ffffff; }
            QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: #374151; }
            QFormLayout { spacing: 6px; }
            QFormLayout > QWidget { background-color: transparent; }
            QLabel { background-color: transparent; color: #374151; }
            QLabel[objectName="metaName"] { font-weight: bold; color: #059669; }
            QTableWidget { background-color: #ffffff; alternate-background-color: #f1f5f9; gridline-color: #e2e8f0; border: 1px solid #cbd5e1; border-radius: 4px; }
            QTableWidget::item { color: #1f2937; }
            QTableWidget::item:selected { background-color: #dbeafe; color: #1e40af; }
            QHeaderView::section { background-color: #f1f5f9; color: #475569; border: none; border-right: 1px solid #e2e8f0; border-bottom: 1px solid #e2e8f0; padding: 4px; font-weight: bold; }
            QScrollArea { background-color: transparent; border: none; }
            QScrollBar:vertical { background: #f1f5f9; width: 8px; }
            QScrollBar::handle:vertical { background: #cbd5e1; border-radius: 4px; }
        )";
        widget->setStyleSheet(lightStyle);
    }
    
    // Create scroll area
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("QScrollArea { border: none; }");
    
    auto* content = new QWidget();
    auto* form = new QFormLayout(content);
    form->setLabelAlignment(Qt::AlignLeft);
    form->setSpacing(6);
    
    // Basic Info Section
    auto* basicGroup = new QGroupBox("Basic Information");
    auto* basicLayout = new QFormLayout(basicGroup);
    basicLayout->setLabelAlignment(Qt::AlignLeft);
    
    QLabel* nameLabel = new QLabel();
    nameLabel->setObjectName("metaName");
    basicLayout->addRow("Name:", nameLabel);
    
    QLabel* descLabel = new QLabel();
    descLabel->setObjectName("metaDesc");
    descLabel->setWordWrap(true);
    basicLayout->addRow("Description:", descLabel);
    
    QLabel* categoryLabel = new QLabel();
    categoryLabel->setObjectName("metaCategory");
    basicLayout->addRow("Category:", categoryLabel);
    
    QLabel* prefixLabel = new QLabel();
    prefixLabel->setObjectName("metaPrefix");
    basicLayout->addRow("Prefix:", prefixLabel);
    
    QLabel* unitCountLabel = new QLabel();
    unitCountLabel->setObjectName("metaUnits");
    basicLayout->addRow("Unit Count:", unitCountLabel);
    
    form->addRow(basicGroup);
    
    // SPICE Model Section
    auto* spiceGroup = new QGroupBox("SPICE Model");
    auto* spiceLayout = new QFormLayout(spiceGroup);
    
    QLabel* modelNameLabel = new QLabel();
    modelNameLabel->setObjectName("metaModelName");
    modelNameLabel->setWordWrap(true);
    spiceLayout->addRow("Model Name:", modelNameLabel);
    
    QLabel* modelSourceLabel = new QLabel();
    modelSourceLabel->setObjectName("metaModelSource");
    spiceLayout->addRow("Source:", modelSourceLabel);
    
    QLabel* modelPathLabel = new QLabel();
    modelPathLabel->setObjectName("metaModelPath");
    modelPathLabel->setWordWrap(true);
    spiceLayout->addRow("Path:", modelPathLabel);

    m_subcktMappingTable = new QTableWidget(0, 4);
    m_subcktMappingTable->setHorizontalHeaderLabels({"Symbol Pin", "Symbol Name", "Subckt Pin", "Status"});
    m_subcktMappingTable->horizontalHeader()->setStretchLastSection(false);
    m_subcktMappingTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_subcktMappingTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_subcktMappingTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_subcktMappingTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_subcktMappingTable->verticalHeader()->setVisible(false);
    m_subcktMappingTable->setMinimumHeight(180);
    spiceLayout->addRow("Node Mapping:", m_subcktMappingTable);
    auto* mappingButtonRow = new QHBoxLayout();
    auto* smartMapButton = new QPushButton("Smart Map");
    auto* orderedMapButton = new QPushButton("Map By Order");
    auto* clearMapButton = new QPushButton("Clear Mapping");
    mappingButtonRow->addWidget(smartMapButton);
    mappingButtonRow->addWidget(orderedMapButton);
    mappingButtonRow->addWidget(clearMapButton);
    mappingButtonRow->addStretch(1);
    spiceLayout->addRow(QString(), mappingButtonRow);
    m_subcktMappingSummaryLabel = new QLabel();
    m_subcktMappingSummaryLabel->setWordWrap(true);
    m_subcktMappingSummaryLabel->setTextFormat(Qt::PlainText);
    spiceLayout->addRow("Mapping Summary:", m_subcktMappingSummaryLabel);
    m_subcktSyncLabel = new QLabel();
    m_subcktSyncLabel->setWordWrap(true);
    m_subcktSyncLabel->setTextFormat(Qt::PlainText);
    spiceLayout->addRow("Sync Status:", m_subcktSyncLabel);
    connect(m_subcktMappingTable, &QTableWidget::cellChanged, this, [this](int, int) {
        markSubcktMappingSynchronized();
    });
    connect(smartMapButton, &QPushButton::clicked, this, &SymbolEditor::onApplySmartSubcktMapping);
    connect(orderedMapButton, &QPushButton::clicked, this, &SymbolEditor::onApplyOrderedSubcktMapping);
    connect(clearMapButton, &QPushButton::clicked, this, &SymbolEditor::onClearSubcktMapping);
     
    form->addRow(spiceGroup);
    
    // Physical Info Section
    auto* physicalGroup = new QGroupBox("Physical");
    auto* physicalLayout = new QFormLayout(physicalGroup);
    
    QLabel* footprintLabel = new QLabel();
    footprintLabel->setObjectName("metaFootprint");
    physicalLayout->addRow("Default Footprint:", footprintLabel);
    
    QLabel* datasheetLabel = new QLabel();
    datasheetLabel->setObjectName("metaDatasheet");
    datasheetLabel->setOpenExternalLinks(true);
    physicalLayout->addRow("Datasheet:", datasheetLabel);
    
    form->addRow(physicalGroup);
    
    // Custom Fields Section
    auto* fieldsGroup = new QGroupBox("Custom Fields");
    auto* fieldsLayout = new QVBoxLayout(fieldsGroup);
    
    auto* fieldsTable = new QTableWidget(0, 2);
    fieldsTable->setObjectName("metaFieldsTable");
    fieldsTable->setHorizontalHeaderLabels({"Field", "Value"});
    fieldsTable->horizontalHeader()->setStretchLastSection(true);
    fieldsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    fieldsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    fieldsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    fieldsLayout->addWidget(fieldsTable);
    
    form->addRow(fieldsGroup);
    
    layout->addWidget(scroll);
    
    return widget;
}

void SymbolEditor::createLibraryBrowser() {
    connect(&SymbolLibraryManager::instance(), &SymbolLibraryManager::librariesChanged,
            this, &SymbolEditor::populateLibraryTree);
    if (SymbolLibraryManager::instance().libraries().isEmpty()) {
        SymbolLibraryManager::instance().loadUserLibraries(QDir::homePath() + "/ViospiceLib/sym", true);
    }

    m_libSearchEdit = new QLineEdit();
    m_libSearchEdit->setPlaceholderText("Search symbols…");
    m_libSearchEdit->setClearButtonEnabled(true);
    connect(m_libSearchEdit, &QLineEdit::textChanged,
            this, &SymbolEditor::onLibSearchChanged);

    m_libraryTree = new QTreeWidget();
    m_libraryTree->setHeaderHidden(true);
    m_libraryTree->setMinimumHeight(200);
    m_libraryTree->setAnimated(true);
    m_libraryTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_libraryTree, &QTreeWidget::customContextMenuRequested, this, &SymbolEditor::onLibraryContextMenu);
    
    connect(m_libraryTree, &QTreeWidget::itemDoubleClicked,
            this, &SymbolEditor::onCloneSymbol);
    connect(m_libraryTree, &QTreeWidget::itemClicked,
            this, &SymbolEditor::onLibraryItemClicked);

    m_libPreviewScene = new QGraphicsScene(this);
    m_libPreviewView = new QGraphicsView(m_libPreviewScene);
    m_libPreviewView->setFixedHeight(180);
    m_libPreviewView->setRenderHint(QPainter::Antialiasing);
    m_libPreviewView->setFrameShape(QFrame::NoFrame);

    populateLibraryTree();
}

void SymbolEditor::createWizardPanel() {
    m_wizardTemplateSearchEdit = new QLineEdit();
    m_wizardTemplateSearchEdit->setPlaceholderText("Search templates (AND, NAND, XOR, IC, custom...)");
    m_wizardTemplateSearchEdit->setClearButtonEnabled(true);

    m_wizardTemplateCombo = new QComboBox();
    m_wizardTemplateCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_wizardTemplateCombo->setMinimumContentsLength(16);

    m_wizardTemplateInfoLabel = new QLabel("No template selected");
    m_wizardTemplateInfoLabel->setStyleSheet("font-weight: 600;");
    m_wizardTemplateDescLabel = new QLabel();
    m_wizardTemplateDescLabel->setWordWrap(true);
    m_wizardTemplateDescLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_wizardPreviewScene = new QGraphicsScene(this);
    m_wizardPreviewView = new QGraphicsView(m_wizardPreviewScene);
    m_wizardPreviewView->setMinimumHeight(180);
    m_wizardPreviewView->setRenderHint(QPainter::Antialiasing, true);
    m_wizardPreviewView->setFrameShape(QFrame::StyledPanel);
    m_wizardPreviewView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_wizardPreviewView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_wizardPreviewView->setDragMode(QGraphicsView::NoDrag);

    m_wizardStyleCombo = new QComboBox();
    m_wizardStyleCombo->addItems({"Dual (DIP/SOIC)", "Quad (QFP/QFN)"});

    m_pinCountSpin = new QSpinBox();
    m_pinCountSpin->setRange(2, 512);
    m_pinCountSpin->setValue(8);

    m_pinSpacingSpin = new QDoubleSpinBox();
    m_pinSpacingSpin->setRange(1, 50);
    m_pinSpacingSpin->setSingleStep(2.54);
    m_pinSpacingSpin->setValue(10.0);
    m_pinSpacingSpin->setSuffix(" units");

    m_bodyWidthSpin = new QDoubleSpinBox();
    m_bodyWidthSpin->setRange(5, 500);
    m_bodyWidthSpin->setValue(50.0);
    m_bodyWidthSpin->setSuffix(" units");

    connect(m_wizardTemplateSearchEdit, &QLineEdit::textChanged,
            this, &SymbolEditor::onWizardTemplateSearchChanged);
    connect(m_wizardTemplateCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { onWizardApplyTemplate(); });
    refreshWizardTemplateList();
    if (m_wizardTemplateCombo->count() > 0) {
        m_wizardTemplateCombo->setCurrentIndex(0);
    }
    onWizardApplyTemplate();
    updateWizardTemplatePreview();
}

void SymbolEditor::createPinTable() {
    m_pinTable = new QTableWidget(0, 7);
    m_pinTable->setHorizontalHeaderLabels({"Number", "Name", "Type", "Orientation", "Length", "Swap", "Alts"});
    m_pinTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_pinTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pinTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_pinTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_pinTable, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        menu.setStyleSheet(ThemeManager::theme() ? ThemeManager::theme()->widgetStylesheet() : "");
        
        QAction* ren = menu.addAction("Renumber 1..N");
        QAction* sort = menu.addAction("Auto-sort by Position");
        QAction* sortNum = menu.addAction("Auto-sort by Number");
        menu.addSeparator();
        QAction* stack = menu.addAction("Stack Selected Pins");
        menu.addSeparator();
        QAction* dist = menu.addAction("Distribute Evenly");
        
        QAction* selected = menu.exec(m_pinTable->mapToGlobal(pos));
        if (selected == ren) onPinRenumberSequential();
        else if (selected == sort) onPinDistributeSelected(); 
        else if (selected == sortNum) onPinSortByNumber();
        else if (selected == stack) onPinStackSelected();
        else if (selected == dist) onPinDistributeSelected();
    });
    connect(m_pinTable, &QTableWidget::cellChanged, this, &SymbolEditor::onPinTableItemChanged);
}

// ─────────────────────────────────────────────────────────────────────────────
//  SymbolEditor – Library
// ─────────────────────────────────────────────────────────────────────────────

void SymbolEditor::populateLibraryTree() {
    m_libraryTree->clear();
    
    QList<SymbolLibrary*> allLibs = SymbolLibraryManager::instance().libraries();
    
    // Pass 1: User / Project Libraries (Non-Built-in)
    for (SymbolLibrary* lib : allLibs) {
        if (lib->isBuiltIn()) continue;
        
        auto* libItem = new QTreeWidgetItem(m_libraryTree, {lib->name()});
        libItem->setIcon(0, getThemeIcon(":/icons/folder_closed.svg"));
        libItem->setForeground(0, QBrush(QColor("#fbbf24"))); // Amber for user libs

        for (const QString& cat : lib->categories()) {
            QList<SymbolDefinition*> syms = lib->symbolsInCategory(cat);
            if (syms.isEmpty()) continue;

            auto* catItem = new QTreeWidgetItem(libItem, {cat});
            catItem->setIcon(0, getThemeIcon(":/icons/folder_open.svg"));

            for (SymbolDefinition* sym : syms) {
                auto* symItem = new QTreeWidgetItem(catItem, {sym->name()});
                symItem->setIcon(0, getThemeIcon(":/icons/component_file.svg"));
                symItem->setData(0, Qt::UserRole, lib->name());
            }
        }
    }

    // Pass 2: Built-in Libraries
    for (SymbolLibrary* lib : allLibs) {
        if (!lib->isBuiltIn()) continue;

        auto* libItem = new QTreeWidgetItem(m_libraryTree, {lib->name() + " [Built-in]"});
        libItem->setIcon(0, getThemeIcon(":/icons/folder_closed.svg"));
        libItem->setForeground(0, QBrush(QColor("#94a3b8"))); // Grey for built-ins

        for (const QString& cat : lib->categories()) {
            QList<SymbolDefinition*> syms = lib->symbolsInCategory(cat);
            if (syms.isEmpty()) continue;

            auto* catItem = new QTreeWidgetItem(libItem, {cat});
            catItem->setIcon(0, getThemeIcon(":/icons/folder_open.svg"));

            for (SymbolDefinition* sym : syms) {
                auto* symItem = new QTreeWidgetItem(catItem, {sym->name()});
                symItem->setIcon(0, getThemeIcon(":/icons/component_file.svg"));
                symItem->setData(0, Qt::UserRole, lib->name());
            }
        }
    }
    
    m_libraryTree->expandAll();
}

void SymbolEditor::onLibSearchChanged(const QString& text) {
    const QString query = text.trimmed().toLower();

    for (int i = 0; i < m_libraryTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* libItem = m_libraryTree->topLevelItem(i);
        bool libVisible = false;

        for (int j = 0; j < libItem->childCount(); ++j) {
            QTreeWidgetItem* catItem = libItem->child(j);
            bool catVisible = false;

            for (int k = 0; k < catItem->childCount(); ++k) {
                QTreeWidgetItem* symItem = catItem->child(k);
                bool matches = query.isEmpty()
                            || symItem->text(0).toLower().contains(query);
                symItem->setHidden(!matches);
                if (matches) catVisible = true;
            }

            catItem->setHidden(!catVisible && !query.isEmpty());
            if (catVisible) catItem->setExpanded(true);
            if (catVisible) libVisible = true;
        }

        libItem->setHidden(!libVisible && !query.isEmpty());
        if (libVisible) libItem->setExpanded(true);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  SymbolEditor – Wizard
// ─────────────────────────────────────────────────────────────────────────────

void SymbolEditor::refreshWizardTemplateList(const QString& query) {
    if (!m_wizardTemplateCombo) return;

    const QString selectedId = m_wizardTemplateCombo->currentData(Qt::UserRole).toString();
    const QString q = query.trimmed().toLower();
    m_wizardTemplateCombo->clear();

    const QList<WizardTemplateDef> defs = wizardTemplateDefsForProject(m_projectKey);
    for (const WizardTemplateDef& tpl : defs) {
        const QString haystack = (tpl.name + " " + tpl.id + " " + tpl.description + " " + tpl.defaultCategory).toLower();
        if (!q.isEmpty() && !haystack.contains(q)) continue;
        m_wizardTemplateCombo->addItem(QString("%1  [%2]").arg(tpl.name, tpl.defaultCategory), tpl.id);
    }

    if (m_wizardTemplateCombo->count() == 0) {
        if (m_wizardTemplateInfoLabel) m_wizardTemplateInfoLabel->setText("No templates match search.");
        if (m_wizardTemplateDescLabel) m_wizardTemplateDescLabel->clear();
        updateWizardTemplatePreview();
        return;
    }
    const int restoreIdx = m_wizardTemplateCombo->findData(selectedId, Qt::UserRole);
    m_wizardTemplateCombo->setCurrentIndex(restoreIdx >= 0 ? restoreIdx : 0);
    updateWizardTemplatePreview();
}

void SymbolEditor::onWizardTemplateSearchChanged(const QString& text) {
    refreshWizardTemplateList(text);
}

void SymbolEditor::onWizardApplyTemplate() {
    if (!m_wizardTemplateCombo || m_wizardTemplateCombo->count() == 0) return;
    const QList<WizardTemplateDef> defs = wizardTemplateDefsForProject(m_projectKey);
    const QString id = m_wizardTemplateCombo->currentData(Qt::UserRole).toString();
    const WizardTemplateDef* tpl = findWizardTemplate(id, defs);
    if (!tpl) return;

    if (tpl->kind == "ic_dual") {
        m_wizardStyleCombo->setCurrentText("Dual (DIP/SOIC)");
        m_pinCountSpin->setValue(qMax(2, tpl->pins));
        m_pinSpacingSpin->setValue(tpl->pitch);
        m_bodyWidthSpin->setValue(tpl->width);
    } else if (tpl->kind == "ic_quad") {
        m_wizardStyleCombo->setCurrentText("Quad (QFP/QFN)");
        m_pinCountSpin->setValue(qMax(4, tpl->pins));
        m_pinSpacingSpin->setValue(tpl->pitch);
        m_bodyWidthSpin->setValue(tpl->width);
    }

    if (m_wizardTemplateInfoLabel) {
        m_wizardTemplateInfoLabel->setText(
            QString("%1  |  %2  |  %3 pins")
                .arg(tpl->name, tpl->defaultCategory, QString::number(qMax(0, tpl->pins))));
    }
    if (m_wizardTemplateDescLabel) {
        m_wizardTemplateDescLabel->setText(tpl->description.trimmed().isEmpty()
            ? QString("Template ID: %1").arg(tpl->id)
            : QString("%1\nID: %2").arg(tpl->description, tpl->id));
    }

    if (m_nameEdit->text().trimmed().isEmpty()) {
        m_nameEdit->setText(tpl->defaultSymbolName);
    }
    if (m_prefixEdit->text().trimmed().isEmpty()) {
        m_prefixEdit->setText(tpl->defaultPrefix);
    }
    if (m_categoryCombo && m_categoryCombo->findText(tpl->defaultCategory) == -1) {
        m_categoryCombo->addItem(tpl->defaultCategory);
    }
    if (m_categoryCombo) {
        m_categoryCombo->setCurrentText(tpl->defaultCategory);
    }

    if (m_statusBar) {
        m_statusBar->showMessage(QString("Template ready: %1").arg(tpl->name), 2500);
    }

    updateWizardTemplatePreview();
}

void SymbolEditor::updateWizardTemplatePreview() {
    if (!m_wizardPreviewScene || !m_wizardPreviewView) return;
    m_wizardPreviewScene->clear();

    if (!m_wizardTemplateCombo || m_wizardTemplateCombo->count() == 0) {
        return;
    }

    const QString id = m_wizardTemplateCombo->currentData(Qt::UserRole).toString();
    const QList<WizardTemplateDef> defs = wizardTemplateDefsForProject(m_projectKey);
    const WizardTemplateDef* tpl = findWizardTemplate(id, defs);
    if (!tpl) return;

    SymbolDefinition previewDef;
    if (tpl->kind == "logic") {
        previewDef = buildLogicTemplateSymbol(*tpl,
                                             tpl->defaultSymbolName.isEmpty() ? tpl->name : tpl->defaultSymbolName,
                                             tpl->defaultPrefix.isEmpty() ? "U" : tpl->defaultPrefix,
                                             tpl->defaultCategory.isEmpty() ? "Digital" : tpl->defaultCategory);
    } else if (tpl->kind == "symbol" && !tpl->symbolJson.isEmpty()) {
        previewDef = SymbolDefinition::fromJson(tpl->symbolJson);
    } else {
        previewDef = buildIcTemplateSymbol(*tpl,
                                           tpl->defaultSymbolName.isEmpty() ? tpl->name : tpl->defaultSymbolName,
                                           tpl->defaultPrefix.isEmpty() ? "U" : tpl->defaultPrefix,
                                           tpl->defaultCategory.isEmpty() ? "IC" : tpl->defaultCategory);
    }

    for (const SymbolPrimitive& prim : previewDef.primitives()) {
        if (QGraphicsItem* item = buildVisual(prim, -1)) {
            m_wizardPreviewScene->addItem(item);
        }
    }

    QRectF bounds = m_wizardPreviewScene->itemsBoundingRect();
    if (!bounds.isValid() || bounds.isEmpty()) {
        bounds = QRectF(-40, -30, 80, 60);
    }
    bounds = bounds.adjusted(-12, -12, 12, 12);
    m_wizardPreviewScene->setSceneRect(bounds);
    m_wizardPreviewView->fitInView(bounds, Qt::KeepAspectRatio);
}

void SymbolEditor::onWizardGenerate() {
    if (!m_drawnItems.isEmpty()) {
        if (QMessageBox::question(this, "Generate", "This will clear the current symbol. Continue?")
                != QMessageBox::Yes)
            return;
    }

    const QString templateId = m_wizardTemplateCombo
        ? m_wizardTemplateCombo->currentData(Qt::UserRole).toString()
        : QString();
    const QList<WizardTemplateDef> defs = wizardTemplateDefsForProject(m_projectKey);
    const WizardTemplateDef* tpl = findWizardTemplate(templateId, defs);

    if (tpl && tpl->kind == "symbol" && !tpl->symbolJson.isEmpty()) {
        SymbolDefinition oldDef = symbolDefinition();
        SymbolDefinition newDef = SymbolDefinition::fromJson(tpl->symbolJson);
        const QString name = m_nameEdit->text().trimmed();
        const QString prefix = m_prefixEdit->text().trimmed();
        const QString category = m_categoryCombo ? m_categoryCombo->currentText().trimmed() : QString();
        if (!name.isEmpty()) newDef.setName(name);
        if (!prefix.isEmpty()) newDef.setReferencePrefix(prefix);
        if (!category.isEmpty()) newDef.setCategory(category);
        m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Wizard Generate Saved Template"));
        return;
    }

    if (tpl && tpl->kind == "logic") {
        SymbolDefinition oldDef = symbolDefinition();
        const QString name = m_nameEdit->text().trimmed().isEmpty()
            ? tpl->defaultSymbolName
            : m_nameEdit->text().trimmed();
        const QString prefix = m_prefixEdit->text().trimmed().isEmpty()
            ? tpl->defaultPrefix
            : m_prefixEdit->text().trimmed();
        QString category = m_categoryCombo ? m_categoryCombo->currentText().trimmed() : QString();
        if (category.isEmpty()) category = tpl->defaultCategory;

        SymbolDefinition newDef = buildLogicTemplateSymbol(*tpl, name, prefix, category);
        m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Wizard Generate Template"));
        return;
    }

    const int    pins  = m_pinCountSpin->value();
    const qreal  pitch = m_pinSpacingSpin->value();
    const qreal  bW    = m_bodyWidthSpin->value();
    const QString style = m_wizardStyleCombo->currentText();

    SymbolDefinition oldDef = symbolDefinition();
    SymbolDefinition newDef;
    newDef.setName(m_nameEdit->text().isEmpty() ? "New_IC" : m_nameEdit->text());
    newDef.setReferencePrefix(m_prefixEdit->text().isEmpty() ? "U" : m_prefixEdit->text());
    newDef.setCategory(m_categoryCombo->currentText());

    if (style.startsWith("Dual")) {
        const int   half    = pins / 2;
        const qreal bHeight = qMax(2.0 * pitch, half * pitch + pitch);

        newDef.addPrimitive(SymbolPrimitive::createRect(
            QRectF(-bW/2, -bHeight/2, bW, bHeight), false));

        for (int i = 0; i < half; ++i) {
            qreal y = -bHeight/2 + pitch + i * pitch;
            newDef.addPrimitive(SymbolPrimitive::createPin(
                QPointF(-bW/2 - 15, y), i + 1, QString::number(i + 1), "Right", 15));
        }
        for (int i = 0; i < half; ++i) {
            qreal y = bHeight/2 - pitch - i * pitch;
            newDef.addPrimitive(SymbolPrimitive::createPin(
                QPointF(bW/2 + 15, y), half + i + 1,
                QString::number(half + i + 1), "Left", 15));
        }
        // Remaining pins (odd total) on left side
        if (pins % 2 != 0) {
            newDef.addPrimitive(SymbolPrimitive::createPin(
                QPointF(-bW/2 - 15, bHeight/2 - pitch - half * pitch),
                pins, QString::number(pins), "Right", 15));
        }
    } else {
        // Quad
        const int   perSide = qMax(1, pins / 4);
        const qreal side    = qMax(2.0 * pitch, perSide * pitch + pitch);

        newDef.addPrimitive(SymbolPrimitive::createRect(
            QRectF(-side/2, -side/2, side, side), false));

        int pinNum = 1;
        // Left side (top→bottom)
        for (int i = 0; i < perSide; ++i) {
            qreal y = -side/2 + pitch + i * pitch;
            newDef.addPrimitive(SymbolPrimitive::createPin(
                QPointF(-side/2 - 15, y), pinNum, QString::number(pinNum), "Right", 15));
            ++pinNum;
        }
        // Bottom (left→right)
        for (int i = 0; i < perSide; ++i) {
            qreal x = -side/2 + pitch + i * pitch;
            newDef.addPrimitive(SymbolPrimitive::createPin(
                QPointF(x, side/2 + 15), pinNum, QString::number(pinNum), "Up", 15));
            ++pinNum;
        }
        // Right side (bottom→top)
        for (int i = 0; i < perSide; ++i) {
            qreal y = side/2 - pitch - i * pitch;
            newDef.addPrimitive(SymbolPrimitive::createPin(
                QPointF(side/2 + 15, y), pinNum, QString::number(pinNum), "Left", 15));
            ++pinNum;
        }
        // Top (right→left)
        for (int i = 0; i < perSide; ++i) {
            qreal x = side/2 - pitch - i * pitch;
            newDef.addPrimitive(SymbolPrimitive::createPin(
                QPointF(x, -side/2 - 15), pinNum, QString::number(pinNum), "Down", 15));
            ++pinNum;
        }
    }

    m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Wizard Generate IC"));
}

void SymbolEditor::onWizardSaveTemplate() {
    SymbolDefinition current = symbolDefinition();
    if (current.primitives().isEmpty()) {
        QMessageBox::warning(this, "Save Wizard Template", "Current symbol is empty.");
        return;
    }

    const QString defaultName = m_nameEdit->text().trimmed().isEmpty()
        ? QStringLiteral("Custom Symbol")
        : m_nameEdit->text().trimmed();

    bool ok = false;
    const QString templateName = QInputDialog::getText(
        this,
        "Save Wizard Template",
        "Template name:",
        QLineEdit::Normal,
        defaultName,
        &ok).trimmed();
    if (!ok || templateName.isEmpty()) return;

    current.setName(m_nameEdit->text().trimmed().isEmpty() ? templateName : m_nameEdit->text().trimmed());
    current.setDescription(m_descriptionEdit->text().trimmed());
    current.setReferencePrefix(m_prefixEdit->text().trimmed().isEmpty() ? "U" : m_prefixEdit->text().trimmed());
    if (m_categoryCombo && !m_categoryCombo->currentText().trimmed().isEmpty()) {
        current.setCategory(m_categoryCombo->currentText().trimmed());
    }

    int pinCount = 0;
    for (const SymbolPrimitive& prim : current.primitives()) {
        if (prim.type == SymbolPrimitive::Pin) ++pinCount;
    }

    WizardTemplateDef tpl;
    tpl.id = uniqueWizardTemplateId(m_projectKey, sanitizeWizardTemplateId(templateName));
    tpl.name = templateName;
    tpl.description = current.description().trimmed().isEmpty()
        ? QString("Saved template from symbol \"%1\"").arg(current.name())
        : current.description();
    tpl.kind = "symbol";
    tpl.defaultCategory = current.category().trimmed().isEmpty() ? "Custom" : current.category().trimmed();
    tpl.defaultPrefix = current.referencePrefix().trimmed().isEmpty() ? "U" : current.referencePrefix().trimmed();
    tpl.defaultSymbolName = current.name().trimmed().isEmpty() ? templateName : current.name().trimmed();
    tpl.pins = pinCount;
    tpl.symbolJson = current.toJson();

    QString error;
    if (!upsertWizardTemplate(m_projectKey, tpl, &error)) {
        QMessageBox::critical(this, "Save Wizard Template",
                              error.isEmpty() ? "Failed to save template." : error);
        return;
    }

    refreshWizardTemplateList(m_wizardTemplateSearchEdit ? m_wizardTemplateSearchEdit->text() : QString());
    if (m_wizardTemplateCombo) {
        const int idx = m_wizardTemplateCombo->findData(tpl.id, Qt::UserRole);
        if (idx >= 0) m_wizardTemplateCombo->setCurrentIndex(idx);
    }

    if (m_statusBar) {
        m_statusBar->showMessage(QString("Saved wizard template: %1").arg(tpl.name), 3500);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  SymbolEditor – Edit operations (New / Clone / Clear / Delete / Undo / Redo)
// ─────────────────────────────────────────────────────────────────────────────

void SymbolEditor::onNewSymbol() {
    if (!m_drawnItems.isEmpty()) {
        if (QMessageBox::question(this, "New Symbol",
                "Current symbol is not empty. Discard changes and create new?")
                != QMessageBox::Yes)
            return;
    }

    m_undoStack->clear();
    clearScene();
    m_symbol = SymbolDefinition();
    m_nameEdit->clear();
    m_descriptionEdit->clear();
    m_prefixEdit->setText("U");
}

void SymbolEditor::onCloneSymbol(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column)
    if (!item || item->childCount() > 0) return; // leaf nodes only

    const QString symName = item->text(0);
    const QString libName = item->data(0, Qt::UserRole).toString();

    SymbolLibrary* lib = SymbolLibraryManager::instance().findLibrary(libName);
    if (!lib) return;

    SymbolDefinition* source = lib->findSymbol(symName);
    if (!source) return;

    if (!m_drawnItems.isEmpty() && !m_undoStack->isClean()) {
        if (QMessageBox::question(this, "Load Symbol",
                "Current symbol has unsaved changes. Discard and load selected symbol?")
                != QMessageBox::Yes)
            return;
    }

    setSymbolDefinition(*source);
}

void SymbolEditor::onClear() {
    if (m_drawnItems.isEmpty()) return;
    if (QMessageBox::question(this, "Clear All",
            "Are you sure you want to delete all primitives?")
            != QMessageBox::Yes)
        return;

    SymbolDefinition oldDef = symbolDefinition();
    SymbolDefinition newDef = oldDef;
    newDef.clearPrimitives();
    m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Clear All"));
}

void SymbolEditor::onDelete() {
    // Collect valid primitive indices, sort descending to avoid index shifting
    QList<int> toDelete;
    // Walk our primitive visuals directly, so text primitives are always handled.
    for (QGraphicsItem* vis : m_drawnItems) {
        if (!vis || !vis->isSelected()) continue;
        int idx = primitiveIndex(vis);
        if (idx != -1 && !toDelete.contains(idx)) toDelete.append(idx);
    }
    // Fallback for selections that hit child items of primitive groups.
    if (toDelete.isEmpty()) {
        QList<QGraphicsItem*> selected = m_scene->selectedItems();
        for (QGraphicsItem* item : selected) {
            int idx = primitiveIndex(item);
            if (idx != -1 && !toDelete.contains(idx)) toDelete.append(idx);
        }
    }
    if (toDelete.isEmpty()) return;

    std::sort(toDelete.begin(), toDelete.end(), std::greater<int>());

    SymbolDefinition oldDef = symbolDefinition();
    SymbolDefinition newDef = oldDef;
    for (int idx : toDelete) {
        newDef.removePrimitive(idx);
    }
    
    m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Delete Items"));
}

void SymbolEditor::onUndo() { if (m_undoStack) m_undoStack->undo(); }
void SymbolEditor::onRedo() { if (m_undoStack) m_undoStack->redo(); }

void SymbolEditor::onToolSelected() {
    auto* action = qobject_cast<QAction*>(sender());
    if (!action) return;
    m_currentTool = static_cast<Tool>(action->data().toInt());
    
    // Immediate action for Image import
    if (m_currentTool == Image) {
        onImportImage();
        if (m_selectAction) m_selectAction->trigger(); // Revert to select
        return;
    }

    // Clear pen state when switching tools
    if (m_currentTool != Pen) {
        clearPenState();
    }
    
    m_view->setCurrentTool(static_cast<int>(m_currentTool));
    m_view->setDragMode(m_currentTool == Select ? QGraphicsView::RubberBandDrag
                                                : QGraphicsView::NoDrag);
    m_polyPoints.clear();
    if (m_previewItem) {
        m_scene->removeItem(m_previewItem);
        delete m_previewItem;
        m_previewItem = nullptr;
    }
}

void SymbolEditor::onItemErased(QGraphicsItem* item) {
    int idx = primitiveIndex(item);
    if (idx != -1) {
        SymbolDefinition oldDef = symbolDefinition();
        SymbolDefinition newDef = oldDef;
        newDef.removePrimitive(idx);
        m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Erase Item"));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  SymbolEditor – Save
// ─────────────────────────────────────────────────────────────────────────────

bool SymbolEditor::saveSymbolToCurrentFlow() {
    QStringList errors;
    QStringList warnings;
    if (!validateCurrentSymbolForSave(&errors, &warnings)) {
        QMessageBox::warning(this, "Save Symbol", errors.join("\n"));
        return false;
    }
    if (!warnings.isEmpty()) {
        const auto choice = QMessageBox::warning(this,
                                                 "Save Symbol",
                                                 warnings.join("\n") + "\n\nSave anyway?",
                                                 QMessageBox::Yes | QMessageBox::No,
                                                 QMessageBox::No);
        if (choice != QMessageBox::Yes) return false;
    }
    m_symbol = symbolDefinition();
    m_lastSaveTarget = SaveTarget::CurrentFlow;
    Q_EMIT symbolSaved(m_symbol);
    if (m_undoStack) m_undoStack->setClean();
    return true;
}

bool SymbolEditor::promptForSaveTarget() {
    QMessageBox msg(this);
    msg.setWindowTitle("Save Symbol");
    msg.setText("Choose where to save this symbol.");
    QPushButton* currentBtn = msg.addButton("Save to Project", QMessageBox::AcceptRole);
    QPushButton* libraryBtn = msg.addButton("Save to Library", QMessageBox::AcceptRole);
    msg.addButton(QMessageBox::Cancel);
    msg.setDefaultButton(currentBtn);
    msg.exec();

    QAbstractButton* clicked = msg.clickedButton();
    if (clicked == currentBtn) return saveSymbolToCurrentFlow();
    if (clicked == libraryBtn) return saveSymbolToLibrary();
    return false;
}

void SymbolEditor::onSave() {
    switch (m_lastSaveTarget) {
    case SaveTarget::CurrentFlow:
        saveSymbolToCurrentFlow();
        break;
    case SaveTarget::Library:
        saveSymbolToLibrary();
        break;
    case SaveTarget::None:
    default:
        promptForSaveTarget();
        break;
    }
}

void SymbolEditor::onSaveToLibrary() {
    saveSymbolToLibrary();
}

bool SymbolEditor::saveSymbolToLibrary() {
    if (m_nameEdit->text().trimmed().isEmpty()) {
        bool ok = false;
        QString name = QInputDialog::getText(this, "Symbol Name",
                                             "Enter symbol name:", QLineEdit::Normal,
                                             "", &ok);
        if (!ok || name.trimmed().isEmpty()) {
            QMessageBox::warning(this, "Error", "Please enter a symbol name.");
            return false;
        }
        m_nameEdit->setText(name.trimmed());
    }

    QStringList errors;
    QStringList warnings;
    if (!validateCurrentSymbolForSave(&errors, &warnings)) {
        QMessageBox::warning(this, "Save to Library", errors.join("\n"));
        return false;
    }
    if (!warnings.isEmpty()) {
        const auto choice = QMessageBox::warning(this,
                                                 "Save to Library",
                                                 warnings.join("\n") + "\n\nSave anyway?",
                                                 QMessageBox::Yes | QMessageBox::No,
                                                 QMessageBox::No);
        if (choice != QMessageBox::Yes) return false;
    }

    m_symbol = symbolDefinition();
    m_symbol.setName(m_nameEdit->text().trimmed());

    const QString symBaseDir = QDir::homePath() + "/ViospiceLib/sym";
    QDir symDir(symBaseDir);
    if (!symDir.exists()) symDir.mkpath(".");

    QStringList subDirs;
    for (const QFileInfo& fi : QDir(symBaseDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        subDirs.append(fi.fileName());
    }
    subDirs.sort();

    QStringList sclibNames;
    for (SymbolLibrary* lib : SymbolLibraryManager::instance().libraries()) {
        if (!lib->isBuiltIn()) sclibNames.append(lib->name());
    }

    QStringList options;
    options << "[ Create New Category... ]";
    for (const QString& d : subDirs) options << QString("[Dir] %1").arg(d);
    if (!sclibNames.isEmpty()) {
        options << "---- .sclib Libraries ----";
        for (const QString& n : sclibNames) options << QString("[Lib] %1").arg(n);
    }

    bool ok = false;
    QString selection = QInputDialog::getItem(this, "Save to Library",
                            "Select category folder:", options, 1, false, &ok);
    if (!ok || selection.isEmpty()) return false;

    if (selection == "[ Create New Category... ]") {
        bool ok2 = false;
        QString dirName = QInputDialog::getText(this, "New Category",
                                "Category name (used as folder):",
                                QLineEdit::Normal, "", &ok2);
        if (!ok2 || dirName.trimmed().isEmpty()) return false;
        dirName = dirName.trimmed();
        QDir(symBaseDir).mkpath(dirName);
        selection = QString("[Dir] %1").arg(dirName);
    }

    if (selection.startsWith("[Dir] ")) {
        QString dirName = selection.mid(QString("[Dir] ").size()).trimmed();
        QString dirPath = symBaseDir + "/" + dirName;
        QDir().mkpath(dirPath);

        m_symbol.setCategory(dirName);

        QString symFile = dirPath + "/" + m_symbol.name() + ".viosym";
        QJsonDocument doc(m_symbol.toJson());
        QFile file(symFile);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QMessageBox::warning(this, "Error",
                QString("Failed to write:\n%1").arg(symFile));
            return false;
        }
        file.write(doc.toJson(QJsonDocument::Indented));

        LibraryIndex::instance().addSymbol(m_symbol.name(), dirName, dirName);
        QMessageBox::information(this, "Saved",
            QString("'%1' saved to %2/").arg(m_symbol.name(), dirName));
        Q_EMIT symbolSaved(m_symbol);
        if (m_undoStack) m_undoStack->setClean();
        populateLibraryTree();
        m_lastSaveTarget = SaveTarget::Library;
        return true;
    }

    if (selection.startsWith("----")) return false;

    QString libName = selection;
    libName.remove(0, QString("[Lib] ").size());
    libName = libName.trimmed();
    SymbolLibrary* lib = SymbolLibraryManager::instance().findLibrary(libName);
    if (!lib) return false;

    lib->addSymbol(m_symbol);
    if (!lib->save()) return false;

    LibraryIndex::instance().addSymbol(m_symbol.name(), lib->name(), m_symbol.category());
    QMessageBox::information(this, "Saved",
        QString("Symbol '%1' saved to library '%2'.").arg(m_symbol.name(), libName));
    Q_EMIT symbolSaved(m_symbol);
    if (m_undoStack) m_undoStack->setClean();
    populateLibraryTree();
    m_lastSaveTarget = SaveTarget::Library;
    return true;
}

void SymbolEditor::onExportVioSym() {
    if (m_nameEdit->text().trimmed().isEmpty()) {
        bool ok = false;
        QString name = QInputDialog::getText(this, "Symbol Name",
                                             "Enter symbol name:", QLineEdit::Normal,
                                             "", &ok);
        if (!ok || name.trimmed().isEmpty()) {
            QMessageBox::warning(this, "Export", "Please enter a symbol name before exporting.");
            return;
        }
        m_nameEdit->setText(name.trimmed());
    }

    QString baseDir = QDir::homePath() + "/ViospiceLib/sym";
    QDir().mkpath(baseDir);
    QString suggested = QDir(baseDir).filePath(m_nameEdit->text().trimmed() + ".viosym");

    QString path = runThemedSaveFileDialog(this, "Export Symbol (.viosym)", "viospice Symbol (*.viosym)", suggested);
    if (path.isEmpty()) return;
    if (!path.endsWith(".viosym", Qt::CaseInsensitive)) path += ".viosym";

    SymbolDefinition def = symbolDefinition();
    QJsonDocument doc(def.toJson());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, "Export Failed", "Could not write symbol file.");
        return;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    SymbolLibraryManager::instance().reloadUserLibraries();
    populateLibraryTree();
    statusBar()->showMessage("Exported symbol to " + path, 4000);
    if (m_undoStack) m_undoStack->setClean();
}

void SymbolEditor::onRefreshLibraries() {
    SymbolLibraryManager::instance().reloadUserLibraries();
    populateLibraryTree();
    statusBar()->showMessage("Libraries refreshed.", 3000);
}

void SymbolEditor::onRotateCW() {
    const QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;

    QPointF center(0, 0);
    bool centerFound = false;

    // 1. Prioritize any selected Pin's connection point as the pivot
    for (auto* item : selected) {
        int idx = primitiveIndex(item);
        if (idx != -1) {
            const SymbolPrimitive& prim = m_symbol.primitives().at(idx);
            if (prim.type == SymbolPrimitive::Pin) {
                center = QPointF(prim.data.value("x").toDouble(), prim.data.value("y").toDouble());
                centerFound = true;
                break;
            }
        }
    }

    // 2. Fallback to bounding box center if no pin is selected
    if (!centerFound) {
        if (selected.size() == 1) {
            center = m_view->snapToGrid(selected.first()->sceneBoundingRect().center());
        } else {
            QRectF bbox;
            for (auto* item : selected) bbox = bbox.united(item->sceneBoundingRect());
            center = m_view->snapToGrid(bbox.center());
        }
    }

    SymbolDefinition oldDef = symbolDefinition();
    SymbolDefinition newDef = oldDef;

    for (QGraphicsItem* item : selected) {
        int idx = primitiveIndex(item);
        if (idx != -1 && idx < newDef.primitives().size())
            newDef.primitives()[idx].rotateCW(center);
    }
    m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Rotate CW"));
}

void SymbolEditor::onRotateCCW() {
    const QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;

    QPointF center(0, 0);
    bool centerFound = false;

    for (auto* item : selected) {
        int idx = primitiveIndex(item);
        if (idx != -1) {
            const SymbolPrimitive& prim = m_symbol.primitives().at(idx);
            if (prim.type == SymbolPrimitive::Pin) {
                center = QPointF(prim.data.value("x").toDouble(), prim.data.value("y").toDouble());
                centerFound = true;
                break;
            }
        }
    }

    if (!centerFound) {
        if (selected.size() == 1) {
            center = m_view->snapToGrid(selected.first()->sceneBoundingRect().center());
        } else {
            QRectF bbox;
            for (auto* item : selected) bbox = bbox.united(item->sceneBoundingRect());
            center = m_view->snapToGrid(bbox.center());
        }
    }

    SymbolDefinition oldDef = symbolDefinition();
    SymbolDefinition newDef = oldDef;

    for (QGraphicsItem* item : selected) {
        int idx = primitiveIndex(item);
        if (idx != -1 && idx < newDef.primitives().size())
            newDef.primitives()[idx].rotateCCW(center);
    }
    m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Rotate CCW"));
}

void SymbolEditor::onFlipH() {
    const QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;

    QPointF center(0, 0);
    bool centerFound = false;

    for (auto* item : selected) {
        int idx = primitiveIndex(item);
        if (idx != -1) {
            const SymbolPrimitive& prim = m_symbol.primitives().at(idx);
            if (prim.type == SymbolPrimitive::Pin) {
                center = QPointF(prim.data.value("x").toDouble(), prim.data.value("y").toDouble());
                centerFound = true;
                break;
            }
        }
    }

    if (!centerFound) {
        if (selected.size() == 1) {
            center = m_view->snapToGrid(selected.first()->sceneBoundingRect().center());
        } else {
            QRectF bbox;
            for (auto* item : selected) bbox = bbox.united(item->sceneBoundingRect());
            center = m_view->snapToGrid(bbox.center());
        }
    }

    SymbolDefinition oldDef = symbolDefinition();
    SymbolDefinition newDef = oldDef;

    for (QGraphicsItem* item : selected) {
        int idx = primitiveIndex(item);
        if (idx != -1 && idx < newDef.primitives().size())
            newDef.primitives()[idx].flipH(center);
    }
    m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Flip Horizontal"));
}

void SymbolEditor::onFlipV() {
    const QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;

    QPointF center(0, 0);
    bool centerFound = false;

    for (auto* item : selected) {
        int idx = primitiveIndex(item);
        if (idx != -1) {
            const SymbolPrimitive& prim = m_symbol.primitives().at(idx);
            if (prim.type == SymbolPrimitive::Pin) {
                center = QPointF(prim.data.value("x").toDouble(), prim.data.value("y").toDouble());
                centerFound = true;
                break;
            }
        }
    }

    if (!centerFound) {
        if (selected.size() == 1) {
            center = m_view->snapToGrid(selected.first()->sceneBoundingRect().center());
        } else {
            QRectF bbox;
            for (auto* item : selected) bbox = bbox.united(item->sceneBoundingRect());
            center = m_view->snapToGrid(bbox.center());
        }
    }

    SymbolDefinition oldDef = symbolDefinition();
    SymbolDefinition newDef = oldDef;

    for (QGraphicsItem* item : selected) {
        int idx = primitiveIndex(item);
        if (idx != -1 && idx < newDef.primitives().size())
            newDef.primitives()[idx].flipV(center);
    }
    m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Flip Vertical"));
}

// ─────────────────────────────────────────────────────────────────────────────
//  SymbolEditor – Align / Distribute
// ─────────────────────────────────────────────────────────────────────────────

void SymbolEditor::onAlignLeft() {
    const QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() < 2) return;

    qreal minX = 1e9;
    for (auto* item : selected) minX = qMin(minX, item->sceneBoundingRect().left());

    SymbolDefinition oldDef = symbolDefinition();
    SymbolDefinition newDef = oldDef;

    for (QGraphicsItem* item : selected) {
        int idx = primitiveIndex(item);
        if (idx != -1) {
            qreal dx = minX - item->sceneBoundingRect().left();
            SymbolPrimitive& prim = newDef.primitives()[idx];
            translatePrimitive(prim, dx, 0.0);
        }
    }
    m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Align Left"));
}

void SymbolEditor::onAlignRight() {
    const QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() < 2) return;
    qreal maxX = -1e9;
    for (auto* item : selected) maxX = qMax(maxX, item->sceneBoundingRect().right());
    SymbolDefinition oldDef = symbolDefinition();
    SymbolDefinition newDef = oldDef;
    for (QGraphicsItem* item : selected) {
        int idx = primitiveIndex(item);
        if (idx != -1) {
            qreal dx = maxX - item->sceneBoundingRect().right();
            SymbolPrimitive& prim = newDef.primitives()[idx];
            translatePrimitive(prim, dx, 0.0);
        }
    }
    m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Align Right"));
}

void SymbolEditor::onAlignTop() {
    const QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() < 2) return;
    qreal minY = 1e9;
    for (auto* item : selected) minY = qMin(minY, item->sceneBoundingRect().top());
    SymbolDefinition oldDef = symbolDefinition();
    SymbolDefinition newDef = oldDef;
    for (QGraphicsItem* item : selected) {
        int idx = primitiveIndex(item);
        if (idx != -1) {
            qreal dy = minY - item->sceneBoundingRect().top();
            SymbolPrimitive& prim = newDef.primitives()[idx];
            translatePrimitive(prim, 0.0, dy);
        }
    }
    m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Align Top"));
}

void SymbolEditor::onAlignBottom() {
    const QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() < 2) return;
    qreal maxY = -1e9;
    for (auto* item : selected) maxY = qMax(maxY, item->sceneBoundingRect().bottom());
    SymbolDefinition oldDef = symbolDefinition();
    SymbolDefinition newDef = oldDef;
    for (QGraphicsItem* item : selected) {
        int idx = primitiveIndex(item);
        if (idx != -1) {
            qreal dy = maxY - item->sceneBoundingRect().bottom();
            SymbolPrimitive& prim = newDef.primitives()[idx];
            translatePrimitive(prim, 0.0, dy);
        }
    }
    m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Align Bottom"));
}

void SymbolEditor::onAlignCenterX() {
    const QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() < 2) return;
    QRectF bbox;
    for (auto* item : selected) bbox = bbox.united(item->sceneBoundingRect());
    qreal centerX = bbox.center().x();
    SymbolDefinition oldDef = symbolDefinition();
    SymbolDefinition newDef = oldDef;
    for (QGraphicsItem* item : selected) {
        int idx = primitiveIndex(item);
        if (idx != -1) {
            qreal dx = centerX - item->sceneBoundingRect().center().x();
            SymbolPrimitive& prim = newDef.primitives()[idx];
            translatePrimitive(prim, dx, 0.0);
        }
    }
    m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Center X"));
}

void SymbolEditor::onAlignCenterY() {
    const QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() < 2) return;
    QRectF bbox;
    for (auto* item : selected) bbox = bbox.united(item->sceneBoundingRect());
    qreal centerY = bbox.center().y();
    SymbolDefinition oldDef = symbolDefinition();
    SymbolDefinition newDef = oldDef;
    for (QGraphicsItem* item : selected) {
        int idx = primitiveIndex(item);
        if (idx != -1) {
            qreal dy = centerY - item->sceneBoundingRect().center().y();
            SymbolPrimitive& prim = newDef.primitives()[idx];
            translatePrimitive(prim, 0.0, dy);
        }
    }
    m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Center Y"));
}

void SymbolEditor::onDistributeH() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() < 3) return;
    std::sort(selected.begin(), selected.end(), [](auto* a, auto* b){ return a->sceneBoundingRect().center().x() < b->sceneBoundingRect().center().x(); });
    qreal firstX = selected.first()->sceneBoundingRect().center().x();
    qreal lastX = selected.last()->sceneBoundingRect().center().x();
    qreal step = (lastX - firstX) / (selected.size() - 1);
    SymbolDefinition oldDef = symbolDefinition();
    SymbolDefinition newDef = oldDef;
    for (int i = 1; i < selected.size() - 1; ++i) {
        int idx = primitiveIndex(selected[i]);
        if (idx != -1) {
            qreal dx = (firstX + i * step) - selected[i]->sceneBoundingRect().center().x();
            SymbolPrimitive& prim = newDef.primitives()[idx];
            translatePrimitive(prim, dx, 0.0);
        }
    }
    m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Distribute H"));
}

void SymbolEditor::onDistributeV() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() < 3) return;
    std::sort(selected.begin(), selected.end(), [](auto* a, auto* b){ return a->sceneBoundingRect().center().y() < b->sceneBoundingRect().center().y(); });
    qreal firstY = selected.first()->sceneBoundingRect().center().y();
    qreal lastY = selected.last()->sceneBoundingRect().center().y();
    qreal step = (lastY - firstY) / (selected.size() - 1);
    SymbolDefinition oldDef = symbolDefinition();
    SymbolDefinition newDef = oldDef;
    for (int i = 1; i < selected.size() - 1; ++i) {
        int idx = primitiveIndex(selected[i]);
        if (idx != -1) {
            qreal dy = (firstY + i * step) - selected[i]->sceneBoundingRect().center().y();
            SymbolPrimitive& prim = newDef.primitives()[idx];
            translatePrimitive(prim, 0.0, dy);
        }
    }
    m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Distribute V"));
}

void SymbolEditor::onMatchSpacing() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() < 2) return;

    bool ok;
    double pitch = QInputDialog::getDouble(this, "Match Spacing", "Enter spacing (mm):", 
                                          m_view->gridSize(), 0.1, 100.0, 2, &ok);
    if (!ok) return;

    // Sort items by position
    bool horizontal = true;
    QRectF totalRect;
    for (auto* item : selected) totalRect = totalRect.united(item->sceneBoundingRect());
    if (totalRect.height() > totalRect.width()) horizontal = false;

    if (horizontal) {
        std::sort(selected.begin(), selected.end(), [](auto* a, auto* b){ 
            return a->sceneBoundingRect().center().x() < b->sceneBoundingRect().center().x(); 
        });
    } else {
        std::sort(selected.begin(), selected.end(), [](auto* a, auto* b){ 
            return a->sceneBoundingRect().center().y() < b->sceneBoundingRect().center().y(); 
        });
    }

    SymbolDefinition oldDef = symbolDefinition();
    SymbolDefinition newDef = oldDef;
    QPointF start = selected.first()->sceneBoundingRect().center();

    for (int i = 1; i < selected.size(); ++i) {
        int idx = primitiveIndex(selected[i]);
        if (idx != -1) {
            QPointF target;
            if (horizontal) target = start + QPointF(i * pitch, 0);
            else target = start + QPointF(0, i * pitch);

            QPointF delta = target - selected[i]->sceneBoundingRect().center();
            SymbolPrimitive& prim = newDef.primitives()[idx];
            translatePrimitive(prim, delta.x(), delta.y());
        }
    }
    m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Match Spacing"));
}

void SymbolEditor::onSnapToGrid() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;

    SymbolDefinition oldDef = symbolDefinition();
    SymbolDefinition newDef = oldDef;
    bool anyChanged = false;

    for (auto* item : selected) {
        int idx = primitiveIndex(item);
        if (idx == -1) continue;

        SymbolPrimitive& prim = newDef.primitives()[idx];
        
        // Snap logic depends on type
        if (prim.type == SymbolPrimitive::Pin) {
            QPointF p(prim.data["x"].toDouble(), prim.data["y"].toDouble());
            QPointF snapped = m_view->snapToGrid(p);
            if (p != snapped) {
                prim.data["x"] = snapped.x();
                prim.data["y"] = snapped.y();
                anyChanged = true;
            }
        } else if (prim.type == SymbolPrimitive::Line || prim.type == SymbolPrimitive::Bezier) {
            for (int i = 1; i <= 4; ++i) {
                QString kX = QString("x%1").arg(i);
                QString kY = QString("y%1").arg(i);
                if (prim.data.contains(kX)) {
                    QPointF p(prim.data[kX].toDouble(), prim.data[kY].toDouble());
                    QPointF snapped = m_view->snapToGrid(p);
                    prim.data[kX] = snapped.x();
                    prim.data[kY] = snapped.y();
                    anyChanged = true;
                }
            }
        } else if (prim.type == SymbolPrimitive::Rect || prim.type == SymbolPrimitive::Circle || prim.type == SymbolPrimitive::Arc) {
            QString kX = prim.data.contains("centerX") ? "centerX" : "x";
            QString kY = prim.data.contains("centerY") ? "centerY" : "y";
            QPointF p(prim.data[kX].toDouble(), prim.data[kY].toDouble());
            QPointF snapped = m_view->snapToGrid(p);
            if (p != snapped) {
                prim.data[kX] = snapped.x();
                prim.data[kY] = snapped.y();
                anyChanged = true;
            }
        }
    }

    if (anyChanged) {
        m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Snap to Grid"));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  SymbolEditor – Pin Table
// ─────────────────────────────────────────────────────────────────────────────

void SymbolEditor::onPinTable() {
    QList<int> pinIndices;
    QList<SymbolPrimitive> pins;
    for (int i = 0; i < m_symbol.primitives().size(); ++i) {
        if (m_symbol.primitives()[i].type == SymbolPrimitive::Pin) {
            pinIndices.append(i);
            pins.append(m_symbol.primitives()[i]);
        }
    }

    if (pins.isEmpty()) {
        QMessageBox::information(this, "Pin Table", "No pins found in this symbol.");
        return;
    }

    PinTableDialog dlg(pins, this);
    if (dlg.exec() == QDialog::Accepted) {
        SymbolDefinition oldDef = symbolDefinition();
        SymbolDefinition newDef = oldDef;
        auto res = dlg.results();
        
        if (res.size() == pinIndices.size()) {
            // Update in-place
            for (int i = 0; i < pinIndices.size(); ++i) {
                int idx = pinIndices[i];
                newDef.primitives()[idx].data["name"] = res[i]["name"].toString();
                newDef.primitives()[idx].data["number"] = res[i]["number"].toInt();
                newDef.primitives()[idx].data["orientation"] = res[i]["orientation"].toString();
                newDef.primitives()[idx].data["electricalType"] = res[i]["electricalType"].toString();
                newDef.primitives()[idx].data["visible"] = res[i]["visible"].toBool();
                newDef.primitives()[idx].data["swapGroup"] = res[i]["swapGroup"].toInt();
                newDef.primitives()[idx].data["jumperGroup"] = res[i]["jumperGroup"].toInt();
                newDef.primitives()[idx].data["stackedNumbers"] = res[i]["stackedNumbers"].toString();
                newDef.primitives()[idx].data["alternateNames"] = res[i]["alternateNames"].toString();
            }
        } else {
            // Rebuild all pins (count changed via import)
            QList<SymbolPrimitive> others;
            for (const auto& p : newDef.primitives()) if (p.type != SymbolPrimitive::Pin) others.append(p);
            
            newDef.clearPrimitives();
            for (const auto& p : others) newDef.addPrimitive(p);
            
            for (int i = 0; i < res.size(); ++i) {
                // Heuristic: vertical stack for new pins
                QPointF pos(0, i * 15.0);
                SymbolPrimitive p = SymbolPrimitive::createPin(pos, res[i]["number"].toInt(), res[i]["name"].toString());
                p.data["orientation"] = res[i]["orientation"].toString();
                p.data["electricalType"] = res[i]["electricalType"].toString();
                p.data["visible"] = res[i]["visible"].toBool();
                p.data["swapGroup"] = res[i]["swapGroup"].toInt();
                p.data["jumperGroup"] = res[i]["jumperGroup"].toInt();
                p.data["stackedNumbers"] = res[i]["stackedNumbers"].toString();
                p.data["alternateNames"] = res[i]["alternateNames"].toString();
                newDef.addPrimitive(p);
            }
        }
        m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Batch Edit Pins"));
    }
}

void SymbolEditor::updatePinTable() {
    if (!m_pinTable) return;
    m_pinTable->blockSignals(true);
    m_pinTable->setColumnCount(10);
    m_pinTable->setHorizontalHeaderLabels({"#", "Name", "Type", "Ori", "Len", "Vis", "Swap", "Jumper", "Stacked", "Alts"});
    m_pinTable->setRowCount(0);
    for (int primIdx = 0; primIdx < m_symbol.primitives().size(); ++primIdx) {
        const auto& prim = m_symbol.primitives().at(primIdx);
        if (prim.type == SymbolPrimitive::Pin) {
            int row = m_pinTable->rowCount();
            m_pinTable->insertRow(row);

            auto* numItem = new QTableWidgetItem(QString::number(prim.data["number"].toInt()));
            auto* nameItem = new QTableWidgetItem(prim.data["name"].toString());
            auto* typeItem = new QTableWidgetItem(prim.data.value("electricalType").toString("Passive"));
            auto* oriItem = new QTableWidgetItem(prim.data.value("orientation").toString("Right"));
            auto* lenItem = new QTableWidgetItem(QString::number(prim.data.value("length").toDouble(15.0)));
            auto* visItem = new QTableWidgetItem();
            visItem->setCheckState(prim.data.value("visible").toBool(true) ? Qt::Checked : Qt::Unchecked);
            auto* swapItem = new QTableWidgetItem(QString::number(prim.data.value("swapGroup").toInt(0)));
            auto* jumpItem = new QTableWidgetItem(QString::number(prim.data.value("jumperGroup").toInt(0)));
            auto* stackItem = new QTableWidgetItem(prim.data.value("stackedNumbers").toString());
            auto* altsItem = new QTableWidgetItem(prim.data.value("alternateNames").toString());

            numItem->setData(Qt::UserRole, primIdx);
            nameItem->setData(Qt::UserRole, primIdx);
            typeItem->setData(Qt::UserRole, primIdx);
            oriItem->setData(Qt::UserRole, primIdx);
            lenItem->setData(Qt::UserRole, primIdx);
            visItem->setData(Qt::UserRole, primIdx);
            swapItem->setData(Qt::UserRole, primIdx);
            jumpItem->setData(Qt::UserRole, primIdx);
            stackItem->setData(Qt::UserRole, primIdx);
            altsItem->setData(Qt::UserRole, primIdx);

            m_pinTable->setItem(row, 0, numItem);
            m_pinTable->setItem(row, 1, nameItem);
            m_pinTable->setItem(row, 2, typeItem);
            m_pinTable->setItem(row, 3, oriItem);
            m_pinTable->setItem(row, 4, lenItem);
            m_pinTable->setItem(row, 5, visItem);
            m_pinTable->setItem(row, 6, swapItem);
            m_pinTable->setItem(row, 7, jumpItem);
            m_pinTable->setItem(row, 8, stackItem);
            m_pinTable->setItem(row, 9, altsItem);
        }
    }
    m_pinTable->blockSignals(false);
}

void SymbolEditor::updateSubcktMappingTable() {
    if (!m_subcktMappingTable) return;

    m_subcktMappingTable->blockSignals(true);
    m_subcktMappingTable->setRowCount(0);

    const QMap<int, QString> mapping = m_symbol.spiceNodeMapping();
    QList<const SymbolPrimitive*> pinPrimitives;
    for (const SymbolPrimitive& prim : m_symbol.primitives()) {
        if (prim.type == SymbolPrimitive::Pin) {
            pinPrimitives.append(&prim);
        }
    }

    std::sort(pinPrimitives.begin(), pinPrimitives.end(), [](const SymbolPrimitive* a, const SymbolPrimitive* b) {
        return a->data.value("number").toInt() < b->data.value("number").toInt();
    });

    for (const SymbolPrimitive* prim : pinPrimitives) {
        const int row = m_subcktMappingTable->rowCount();
        m_subcktMappingTable->insertRow(row);

        const int pinNumber = prim->data.value("number").toInt();
        const QString pinName = prim->data.value("name").toString();
        const QString subcktPin = mapping.value(pinNumber, pinName);

        auto* pinNumberItem = new QTableWidgetItem(QString::number(pinNumber));
        pinNumberItem->setFlags(pinNumberItem->flags() & ~Qt::ItemIsEditable);

        auto* pinNameItem = new QTableWidgetItem(pinName);
        pinNameItem->setFlags(pinNameItem->flags() & ~Qt::ItemIsEditable);

        auto* subcktPinItem = new QTableWidgetItem(subcktPin);
        auto* statusItem = new QTableWidgetItem();
        statusItem->setFlags(statusItem->flags() & ~Qt::ItemIsEditable);

        m_subcktMappingTable->setItem(row, 0, pinNumberItem);
        m_subcktMappingTable->setItem(row, 1, pinNameItem);
        m_subcktMappingTable->setItem(row, 2, subcktPinItem);
        m_subcktMappingTable->setItem(row, 3, statusItem);
    }

    m_subcktMappingTable->blockSignals(false);
    refreshSubcktMappingStatus();
}

void SymbolEditor::refreshSubcktMappingStatus() {
    if (!m_subcktMappingTable) return;

    const QString modelName = m_modelNameEdit ? m_modelNameEdit->text().trimmed() : QString();
    const QString currentPinSignature = currentSymbolPinSignature();
    const bool mappingOutOfSync = !m_subcktMappingPinSignature.isEmpty() && currentPinSignature != m_subcktMappingPinSignature;
    QStringList subcktPins;
    QMap<QString, int> subcktIndexByNormalized;
    if (!modelName.isEmpty()) {
        if (const SimSubcircuit* sub = ModelLibraryManager::instance().findSubcircuit(modelName)) {
            for (size_t i = 0; i < sub->pinNames.size(); ++i) {
                const QString pin = QString::fromStdString(sub->pinNames[i]);
                subcktPins << pin;
                const QString normalized = normalizePinNameForStatus(pin);
                if (!normalized.isEmpty() && !subcktIndexByNormalized.contains(normalized)) {
                    subcktIndexByNormalized.insert(normalized, static_cast<int>(i));
                }
            }
        }
    }

    QMap<QString, int> usageCount;
    for (int row = 0; row < m_subcktMappingTable->rowCount(); ++row) {
        if (QTableWidgetItem* subcktPinItem = m_subcktMappingTable->item(row, 2)) {
            const QString key = subcktPinItem->text().trimmed().toUpper();
            if (!key.isEmpty()) usageCount[key] += 1;
        }
    }

    QMap<QString, int> counts;
    auto increment = [&counts](const QString& key) {
        counts[key] = counts.value(key) + 1;
    };

    const bool haveModel = !modelName.isEmpty();
    const bool haveKnownSubckt = !subcktPins.isEmpty();
    for (int row = 0; row < m_subcktMappingTable->rowCount(); ++row) {
        QTableWidgetItem* pinNumberItem = m_subcktMappingTable->item(row, 0);
        QTableWidgetItem* pinNameItem = m_subcktMappingTable->item(row, 1);
        QTableWidgetItem* subcktPinItem = m_subcktMappingTable->item(row, 2);
        QTableWidgetItem* statusItem = m_subcktMappingTable->item(row, 3);
        if (!pinNumberItem || !pinNameItem || !subcktPinItem || !statusItem) continue;

        const QString symbolPin = pinNameItem->text().trimmed();
        const QString mappedPin = subcktPinItem->text().trimmed();
        const QString normalizedSymbol = normalizePinNameForStatus(symbolPin);
        const QString normalizedMapped = normalizePinNameForStatus(mappedPin);

        MappingStatusInfo status;
        if (mappedPin.isEmpty()) {
            status = {"Missing", "No subcircuit pin is assigned for this symbol pin.", QColor("#b91c1c")};
            increment("missing");
        } else if (usageCount.value(mappedPin.toUpper()) > 1) {
            status = {"Duplicate", QString("Subcircuit pin '%1' is assigned more than once.").arg(mappedPin), QColor("#b91c1c")};
            increment("duplicate");
        } else if (!haveModel) {
            status = {"Manual", "Pick a subcircuit model to evaluate mapping quality.", QColor("#475569")};
            increment("manual");
        } else if (!haveKnownSubckt) {
            status = {"Unknown", QString("Model '%1' is not indexed as a subcircuit.").arg(modelName), QColor("#b45309")};
            increment("unknown");
        } else {
            const bool existsByName = std::any_of(subcktPins.begin(), subcktPins.end(), [&](const QString& candidate) {
                return candidate.compare(mappedPin, Qt::CaseInsensitive) == 0;
            });
            const int normalizedIndex = subcktIndexByNormalized.value(normalizedMapped, -1);
            const bool rowMatchesOrder = row < subcktPins.size() && subcktPins.at(row).compare(mappedPin, Qt::CaseInsensitive) == 0;
            const bool normalizedMatchesSymbol = !normalizedSymbol.isEmpty() && normalizedSymbol == normalizedMapped;

            if (existsByName && symbolPin.compare(mappedPin, Qt::CaseInsensitive) == 0) {
                status = {"Exact", "Mapped pin matches the symbol pin name exactly.", QColor("#15803d")};
                increment("exact");
            } else if (normalizedIndex >= 0 && normalizedMatchesSymbol) {
                status = {"Alias", QString("Mapped via normalized alias to subcircuit pin '%1'.").arg(subcktPins.at(normalizedIndex)), QColor("#0f766e")};
                increment("alias");
            } else if (rowMatchesOrder) {
                status = {"Order", "Mapped by pin order rather than by matching names.", QColor("#b45309")};
                increment("order");
            } else if (existsByName || normalizedIndex >= 0) {
                status = {"Manual", "Mapped to a valid subcircuit pin, but not by exact name or order.", QColor("#475569")};
                increment("manual");
            } else {
                status = {"Unknown", QString("Mapped pin '%1' does not exist on subcircuit '%2'.").arg(mappedPin, modelName), QColor("#b91c1c")};
                increment("unknown");
            }
        }

        statusItem->setText(status.label);
        statusItem->setToolTip(status.detail);
        statusItem->setForeground(QBrush(status.color));
        const QColor bg = status.color.lighter(185);
        pinNumberItem->setBackground(QBrush(bg));
        pinNameItem->setBackground(QBrush(bg));
        subcktPinItem->setBackground(QBrush(bg));
        statusItem->setBackground(QBrush(bg));
        pinNumberItem->setForeground(QBrush(status.color.darker(125)));
        pinNameItem->setToolTip(status.detail);
        subcktPinItem->setToolTip(status.detail);
        pinNumberItem->setToolTip(status.detail);
    }

    if (m_subcktMappingSummaryLabel) {
        QStringList parts;
        auto appendCount = [&](const QString& key, const QString& label) {
            const int value = counts.value(key);
            if (value > 0) parts << QString("%1 %2").arg(value).arg(label);
        };
        appendCount("exact", "exact");
        appendCount("alias", "alias");
        appendCount("order", "order fallback");
        appendCount("manual", "manual");
        appendCount("duplicate", "duplicate");
        appendCount("unknown", "unknown");
        appendCount("missing", "missing");

        QString summary;
        if (!haveModel) {
            summary = "Select a subcircuit model to evaluate mapping quality.";
        } else if (!haveKnownSubckt) {
            summary = QString("Model '%1' is not currently indexed as a subcircuit.").arg(modelName);
        } else {
            summary = QString("Subcircuit '%1' has %2 pin(s).").arg(modelName).arg(subcktPins.size());
            if (!parts.isEmpty()) summary += "  Status: " + parts.join(", ");
        }
        m_subcktMappingSummaryLabel->setText(summary);
    }

    if (m_subcktSyncLabel) {
        if (mappingOutOfSync) {
            m_subcktSyncLabel->setText("Pin names or numbers changed since the last mapping update. Re-run Smart Map or Map By Order, or edit the mapping table manually.");
            QPalette palette = m_subcktSyncLabel->palette();
            palette.setColor(QPalette::WindowText, QColor("#b45309"));
            m_subcktSyncLabel->setPalette(palette);
        } else {
            m_subcktSyncLabel->setText("Mapping is in sync with the current symbol pin names and numbers.");
            QPalette palette = m_subcktSyncLabel->palette();
            palette.setColor(QPalette::WindowText, QColor("#15803d"));
            m_subcktSyncLabel->setPalette(palette);
        }
    }
}

bool SymbolEditor::validateCurrentSymbolForSave(QStringList* errors, QStringList* warnings) const {
    if (errors) errors->clear();
    if (warnings) warnings->clear();

    const SymbolDefinition def = symbolDefinition();
    const QString modelName = def.modelName().trimmed();
    const QString modelPath = def.modelPath().trimmed();
    const QString modelSource = def.modelSource().trimmed().toLower();
    const bool hasBinding = !modelName.isEmpty() || !modelPath.isEmpty() || modelSource == "project" || modelSource == "library" || modelSource == "absolute";
    if (!hasBinding) return true;

    if (modelName.isEmpty() && errors) {
        errors->append("SPICE model binding is enabled but Model Name is empty.");
    }
    if (modelPath.isEmpty() && errors) {
        errors->append("SPICE model binding is enabled but Model File is empty.");
    }
    if (!modelPath.isEmpty()) {
        const QString resolved = resolveModelPathForEditor(modelPath, modelSource, m_projectKey);
        if (resolved.isEmpty() && warnings) {
            warnings->append(QString("Model file '%1' could not be resolved from the current source settings.").arg(modelPath));
        }
    }

    const SimSubcircuit* sub = modelName.isEmpty() ? nullptr : ModelLibraryManager::instance().findSubcircuit(modelName);
    if (!sub) return errors ? errors->isEmpty() : true;

    QStringList subcktPins;
    QSet<QString> expectedPins;
    for (const std::string& pin : sub->pinNames) {
        const QString name = QString::fromStdString(pin).trimmed();
        subcktPins << name;
        expectedPins.insert(name.toUpper());
    }

    QMap<int, QString> mapping = def.spiceNodeMapping();
    QList<const SymbolPrimitive*> pinPrimitives;
    for (const SymbolPrimitive& prim : def.primitives()) {
        if (prim.type == SymbolPrimitive::Pin) pinPrimitives.append(&prim);
    }
    std::sort(pinPrimitives.begin(), pinPrimitives.end(), [](const SymbolPrimitive* a, const SymbolPrimitive* b) {
        return a->data.value("number").toInt() < b->data.value("number").toInt();
    });

    QSet<QString> usedPins;
    for (const SymbolPrimitive* prim : pinPrimitives) {
        const int pinNumber = prim->data.value("number").toInt();
        const QString symbolPinName = prim->data.value("name").toString().trimmed();
        const QString mappedPin = mapping.value(pinNumber).trimmed();
        if (mappedPin.isEmpty()) {
            if (warnings) warnings->append(QString("Symbol pin %1 (%2) has no mapped subcircuit pin.").arg(pinNumber).arg(symbolPinName));
            continue;
        }

        const QString upper = mappedPin.toUpper();
        if (usedPins.contains(upper)) {
            if (errors) errors->append(QString("Subcircuit pin '%1' is assigned more than once.").arg(mappedPin));
        }
        usedPins.insert(upper);

        if (!expectedPins.contains(upper) && warnings) {
            warnings->append(QString("Mapped pin '%1' for symbol pin %2 is not present on subcircuit '%3'.").arg(mappedPin).arg(pinNumber).arg(modelName));
        }
    }

    if (pinPrimitives.size() != subcktPins.size() && warnings) {
        warnings->append(QString("Pin count mismatch: symbol has %1 pin(s) while subcircuit '%2' has %3 pin(s).").arg(pinPrimitives.size()).arg(modelName).arg(subcktPins.size()));
    }

    return errors ? errors->isEmpty() : true;
}

void SymbolEditor::onPinTableItemChanged(int row, int col) {
    if (!m_pinTable || row < 0 || row >= m_pinTable->rowCount() || col < 0) return;
    QTableWidgetItem* item = m_pinTable->item(row, col);
    if (!item) return;

    const int primIdx = item->data(Qt::UserRole).toInt();
    if (primIdx < 0 || primIdx >= m_symbol.primitives().size()) return;
    if (m_symbol.primitives().at(primIdx).type != SymbolPrimitive::Pin) return;

    SymbolDefinition oldDef = symbolDefinition();
    SymbolDefinition newDef = oldDef;
    SymbolPrimitive& pin = newDef.primitives()[primIdx];

    switch (col) {
    case 0: pin.data["number"] = item->text().toInt(); break;
    case 1: pin.data["name"] = item->text(); break;
    case 2: pin.data["electricalType"] = item->text().trimmed(); break;
    case 3: pin.data["orientation"] = item->text().trimmed(); break;
    case 4: pin.data["length"] = item->text().toDouble(); break;
    case 5: pin.data["visible"] = (item->checkState() == Qt::Checked); break;
    case 6: pin.data["swapGroup"] = item->text().toInt(); break;
    case 7: pin.data["jumperGroup"] = item->text().toInt(); break;
    case 8: pin.data["stackedNumbers"] = item->text(); break;
    case 9: pin.data["alternateNames"] = item->text(); break;
    default: return;
    }

    m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Edit Pin"));
}

QList<int> SymbolEditor::selectedPinRows() const {
    QList<int> rows;
    if (!m_pinTable) return rows;
    for (const QModelIndex& idx : m_pinTable->selectionModel()->selectedRows()) {
        rows.append(idx.row());
    }
    if (rows.isEmpty()) {
        for (int r = 0; r < m_pinTable->rowCount(); ++r) rows.append(r);
    }
    return rows;
}

void SymbolEditor::applyPinEditsToRows(const QList<int>& rows, const std::function<void(SymbolPrimitive&)>& edit, const QString& label) {
    if (rows.isEmpty()) return;

    SymbolDefinition oldDef = symbolDefinition();
    SymbolDefinition newDef = oldDef;
    bool changed = false;

    for (int row : rows) {
        if (row < 0 || row >= m_pinTable->rowCount()) continue;
        QTableWidgetItem* item0 = m_pinTable->item(row, 0);
        if (!item0) continue;
        int primIdx = item0->data(Qt::UserRole).toInt();
        if (primIdx < 0 || primIdx >= newDef.primitives().size()) continue;
        SymbolPrimitive& prim = newDef.primitives()[primIdx];
        if (prim.type != SymbolPrimitive::Pin) continue;
        edit(prim);
        changed = true;
    }

    if (changed) {
        m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, label));
    }
}

void SymbolEditor::onPinRenumberSequential() {
    const QList<int> rows = selectedPinRows();
    if (rows.isEmpty()) return;

    SymbolDefinition oldDef = symbolDefinition();
    SymbolDefinition newDef = oldDef;
    bool changed = false;
    int nextNum = 1;
    for (int row : rows) {
        if (row < 0 || row >= m_pinTable->rowCount()) continue;
        QTableWidgetItem* item0 = m_pinTable->item(row, 0);
        if (!item0) continue;
        int primIdx = item0->data(Qt::UserRole).toInt();
        if (primIdx < 0 || primIdx >= newDef.primitives().size()) continue;
        SymbolPrimitive& prim = newDef.primitives()[primIdx];
        if (prim.type != SymbolPrimitive::Pin) continue;
        prim.data["number"] = nextNum++;
        changed = true;
    }

    if (changed) {
        m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Renumber Pins"));
    }
}

void SymbolEditor::onPinApplyOrientation() {
    if (!m_pinBulkOrientation) return;
    const QString orientation = m_pinBulkOrientation->currentText();
    applyPinEditsToRows(selectedPinRows(), [orientation](SymbolPrimitive& prim) {
        prim.data["orientation"] = orientation;
    }, "Set Pin Orientation");
}

void SymbolEditor::onPinApplyType() {
    if (!m_pinBulkType) return;
    const QString type = m_pinBulkType->currentText();
    applyPinEditsToRows(selectedPinRows(), [type](SymbolPrimitive& prim) {
        prim.data["electricalType"] = type;
    }, "Set Pin Type");
}

void SymbolEditor::onPinDistributeSelected() {
    QList<int> rows = selectedPinRows();
    if (rows.size() < 3) return;

    struct PinRef {
        int primIdx;
        QString orientation;
        qreal x;
        qreal y;
    };

    QList<PinRef> pins;
    SymbolDefinition oldDef = symbolDefinition();
    for (int row : rows) {
        if (row < 0 || row >= m_pinTable->rowCount()) continue;
        QTableWidgetItem* item0 = m_pinTable->item(row, 0);
        if (!item0) continue;
        int primIdx = item0->data(Qt::UserRole).toInt();
        if (primIdx < 0 || primIdx >= oldDef.primitives().size()) continue;
        const SymbolPrimitive& p = oldDef.primitives().at(primIdx);
        if (p.type != SymbolPrimitive::Pin) continue;
        pins.append({primIdx,
                     p.data.value("orientation").toString("Right"),
                     p.data.value("x").toDouble(),
                     p.data.value("y").toDouble()});
    }
    if (pins.size() < 3) return;

    bool alongY = true;
    const QString ori = pins.first().orientation;
    if (ori == "Up" || ori == "Down") alongY = false;

    std::sort(pins.begin(), pins.end(), [alongY](const PinRef& a, const PinRef& b) {
        return alongY ? (a.y < b.y) : (a.x < b.x);
    });

    SymbolDefinition newDef = oldDef;
    if (alongY) {
        const qreal y0 = pins.first().y;
        const qreal y1 = pins.last().y;
        const qreal step = (y1 - y0) / qMax(1, pins.size() - 1);
        for (int i = 0; i < pins.size(); ++i) {
            newDef.primitives()[pins[i].primIdx].data["y"] = y0 + i * step;
        }
    } else {
        const qreal x0 = pins.first().x;
        const qreal x1 = pins.last().x;
        const qreal step = (x1 - x0) / qMax(1, pins.size() - 1);
        for (int i = 0; i < pins.size(); ++i) {
            newDef.primitives()[pins[i].primIdx].data["x"] = x0 + i * step;
        }
    }

    m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Distribute Pins"));
}

void SymbolEditor::onPinSortByNumber() {
    QList<int> rows = selectedPinRows();
    if (rows.size() < 2) return;

    struct PinRef {
        int primIdx;
        int number;
        QString orientation;
        qreal x;
        qreal y;
    };

    QList<PinRef> pins;
    SymbolDefinition oldDef = symbolDefinition();
    for (int row : rows) {
        if (row < 0 || row >= m_pinTable->rowCount()) continue;
        QTableWidgetItem* item0 = m_pinTable->item(row, 0);
        if (!item0) continue;
        int primIdx = item0->data(Qt::UserRole).toInt();
        if (primIdx < 0 || primIdx >= oldDef.primitives().size()) continue;
        const SymbolPrimitive& p = oldDef.primitives().at(primIdx);
        if (p.type != SymbolPrimitive::Pin) continue;
        pins.append({primIdx,
                     p.data.value("number").toInt(),
                     p.data.value("orientation").toString("Right"),
                     p.data.value("x").toDouble(),
                     p.data.value("y").toDouble()});
    }
    if (pins.size() < 2) return;

    bool alongY = true;
    const QString ori = pins.first().orientation;
    if (ori == "Up" || ori == "Down") alongY = false;

    QList<qreal> positions;
    for (const PinRef& p : pins) positions.append(alongY ? p.y : p.x);
    std::sort(positions.begin(), positions.end());

    std::sort(pins.begin(), pins.end(), [](const PinRef& a, const PinRef& b) {
        return a.number < b.number;
    });

    SymbolDefinition newDef = oldDef;
    for (int i = 0; i < pins.size(); ++i) {
        if (alongY) newDef.primitives()[pins[i].primIdx].data["y"] = positions[i];
        else newDef.primitives()[pins[i].primIdx].data["x"] = positions[i];
    }

    m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Sort Pins by Number"));
}

void SymbolEditor::onPinStackSelected() {
    QList<int> rows = selectedPinRows();
    if (rows.size() < 2) {
        QMessageBox::information(this, "Stack Pins", "Please select at least two pins to stack.");
        return;
    }

    SymbolDefinition oldDef = symbolDefinition();
    SymbolDefinition newDef = oldDef;

    // 1. Get primitive indices from rows
    QList<int> primIndices;
    for (int row : rows) {
        if (auto* item = m_pinTable->item(row, 0)) {
            primIndices.append(item->data(Qt::UserRole).toInt());
        }
    }

    if (primIndices.size() < 2) return;

    // 2. Identify Master (first one) and Slaves
    int masterIdx = primIndices.first();
    QStringList slaveNumbers;
    
    // Sort indices in descending order to avoid index shift during removal
    QList<int> sortedIndices = primIndices;
    std::sort(sortedIndices.begin(), sortedIndices.end(), std::greater<int>());
    int removedBeforeMaster = 0;

    for (int idx : sortedIndices) {
        if (idx == masterIdx) continue;
        
        const auto& slave = oldDef.primitives().at(idx);
        slaveNumbers.append(QString::number(slave.data["number"].toInt()));
        
        // Also capture any existing stacked numbers from slave
        QString existing = slave.data.value("stackedNumbers").toString();
        if (!existing.isEmpty()) slaveNumbers << existing.split(",", Qt::SkipEmptyParts);
        
        newDef.removePrimitive(idx);
        if (idx < masterIdx) {
            ++removedBeforeMaster;
        }
    }

    // 3. Update Master
    int masterNumber = oldDef.primitives().at(masterIdx).data["number"].toInt();
    const int newMasterIdx = masterIdx - removedBeforeMaster;

    if (newMasterIdx >= 0 && newMasterIdx < newDef.primitives().size() &&
        newDef.primitives()[newMasterIdx].type == SymbolPrimitive::Pin) {
        SymbolPrimitive& master = newDef.primitives()[newMasterIdx];
        QString currentStacked = master.data.value("stackedNumbers").toString();
        QStringList all = currentStacked.split(",", Qt::SkipEmptyParts);
        all << slaveNumbers;
        all.removeDuplicates();
        master.data["stackedNumbers"] = all.join(",");
    }

    m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Stack Pins"));
    statusBar()->showMessage(QString("Stacked %1 pins into Master Pin %2")
        .arg(slaveNumbers.size())
        .arg(masterNumber), 3000);
}

void SymbolEditor::onManageCustomFields() {
    QDialog dlg(this);
    dlg.setWindowTitle("Manage Custom Fields");
    dlg.resize(500, 400);
    QVBoxLayout* layout = new QVBoxLayout(&dlg);

    QTableWidget* table = new QTableWidget(0, 2);
    table->setHorizontalHeaderLabels({"Field Name", "Default Value"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setStyleSheet("QTableWidget { background-color: #1e1e1e; color: #ccc; gridline-color: #333; }");
    layout->addWidget(table);

    // Populate current fields
    auto currentFields = m_symbol.customFields();
    for (auto it = currentFields.begin(); it != currentFields.end(); ++it) {
        int r = table->rowCount();
        table->insertRow(r);
        table->setItem(r, 0, new QTableWidgetItem(it.key()));
        table->setItem(r, 1, new QTableWidgetItem(it.value()));
    }

    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* addBtn = new QPushButton("Add Field");
    QPushButton* remBtn = new QPushButton("Remove Selected");
    btnLayout->addWidget(addBtn);
    btnLayout->addWidget(remBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    connect(addBtn, &QPushButton::clicked, [&](){
        int r = table->rowCount();
        table->insertRow(r);
        table->setItem(r, 0, new QTableWidgetItem("NewField"));
        table->setItem(r, 1, new QTableWidgetItem(""));
    });

    connect(remBtn, &QPushButton::clicked, [&](){
        QSet<int> rows;
        for (auto* item : table->selectedItems()) {
            if (item) rows.insert(item->row());
        }
        QList<int> ordered = rows.values();
        std::sort(ordered.begin(), ordered.end(), std::greater<int>());
        for (int row : ordered) {
            table->removeRow(row);
        }
    });

    QDialogButtonBox* bbox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(bbox);
    connect(bbox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bbox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        SymbolDefinition oldDef = symbolDefinition();
        SymbolDefinition newDef = oldDef;
        QMap<QString, QString> newFields;
        for (int i = 0; i < table->rowCount(); ++i) {
            QTableWidgetItem* keyItem = table->item(i, 0);
            QTableWidgetItem* valItem = table->item(i, 1);
            const QString key = keyItem ? keyItem->text().trimmed() : QString();
            const QString val = valItem ? valItem->text() : QString();
            if (!key.isEmpty()) newFields[key] = val;
        }
        newDef.setCustomFields(newFields);
        m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Manage Custom Fields"));
    }
}

void SymbolEditor::onBrowseFootprint() {
    QMessageBox::information(this, "Footprint Browser", "Footprint browser is currently being refactored.");
}

// ─────────────────────────────────────────────────────────────────────────────
//  SymbolEditor – Imports
// ─────────────────────────────────────────────────────────────────────────────

void SymbolEditor::onImportKicadSymbol() {
    QString path = runThemedOpenFileDialog(this, "Import KiCad Symbol", "KiCad Symbols (*.kicad_sym)");
    if (path.isEmpty()) return;

    QStringList names = KicadSymbolImporter::getSymbolNames(path);
    if (names.isEmpty()) {
        QMessageBox::warning(this, "Import", "No symbols found in file.");
        return;
    }

    bool ok;
    QString choice = QInputDialog::getItem(this, "Import Symbol", "Select symbol:", names, 0, false, &ok);
    if (ok && !choice.isEmpty()) {
        SymbolDefinition imported = KicadSymbolImporter::importSymbol(path, choice);
        if (imported.isValid()) {
            setSymbolDefinition(imported);
        } else {
            QMessageBox::critical(this, "Error", "Failed to import selected symbol.");
        }
    }
}

void SymbolEditor::onImportLtspiceSymbol() {
    const QString path = runThemedOpenFileDialog(this, "Import LTspice Symbol", "LTspice Symbols (*.asy)");
    if (path.isEmpty()) return;
    importLtspiceSymbol(path);
}

#include <QBuffer>

void SymbolEditor::onImportImage() {
    QString path = runThemedOpenFileDialog(this, "Import Image", "Images (*.png *.jpg *.jpeg *.bmp *.svg)");
    if (path.isEmpty()) return;

    QImage img(path);
    if (img.isNull()) {
        QMessageBox::critical(this, "Error", "Failed to load image.");
        return;
    }

    QByteArray ba;
    QBuffer buffer(&ba);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG"); // Save as PNG for transparency support
    QString base64 = QString::fromLatin1(ba.toBase64());

    // Default size 100x100, centered at 0,0
    QRectF rect(-50, -50, 100, 100);
    SymbolPrimitive prim = SymbolPrimitive::createImage(base64, rect);
    prim.setUnit(m_currentUnit);
    prim.setBodyStyle(m_currentStyle);
    
    QGraphicsItem* visual = buildVisual(prim, m_symbol.primitives().size());
    if (visual) {
        m_undoStack->push(new AddPrimitiveCommand(this, prim, visual));
    }
}

void SymbolEditor::onAiSymbolGenerated(const QString& json) {
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (doc.isNull() || !doc.isArray()) {
        QMessageBox::warning(this, "AI Generation Error",
            "The AI returned an invalid format. Please try again with a clearer prompt.");
        return;
    }

    const QJsonArray primArray = doc.array();
    SymbolDefinition newDef = symbolDefinition(); // keep name/prefix
    newDef.clearPrimitives();

    for (const QJsonValue& val : primArray) {
        if (val.isObject())
            newDef.addPrimitive(SymbolPrimitive::fromJson(val.toObject()));
    }

    if (newDef.primitives().isEmpty()) {
        QMessageBox::warning(this, "AI Generation Error",
            "The AI response contained no drawable primitives.");
        return;
    }

    m_undoStack->push(new UpdateSymbolCommand(this, symbolDefinition(), newDef, "AI Generate Symbol"));
    m_view->fitInView(m_scene->itemsBoundingRect().adjusted(-20,-20,20,20),
                      Qt::KeepAspectRatio);
    
    // Automatically save AI-generated symbols
    QTimer::singleShot(500, this, &SymbolEditor::onSaveToLibrary);
}

// ─────────────────────────────────────────────────────────────────────────────
//  SymbolEditor – Pen Tool (Figma-style bezier path)
// ─────────────────────────────────────────────────────────────────────────────

void SymbolEditor::onPenPointAdded(QPointF pos) {
    if (m_currentTool != Pen) return;
    
    // Check if clicking on first point to close path
    if (m_penPoints.size() > 2) {
        if (QLineF(pos, m_penPoints.first().pos).length() < 8.0) {
            onPenPathClosed();
            return;
        }
    }
    
    PenPoint newPoint;
    newPoint.pos = pos;
    newPoint.handleIn = QPointF(0, 0);
    newPoint.handleOut = QPointF(0, 0);
    newPoint.smooth = false;
    m_penPoints.append(newPoint);
    updatePenPreview();
}

void SymbolEditor::onPenHandleDragged(QPointF handlePos) {
     if (m_currentTool != Pen || m_penPoints.isEmpty()) return;
     
     // If dragging a midpoint, split the segment at that point
     if (m_selectedPenMidpoint != -1) {
         int segIdx = m_selectedPenMidpoint;
         if (segIdx >= 0 && segIdx < m_penPoints.size()) {
             PenPoint& p1 = m_penPoints[segIdx];
             PenPoint& p2 = m_penPoints[(segIdx + 1) % m_penPoints.size()];
             
             // When dragging midpoint, we need to find which "part" of the segment is closer
             // and split there, creating a new anchor point
             QPointF midpoint = calculateBezierPoint(p1, p2, 0.5);
             qreal dragDist = QLineF(handlePos, midpoint).length();
             
             // Only perform split if we've dragged significantly (2+ pixels)
             if (dragDist > 2.0) {
                 // Insert new point at dragged position between p1 and p2
                 PenPoint newPoint;
                 newPoint.pos = handlePos;
                 newPoint.handleIn = QPointF(0, 0);
                 newPoint.handleOut = QPointF(0, 0);
                 newPoint.smooth = false;
                 newPoint.corner = true;
                 
                 // Insert after p1 (segIdx) and before p2
                 m_penPoints.insert(segIdx + 1, newPoint);
                 m_selectedPenMidpoint = -1;  // Clear midpoint selection
                 updatePenPreview();
             }
         }
     } else if (m_selectedPenPoint == -1) {
         // Original behavior: drag handle out of the last point
         PenPoint& lastPoint = m_penPoints.last();
         QPointF delta = handlePos - lastPoint.pos;
         
         // Set handleOut (going out of this point)
         lastPoint.handleOut = delta;
         
         // If smooth mode, mirror the handle
         if (lastPoint.smooth) {
             lastPoint.handleIn = -delta;
         }
     } else if (m_selectedPenPoint >= 0 && m_selectedPenPoint < m_penPoints.size()) {
         PenPoint& selectedPoint = m_penPoints[m_selectedPenPoint];
         QPointF delta = handlePos - selectedPoint.pos;
         
         if (m_selectedPenHandle == 0) {
             // Dragging handleIn
             selectedPoint.handleIn = delta;
             if (selectedPoint.smooth) {
                 selectedPoint.handleOut = -delta;
             }
         } else if (m_selectedPenHandle == 1) {
             // Dragging handleOut
             selectedPoint.handleOut = delta;
             if (selectedPoint.smooth) {
                 selectedPoint.handleIn = -delta;
             }
         } else {
             // No specific handle selected, treat as handleOut drag
             selectedPoint.handleOut = delta;
             if (selectedPoint.smooth) {
                 selectedPoint.handleIn = -delta;
             }
         }
     }
     
     updatePenPreview();
}

void SymbolEditor::onPenPointFinished() {
    if (m_currentTool != Pen) return;
    
    // If we have enough points and user presses Enter, finalize the path
    if (m_penPoints.size() >= 2) {
        // Path is ready but waiting for explicit finish (Enter key)
    }
    updatePenPreview();
}

void SymbolEditor::onPenPathClosed() {
    if (m_currentTool != Pen || m_penPoints.size() < 3) return;
    finalizePenPath();
}

void SymbolEditor::finalizePenPath() {
    if (m_penFinalizing) return;
    if (m_penPoints.size() < 2) {
        clearPenState();
        return;
    }
    m_penFinalizing = true;
    
    // Convert pen points to bezier curve primitives
    // Each segment between two points becomes a bezier curve
    
    for (int i = 0; i < m_penPoints.size(); ++i) {
        PenPoint& p1 = m_penPoints[i];
        PenPoint& p2 = m_penPoints[(i + 1) % m_penPoints.size()];
        
        // Calculate control points for bezier curve
        QPointF cp1 = p1.pos + p1.handleOut;
        QPointF cp2 = p2.pos + p2.handleIn;
        QPointF end = p2.pos;
        
        // Only create curve if there's at least one handle
        if (p1.handleOut != QPointF(0, 0) || p2.handleIn != QPointF(0, 0)) {
            SymbolPrimitive prim = SymbolPrimitive::createBezier(p1.pos, cp1, cp2, end);
            prim.setUnit(m_currentUnit);
            prim.setBodyStyle(m_currentStyle);
            QGraphicsItem* visual = buildVisual(prim, m_symbol.primitives().size());
            if (visual) {
                m_undoStack->push(new AddPrimitiveCommand(this, prim, visual));
            }
        } else {
            // Straight line if no handles
            SymbolPrimitive prim = SymbolPrimitive::createLine(p1.pos, end);
            prim.setUnit(m_currentUnit);
            prim.setBodyStyle(m_currentStyle);
            QGraphicsItem* visual = buildVisual(prim, m_symbol.primitives().size());
            if (visual) {
                m_undoStack->push(new AddPrimitiveCommand(this, prim, visual));
            }
        }
    }
    
    clearPenState();
}

 void SymbolEditor::clearPenState() {
      m_penPoints.clear();
      m_selectedPenPoint = -1;
      m_selectedPenHandle = -1;
      m_selectedPenMidpoint = -1;
      
      if (m_penPreviewItem) {
          m_scene->removeItem(m_penPreviewItem);
          delete m_penPreviewItem;
          m_penPreviewItem = nullptr;
      }
      
      for (auto* marker : m_penPointMarkers) {
          m_scene->removeItem(marker);
          delete marker;
      }
      m_penPointMarkers.clear();
      
      for (auto* line : m_penHandleLines) {
          m_scene->removeItem(line);
          delete line;
      }
      m_penHandleLines.clear();
      
      for (auto* dot : m_penHandleDots) {
          m_scene->removeItem(dot);
          delete dot;
      }
      m_penHandleDots.clear();
      
      for (auto* midDot : m_penMidpointDots) {
          m_scene->removeItem(midDot);
          delete midDot;
      }
      m_penMidpointDots.clear();
 }

void SymbolEditor::updatePenPreview() {
     if (m_currentTool != Pen) return;
     
      // Clear previous preview
      if (m_penPreviewItem) {
          m_scene->removeItem(m_penPreviewItem);
          delete m_penPreviewItem;
          m_penPreviewItem = nullptr;
      }
      for (auto* marker : m_penPointMarkers) {
          m_scene->removeItem(marker);
          delete marker;
      }
      m_penPointMarkers.clear();
      for (auto* line : m_penHandleLines) {
          m_scene->removeItem(line);
          delete line;
      }
      m_penHandleLines.clear();
      for (auto* dot : m_penHandleDots) {
          m_scene->removeItem(dot);
          delete dot;
      }
      m_penHandleDots.clear();
      for (auto* midDot : m_penMidpointDots) {
          m_scene->removeItem(midDot);
          delete midDot;
      }
      m_penMidpointDots.clear();
    
    if (m_penPoints.isEmpty()) return;
    
    // Build the path
    QPainterPath path;
    path.moveTo(m_penPoints.first().pos);
    
    for (int i = 0; i < m_penPoints.size(); ++i) {
        PenPoint& p = m_penPoints[i];
        PenPoint& next = m_penPoints[(i + 1) % m_penPoints.size()];
        
        QPointF cp1 = p.pos + p.handleOut;
        QPointF cp2 = next.pos + next.handleIn;
        
        if (p.handleOut != QPointF(0, 0) || next.handleIn != QPointF(0, 0)) {
            path.cubicTo(cp1, cp2, next.pos);
        } else {
            path.lineTo(next.pos);
        }
    }
    
    // Draw preview path
    m_penPreviewItem = m_scene->addPath(path, QPen(Qt::cyan, 1.5, Qt::DashLine), QBrush());
    m_penPreviewItem->setZValue(1000);
    
     // Draw anchor points and handles
     for (int i = 0; i < m_penPoints.size(); ++i) {
         PenPoint& p = m_penPoints[i];
         
         // Anchor point marker - highlight if selected
         QColor anchorColor;
         if (i == m_selectedPenPoint) {
             anchorColor = QColor(66, 165, 245);  // Blue for selected
         } else if (i == 0) {
             anchorColor = Qt::green;  // Green for start point
         } else {
             anchorColor = Qt::yellow;  // Yellow for others
         }
         
         // Add indicator for smooth vs corner mode
         int size = (i == m_selectedPenPoint) ? 10 : 8;
         int offset = size / 2;
         auto* marker = m_scene->addEllipse(p.pos.x() - offset, p.pos.y() - offset, size, size, 
                                           QPen(anchorColor, 1.5), QBrush(anchorColor));
         marker->setZValue(1001);
         m_penPointMarkers.append(marker);
         
         // Handle lines and dots
         if (p.handleIn != QPointF(0, 0)) {
             QPointF handlePos = p.pos + p.handleIn;
             
             // Color based on selection: blue if selected, orange otherwise
             QColor handleColor = (i == m_selectedPenPoint && m_selectedPenHandle == 0) 
                                 ? QColor(255, 152, 0)  // Orange for selected handleIn
                                 : QColor(156, 39, 176);  // Purple for inactive
             
             auto* handleLine = m_scene->addLine(QLineF(p.pos, handlePos), QPen(handleColor, 1.5));
             handleLine->setZValue(1001);
             m_penHandleLines.append(handleLine);
             
             auto* handleDot = m_scene->addEllipse(handlePos.x() - 3, handlePos.y() - 3, 6, 6,
                                                   QPen(handleColor, 1.5), QBrush(handleColor));
             handleDot->setZValue(1001);
             m_penHandleDots.append(handleDot);
         }
         
         if (p.handleOut != QPointF(0, 0)) {
             QPointF handlePos = p.pos + p.handleOut;
             
             // Color based on selection: blue if selected, orange otherwise
             QColor handleColor = (i == m_selectedPenPoint && m_selectedPenHandle == 1) 
                                 ? QColor(255, 152, 0)  // Orange for selected handleOut
                                 : QColor(156, 39, 176);  // Purple for inactive
             
             auto* handleLine = m_scene->addLine(QLineF(p.pos, handlePos), QPen(handleColor, 1.5));
             handleLine->setZValue(1001);
             m_penHandleLines.append(handleLine);
             
              auto* handleDot = m_scene->addEllipse(handlePos.x() - 3, handlePos.y() - 3, 6, 6,
                                                    QPen(handleColor, 1.5), QBrush(handleColor));
              handleDot->setZValue(1001);
              m_penHandleDots.append(handleDot);
           }
       }
       
       // Draw midpoint dots on each segment (for Figma-style edge editing)
       for (int i = 0; i < m_penPoints.size(); ++i) {
           PenPoint& p1 = m_penPoints[i];
           PenPoint& p2 = m_penPoints[(i + 1) % m_penPoints.size()];
           
           // Calculate midpoint of segment
           QPointF midpoint = calculateBezierPoint(p1, p2, 0.5);
           
           // Color: cyan for regular, bright cyan if selected
           QColor midpointColor = (i == m_selectedPenMidpoint) 
                                 ? QColor(0, 255, 255)    // Bright cyan for selected
                                 : QColor(0, 200, 200);   // Darker cyan for inactive
           
           auto* midpointDot = m_scene->addEllipse(midpoint.x() - 4, midpoint.y() - 4, 8, 8,
                                                   QPen(midpointColor, 1.5), QBrush(midpointColor));
           midpointDot->setZValue(1000);  // Slightly below handles
           m_penMidpointDots.append(midpointDot);
       }
   }
   
   // Calculate point on cubic bezier curve at parameter t [0,1]
   // B(t) = (1-t)³P₀ + 3(1-t)²tP₁ + 3(1-t)t²P₂ + t³P₃
   QPointF SymbolEditor::calculateBezierPoint(const PenPoint& p1, const PenPoint& p2, qreal t) const {
      if (t < 0.0) t = 0.0;
      if (t > 1.0) t = 1.0;
      
      qreal mt = 1.0 - t;
      qreal mt2 = mt * mt;
      qreal mt3 = mt2 * mt;
      qreal t2 = t * t;
      qreal t3 = t2 * t;
      
      QPointF p0 = p1.pos;
      QPointF p3 = p2.pos;
      QPointF cp1 = p1.pos + p1.handleOut;
      QPointF cp2 = p2.pos + p2.handleIn;
      
      return mt3 * p0 + 3 * mt2 * t * cp1 + 3 * mt * t2 * cp2 + t3 * p3;
  }

void SymbolEditor::onPenClicked(QPointF pos, int pointIndex, int handleIndex) {
     if (m_currentTool != Pen) return;
     
     // Check distance to existing points/handles to enable selection & editing
     const qreal HIT_RADIUS = 10.0;
     int closestPoint = -1;
     int closestHandle = -1;
     int closestMidpoint = -1;  // Segment index for midpoint
     qreal closestDist = HIT_RADIUS + 1;
     
     // Check anchor points
     for (int i = 0; i < m_penPoints.size(); ++i) {
         qreal distToPos = QLineF(pos, m_penPoints[i].pos).length();
         if (distToPos < closestDist) {
             closestDist = distToPos;
             closestPoint = i;
             closestHandle = -1;
             closestMidpoint = -1;
         }
         
         // Check handleIn
         if (m_penPoints[i].handleIn != QPointF(0, 0)) {
             QPointF handlePos = m_penPoints[i].pos + m_penPoints[i].handleIn;
             qreal dist = QLineF(pos, handlePos).length();
             if (dist < closestDist) {
                 closestDist = dist;
                 closestPoint = i;
                 closestHandle = 0;  // handleIn
                 closestMidpoint = -1;
             }
         }
         
         // Check handleOut
         if (m_penPoints[i].handleOut != QPointF(0, 0)) {
             QPointF handlePos = m_penPoints[i].pos + m_penPoints[i].handleOut;
             qreal dist = QLineF(pos, handlePos).length();
             if (dist < closestDist) {
                 closestDist = dist;
                 closestPoint = i;
                 closestHandle = 1;  // handleOut
                 closestMidpoint = -1;
             }
         }
     }
     
     // Check midpoints on segments (only if already have at least 2 points)
     if (m_penPoints.size() >= 2) {
         for (int i = 0; i < m_penPoints.size(); ++i) {
             PenPoint& p1 = m_penPoints[i];
             PenPoint& p2 = m_penPoints[(i + 1) % m_penPoints.size()];
             
             // Calculate midpoint of segment at t=0.5
             QPointF midpoint = calculateBezierPoint(p1, p2, 0.5);
             qreal dist = QLineF(pos, midpoint).length();
             
             if (dist < closestDist) {
                 closestDist = dist;
                 closestPoint = -1;
                 closestHandle = -1;
                 closestMidpoint = i;  // Segment index
             }
         }
     }
     
     if (closestMidpoint != -1) {
         // Midpoint hit - split the segment by dragging
         m_selectedPenMidpoint = closestMidpoint;
         updatePenPreview();
     } else if (closestPoint == -1) {
         // No point/handle/midpoint hit, add new point
         if (m_penPoints.size() > 2) {
             if (QLineF(pos, m_penPoints.first().pos).length() < 8.0) {
                 onPenPathClosed();
                 return;
             }
         }
         
         PenPoint newPoint;
         newPoint.pos = pos;
         newPoint.handleIn = QPointF(0, 0);
         newPoint.handleOut = QPointF(0, 0);
         newPoint.smooth = false;
         newPoint.corner = true;  // New points are corners by default
         m_penPoints.append(newPoint);
         updatePenPreview();
     } else {
         // Point or handle hit
         bool isAltPressed = (handleIndex == 1);  // handleIndex=1 indicates Alt was pressed
         
         if (closestHandle == -1) {
             // Anchor point hit
             if (isAltPressed) {
                 // Alt+click toggles smooth/corner mode
                 m_penPoints[closestPoint].smooth = !m_penPoints[closestPoint].smooth;
                 m_penPoints[closestPoint].corner = !m_penPoints[closestPoint].corner;
                 
                 // If toggling to smooth, and no handles exist yet, mirror them
                 if (m_penPoints[closestPoint].smooth && 
                     m_penPoints[closestPoint].handleOut != QPointF(0, 0)) {
                     m_penPoints[closestPoint].handleIn = -m_penPoints[closestPoint].handleOut;
                 }
             } else {
                 // Regular click toggles selection
                 if (m_selectedPenPoint == closestPoint) {
                     m_selectedPenPoint = -1;  // Deselect
                 } else {
                     m_selectedPenPoint = closestPoint;  // Select this point
                     m_selectedPenHandle = -1;  // Clear handle selection
                 }
             }
         } else {
             // Handle hit - mark for dragging
             m_selectedPenPoint = closestPoint;
             m_selectedPenHandle = closestHandle;
         }
         updatePenPreview();
     }
}

void SymbolEditor::onPenDoubleClicked(QPointF pos, int pointIndex) {
    if (m_currentTool != Pen) return;
    
    // Find closest point for deletion
    const qreal HIT_RADIUS = 10.0;
    int closestPoint = -1;
    qreal closestDist = HIT_RADIUS + 1;
    
    for (int i = 0; i < m_penPoints.size(); ++i) {
        qreal dist = QLineF(pos, m_penPoints[i].pos).length();
        if (dist < closestDist) {
            closestDist = dist;
            closestPoint = i;
        }
    }
    
    // Double-click removes point (but keep at least 2 points for a path)
    if (closestPoint != -1 && m_penPoints.size() > 2) {
        m_penPoints.removeAt(closestPoint);
        if (m_selectedPenPoint == closestPoint) {
            m_selectedPenPoint = -1;
        }
        updatePenPreview();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  SymbolEditor – Zoom
// ─────────────────────────────────────────────────────────────────────────────

void SymbolEditor::onZoomIn()  { if (m_view) m_view->zoomByFactor(1.25); }
void SymbolEditor::onZoomOut() { if (m_view) m_view->zoomByFactor(1.0 / 1.25); }
void SymbolEditor::onZoomFit() {
    if (!m_view || !m_scene) return;
    QRectF bounds = m_scene->itemsBoundingRect();
    if (bounds.isNull()) return;
    bounds.adjust(-20, -20, 20, 20);
    m_view->fitInView(bounds, Qt::KeepAspectRatio);
}
void SymbolEditor::onZoomSelection() {
    if (!m_view || !m_scene) return;
    QRectF bounds;
    for (auto* item : m_scene->selectedItems()) bounds = bounds.united(item->sceneBoundingRect());
    if (!bounds.isNull()) m_view->fitInView(bounds.adjusted(-10,-10,10,10), Qt::KeepAspectRatio);
}

// ─────────────────────────────────────────────────────────────────────────────
//  SymbolEditor – Clipboard
// ─────────────────────────────────────────────────────────────────────────────

void SymbolEditor::onCopy() {
    m_copyBuffer.clear();
    for (auto* item : m_scene->selectedItems()) {
        int idx = primitiveIndex(item);
        if (idx != -1) m_copyBuffer.append(m_symbol.primitives()[idx]);
    }
}

void SymbolEditor::onPaste() {
    if (m_copyBuffer.isEmpty()) return;
    SymbolDefinition oldDef = symbolDefinition();
    SymbolDefinition newDef = oldDef;
    for (const auto& prim : m_copyBuffer) {
        SymbolPrimitive p = prim;
        // Shift pasted items slightly
        translatePrimitive(p, 10.0, 10.0);
        newDef.addPrimitive(p);
    }
    m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Paste Items"));
}

void SymbolEditor::onDuplicate() {
    onCopy();
    onPaste();
}

void SymbolEditor::onRectResizeStarted(const QString& corner, QPointF scenePos) {
    if (!m_scene || m_currentTool != Select) return;
    const QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() != 1) return;

    const int idx = primitiveIndex(selected.first());
    if (idx < 0 || idx >= m_symbol.primitives().size()) return;
    const SymbolPrimitive& prim = m_symbol.primitives().at(idx);
    m_rectResizeSessionActive = true;
    m_rectResizePrimIdx = idx;
    m_rectResizeCorner = corner;
    m_rectResizeOldDef = symbolDefinition();
    m_rectResizeAnchor = QPointF();
    m_resizeLineOtherEnd = QPointF();
    m_resizeCircleCenter = QPointF();

    if (prim.type == SymbolPrimitive::Rect || prim.type == SymbolPrimitive::Arc) {
        const qreal x = prim.data.value("x").toDouble();
        const qreal y = prim.data.value("y").toDouble();
        const qreal w = prim.data.contains("width") ? prim.data.value("width").toDouble() : prim.data.value("w").toDouble();
        const qreal h = prim.data.contains("height") ? prim.data.value("height").toDouble() : prim.data.value("h").toDouble();
        QRectF r(x, y, w, h);
        r = r.normalized();
        if (r.isNull()) {
            m_rectResizeSessionActive = false;
            return;
        }
        if (corner == "tl") m_rectResizeAnchor = r.bottomRight();
        else if (corner == "tr") m_rectResizeAnchor = r.bottomLeft();
        else if (corner == "bl") m_rectResizeAnchor = r.topRight();
        else m_rectResizeAnchor = r.topLeft(); // "br"
    } else if (prim.type == SymbolPrimitive::Line) {
        const QPointF p1(prim.data.value("x1").toDouble(), prim.data.value("y1").toDouble());
        const QPointF p2(prim.data.value("x2").toDouble(), prim.data.value("y2").toDouble());
        m_resizeLineOtherEnd = (corner == "p1") ? p2 : p1;
    } else if (prim.type == SymbolPrimitive::Circle) {
        const qreal cx = prim.data.contains("centerX") ? prim.data.value("centerX").toDouble() : prim.data.value("cx").toDouble();
        const qreal cy = prim.data.contains("centerY") ? prim.data.value("centerY").toDouble() : prim.data.value("cy").toDouble();
        m_resizeCircleCenter = QPointF(cx, cy);
    } else {
        m_rectResizeSessionActive = false;
        return;
    }

    onRectResizeUpdated(scenePos);
}

void SymbolEditor::onRectResizeUpdated(QPointF scenePos) {
    if (!m_rectResizeSessionActive) return;
    if (m_rectResizePrimIdx < 0 || m_rectResizePrimIdx >= m_symbol.primitives().size()) return;

    SymbolPrimitive& prim = m_symbol.primitives()[m_rectResizePrimIdx];
    if (prim.type == SymbolPrimitive::Rect || prim.type == SymbolPrimitive::Arc) {
        QPointF p = scenePos;
        const qreal minSize = qMax<qreal>(1.0, m_view ? m_view->gridSize() * 0.5 : 1.0);
        QRectF r(m_rectResizeAnchor, p);
        r = r.normalized();
        if (r.width() < minSize) r.setWidth(minSize);
        if (r.height() < minSize) r.setHeight(minSize);

        prim.data["x"] = r.left();
        prim.data["y"] = r.top();
        prim.data["width"] = r.width();
        prim.data["height"] = r.height();
        prim.data["w"] = r.width();
        prim.data["h"] = r.height();
    } else if (prim.type == SymbolPrimitive::Line) {
        if (m_rectResizeCorner == "p1") {
            prim.data["x1"] = scenePos.x();
            prim.data["y1"] = scenePos.y();
            prim.data["x2"] = m_resizeLineOtherEnd.x();
            prim.data["y2"] = m_resizeLineOtherEnd.y();
        } else {
            prim.data["x2"] = scenePos.x();
            prim.data["y2"] = scenePos.y();
            prim.data["x1"] = m_resizeLineOtherEnd.x();
            prim.data["y1"] = m_resizeLineOtherEnd.y();
        }
    } else if (prim.type == SymbolPrimitive::Circle) {
        qreal radius = 1.0;
        if (m_rectResizeCorner == "east" || m_rectResizeCorner == "west") {
            radius = qAbs(scenePos.x() - m_resizeCircleCenter.x());
        } else if (m_rectResizeCorner == "north" || m_rectResizeCorner == "south") {
            radius = qAbs(scenePos.y() - m_resizeCircleCenter.y());
        } else {
            radius = QLineF(scenePos, m_resizeCircleCenter).length();
        }
        const qreal minR = qMax<qreal>(0.5, m_view ? m_view->gridSize() * 0.25 : 0.5);
        if (radius < minR) radius = minR;
        prim.data["centerX"] = m_resizeCircleCenter.x();
        prim.data["centerY"] = m_resizeCircleCenter.y();
        prim.data["cx"] = m_resizeCircleCenter.x();
        prim.data["cy"] = m_resizeCircleCenter.y();
        prim.data["radius"] = radius;
        prim.data["r"] = radius;
    } else {
        return;
    }

    updateVisualForPrimitive(m_rectResizePrimIdx, prim);
    updateResizeHandles();
}

void SymbolEditor::onRectResizeFinished(QPointF scenePos) {
    if (!m_rectResizeSessionActive) return;
    onRectResizeUpdated(scenePos);

    SymbolDefinition newDef = symbolDefinition();
    const bool changed = (QJsonDocument(newDef.toJson()).toJson(QJsonDocument::Compact) !=
                          QJsonDocument(m_rectResizeOldDef.toJson()).toJson(QJsonDocument::Compact));
    if (changed) {
        m_undoStack->push(new UpdateSymbolCommand(this, m_rectResizeOldDef, newDef, "Resize Rectangle"));
    } else {
        applySymbolDefinition(m_rectResizeOldDef);
    }

    m_rectResizeSessionActive = false;
    m_rectResizePrimIdx = -1;
    m_rectResizeCorner.clear();
}

void SymbolEditor::onSelectionChanged() {
    // Focus the Selection tab when items are selected
    if (m_propsTabWidget && !m_scene->selectedItems().isEmpty()) {
        if (m_propsTabWidget->currentIndex() != 0) {
            m_propsTabWidget->setCurrentIndex(0);
        }
        if (m_propsDock) {
            m_propsDock->raise();
            m_propsDock->activateWindow();
        }
    }
    
    updatePropertiesPanel();
    updateResizeHandles();
    
    // Check if a single bezier primitive is selected for editing in Select mode
    if (m_currentTool == Select) {
        m_editingBezierIndex = -1;
        m_selectedBezierPoint = -1;
        
        // Look for a single selected bezier primitive
        int selectedBezierIndex = -1;
        int selectedCount = 0;
        
        for (QGraphicsItem* item : m_scene->selectedItems()) {
            bool isPrimitive = item->data(1).isValid();
            if (isPrimitive) {
                selectedCount++;
                int primIndex = primitiveIndex(item);
                if (primIndex >= 0 && primIndex < m_symbol.primitives().size()) {
                    if (m_symbol.primitives()[primIndex].type == SymbolPrimitive::Bezier) {
                        selectedBezierIndex = primIndex;
                    }
                }
            }
        }
        
        // Only enable bezier editing if exactly one item is selected and it's a bezier
        if (selectedCount == 1 && selectedBezierIndex >= 0) {
            m_editingBezierIndex = selectedBezierIndex;
            updateBezierEditPreview();
        }
    }

    // Visual selection highlighting (Glow effect)
    for (QGraphicsItem* item : m_scene->items()) {
        // Only apply to our real items (primitives and labels)
        bool isPrimitive = item->data(1).isValid();
        bool isLabel = (item->data(0).toString() == "label");
        
        if (isPrimitive || isLabel) {
            if (item->isSelected()) {
                if (!item->graphicsEffect()) {
                    auto* glow = new QGraphicsDropShadowEffect();
                    glow->setBlurRadius(15);
                    glow->setOffset(0);
                    glow->setColor(QColor("#3b82f6")); // Professional Tech Blue
                    item->setGraphicsEffect(glow);
                }
            } else {
                if (item->graphicsEffect()) {
                    item->setGraphicsEffect(nullptr);
                }
            }
        }
    }
}

void SymbolEditor::populatePropertiesFor(int index) {
    if (!m_propertyEditor) return;
    m_propertyEditor->clear();
    if (index < 0 || index >= m_symbol.primitives().size()) return;

    const SymbolPrimitive& prim = m_symbol.primitives().at(index);

    auto dbl = [&](const char* a, const char* b = nullptr) -> double {
        if (prim.data.contains(a)) return prim.data[a].toDouble();
        return b ? prim.data.value(b).toDouble() : 0.0;
    };
    auto str = [&](const char* k, const char* def = "") -> QString {
        const QString s = prim.data.value(k).toString();
        return s.isEmpty() ? QString(def) : s;
    };

    m_propertyEditor->addProperty("Unit", prim.unit());
    m_propertyEditor->addProperty("Body Style", prim.bodyStyle(), "enum|Shared,Standard,De Morgan");

    switch (prim.type) {
    case SymbolPrimitive::Pin: {
        m_propertyEditor->addProperty("Number",      prim.data.value("number").toInt());
        m_propertyEditor->addProperty("Name",        str("name"));
        m_propertyEditor->addProperty("X",           dbl("x"));
        m_propertyEditor->addProperty("Y",           dbl("y"));
        m_propertyEditor->addProperty("Orientation", str("orientation","Right"),
                                      "enum|Right,Left,Up,Down");
        m_propertyEditor->addProperty("Type",        str("electricalType","Passive"),
                                      "enum|Input,Output,Bidirectional,Tri-state,Passive,Free,Unspecified,Power Input,Power Output,Open Collector,Open Emitter");
        m_propertyEditor->addProperty("Shape",       str("pinShape","Line"),
                                      "enum|Line,Inverted,Clock,Inverted Clock,Falling Edge Clock");
        m_propertyEditor->addProperty("Stacked Pins", str("stackedNumbers"));
        int modeCount = prim.data.value("pinModes").toArray().size();
        m_propertyEditor->addProperty("Pin Modes", QString("%1 Defined").arg(modeCount), "button");
        m_propertyEditor->addProperty("Name Size",   prim.data.value("nameSize").toDouble(7.0));
        m_propertyEditor->addProperty("Number Size", prim.data.value("numSize").toDouble(7.0));
        m_propertyEditor->addProperty("Visible",     prim.data.value("visible").toBool(true));
        m_propertyEditor->addProperty("Length",      dbl("length") > 0 ? dbl("length") : 15.0);
        m_propertyEditor->addProperty("Swap Group",  prim.data.value("swapGroup").toInt(0));
        m_propertyEditor->addProperty("Jumper Group", prim.data.value("jumperGroup").toInt(0));
        m_propertyEditor->addProperty("Alternate Names", str("alternateNames"));
        m_propertyEditor->addProperty("Hide Name",   prim.data.value("hideName").toBool());
        m_propertyEditor->addProperty("Hide Number", prim.data.value("hideNum").toBool());
        break;
    }
    case SymbolPrimitive::Text:
        m_propertyEditor->addProperty("Text",      str("text"));
        m_propertyEditor->addProperty("X",         dbl("x"));
        m_propertyEditor->addProperty("Y",         dbl("y"));
        m_propertyEditor->addProperty("Font Size", prim.data.value("fontSize").toInt(10));
        break;
    case SymbolPrimitive::Line:
        m_propertyEditor->addProperty("X1",         dbl("x1"));
        m_propertyEditor->addProperty("Y1",         dbl("y1"));
        m_propertyEditor->addProperty("X2",         dbl("x2"));
        m_propertyEditor->addProperty("Y2",         dbl("y2"));
        m_propertyEditor->addProperty("Line Width",  dbl("lineWidth") > 0 ? dbl("lineWidth") : 1.5);
        m_propertyEditor->addProperty("Line Style",  str("lineStyle","Solid"),
                                      "enum|Solid,Dash,Dot,DashDot");
        break;
    case SymbolPrimitive::Rect:
        m_propertyEditor->addProperty("X",          dbl("x"));
        m_propertyEditor->addProperty("Y",          dbl("y"));
        m_propertyEditor->addProperty("Width",      dbl("width","w"));
        m_propertyEditor->addProperty("Height",     dbl("height","h"));
        m_propertyEditor->addProperty("Filled",     prim.data.value("filled").toBool());
        m_propertyEditor->addProperty("Fill Color", str("fillColor","#007acc33"));
        m_propertyEditor->addProperty("Line Width",  dbl("lineWidth") > 0 ? dbl("lineWidth") : 1.5);
        m_propertyEditor->addProperty("Line Style",  str("lineStyle","Solid"),
                                      "enum|Solid,Dash,Dot,DashDot");
        break;
    case SymbolPrimitive::Circle:
        m_propertyEditor->addProperty("Center X",   dbl("centerX","cx"));
        m_propertyEditor->addProperty("Center Y",   dbl("centerY","cy"));
        m_propertyEditor->addProperty("Radius",     dbl("radius","r"));
        m_propertyEditor->addProperty("Filled",     prim.data.value("filled").toBool());
        m_propertyEditor->addProperty("Fill Color", str("fillColor","#007acc33"));
        m_propertyEditor->addProperty("Line Width",  dbl("lineWidth") > 0 ? dbl("lineWidth") : 1.5);
        m_propertyEditor->addProperty("Line Style",  str("lineStyle","Solid"),
                                      "enum|Solid,Dash,Dot,DashDot");
        break;
    case SymbolPrimitive::Arc:
        m_propertyEditor->addProperty("X",          dbl("x"));
        m_propertyEditor->addProperty("Y",          dbl("y"));
        m_propertyEditor->addProperty("Width",      dbl("width","w"));
        m_propertyEditor->addProperty("Height",     dbl("height","h"));
        m_propertyEditor->addProperty("Start Angle (°×16)", prim.data.value("startAngle").toInt(0));
        m_propertyEditor->addProperty("Span Angle (°×16)",  prim.data.value("spanAngle").toInt(180 * 16));
        m_propertyEditor->addProperty("Line Width",  dbl("lineWidth") > 0 ? dbl("lineWidth") : 1.5);
        break;
    case SymbolPrimitive::Polygon:
        m_propertyEditor->addProperty("Points",     prim.data.value("points").toArray().size());
        m_propertyEditor->addProperty("Filled",     prim.data.value("filled").toBool());
        m_propertyEditor->addProperty("Fill Color", str("fillColor","#007acc33"));
        break;
    case SymbolPrimitive::Bezier:
        m_propertyEditor->addProperty("X1", dbl("x1"));
        m_propertyEditor->addProperty("Y1", dbl("y1"));
        m_propertyEditor->addProperty("X2", dbl("x2"));
        m_propertyEditor->addProperty("Y2", dbl("y2"));
        m_propertyEditor->addProperty("X3", dbl("x3"));
        m_propertyEditor->addProperty("Y3", dbl("y3"));
        m_propertyEditor->addProperty("X4", dbl("x4"));
        m_propertyEditor->addProperty("Y4", dbl("y4"));
        m_propertyEditor->addProperty("Line Width", dbl("lineWidth") > 0 ? dbl("lineWidth") : 1.5);
        break;
    case SymbolPrimitive::Image:
        m_propertyEditor->addProperty("X",      dbl("x"));
        m_propertyEditor->addProperty("Y",      dbl("y"));
        m_propertyEditor->addProperty("Width",  dbl("width", "w"));
        m_propertyEditor->addProperty("Height", dbl("height", "h"));
        break;
    default:
        break;
    }
}

void SymbolEditor::updatePropertiesPanel() {
    if (!m_propertyEditor) return;
    m_propertyEditor->beginUpdate();
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() == 1) {
        QGraphicsItem* item = selected.first();
        if (item->data(0).toString() == "label") {
            m_propertyEditor->clear();
            QString type = item->data(1).toString();
            m_propertyEditor->addSectionHeader(type == "reference" ? "Reference Label" : "Symbol Name Label");
            m_propertyEditor->addProperty("Label X", item->pos().x());
            m_propertyEditor->addProperty("Label Y", item->pos().y());
            m_propertyEditor->endUpdate();
            return;
        }

        int idx = primitiveIndex(item);
        if (idx != -1) {
            populatePropertiesFor(idx);
            m_propertyEditor->endUpdate();
            return;
        }
    }

    m_propertyEditor->clear();
    if (m_symbol.isDerived()) {
        m_propertyEditor->addSectionHeader("Inheritance");
        m_propertyEditor->addProperty("Parent Symbol",  m_symbol.parentName());
        m_propertyEditor->addProperty("Parent Library", m_symbol.parentLibrary());
    }
    m_propertyEditor->addSectionHeader("Symbol Settings");
    m_propertyEditor->addProperty("Symbol Name", m_nameEdit->text());
    m_propertyEditor->addProperty("Ref Prefix",   m_prefixEdit->text());
    m_propertyEditor->addProperty("Default Value", m_symbol.defaultValue());
    m_propertyEditor->addProperty("Default Footprint", m_symbol.defaultFootprint());
    m_propertyEditor->addProperty("Category",     m_categoryCombo->currentText(),
                                  "enum|Passives,Semiconductors,Integrated Circuits,Connectors,Power,Other");
    m_propertyEditor->endUpdate();
}

void SymbolEditor::onPropertyChanged(const QString& name, const QVariant& value) {
    if (m_livePreview) m_livePreview->setSymbol(m_symbol);
    QList<QGraphicsItem*> selected = m_scene->selectedItems();

    if (selected.size() == 1) {
        QGraphicsItem* item = selected.first();

        if (item->data(0).toString() == "label") {
            QString type = item->data(1).toString();
            SymbolDefinition oldDef = symbolDefinition();
            SymbolDefinition newDef = oldDef;

            if (type == "reference") {
                QPointF p = oldDef.referencePos();
                if (name == "Label X") p.setX(value.toDouble());
                else if (name == "Label Y") p.setY(value.toDouble());
                newDef.setReferencePos(p);
            } else {
                QPointF p = oldDef.namePos();
                if (name == "Label X") p.setX(value.toDouble());
                else if (name == "Label Y") p.setY(value.toDouble());
                newDef.setNamePos(p);
            }

            m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Move Label"));
            return;
        }

        int index = primitiveIndex(item);
        if (index >= 0 && index < m_symbol.primitives().size()) {
            SymbolDefinition oldDef = symbolDefinition();
            SymbolDefinition newDef = oldDef;

            SymbolPrimitive& prim = newDef.primitives()[index];
            const QString key = name.toLower().replace(' ', '_');

            static const QHash<QString, QString> keyMap {
                {"x",             "x"},    {"y",             "y"},
                {"x1",            "x1"},   {"y1",            "y1"},
                {"x2",            "x2"},   {"y2",            "y2"},
                {"x3",            "x3"},   {"y3",            "y3"},
                {"x4",            "x4"},   {"y4",            "y4"},
                {"center_x",      "centerX"}, {"center_y",   "centerY"},
                {"radius",        "radius"},  {"width",       "width"},
                {"height",        "height"},  {"filled",      "filled"},
                {"fill_color",    "fillColor"},{"line_width",  "lineWidth"},
                {"line_style",    "lineStyle"},{"text",        "text"},
                {"font_size",     "fontSize"}, {"name",        "name"},
                {"unit",          "unit"},     {"body_style",  "bodyStyle"},
                {"number",        "number"},   {"orientation", "orientation"},
                {"type",          "electricalType"}, {"length", "length"},
                {"shape",         "pinShape"},
                {"pin_modes",     "pinModes"},
                {"name_size",     "nameSize"},
                {"number_size",   "numSize"},
                {"stacked_pins",  "stackedNumbers"},
                {"swap_group",    "swapGroup"},
                {"jumper_group",  "jumperGroup"},
                {"alternate_names", "alternateNames"},
                {"hide_name",     "hideName"}, {"hide_number", "hideNum"},
                {"start_angle_(°×16)", "startAngle"},
                {"span_angle_(°×16)",  "spanAngle"},
            };

            const QString dataKey = keyMap.value(key, key);

            if (dataKey == "unit") {
                prim.setUnit(value.toInt());
            } else if (dataKey == "bodyStyle") {
                if (value.typeId() == QMetaType::QString) {
                    QString s = value.toString();
                    if (s == "Standard") prim.setBodyStyle(1);
                    else if (s == "De Morgan") prim.setBodyStyle(2);
                    else prim.setBodyStyle(0);
                } else {
                    prim.setBodyStyle(value.toInt());
                }
            } else {
                static const QSet<QString> boolKeys = {
                    "filled", "visible", "hideName", "hideNum"
                };
                static const QSet<QString> intKeys = {
                    "number", "swapGroup", "jumperGroup", "fontSize", "startAngle", "spanAngle"
                };
                static const QSet<QString> doubleKeys = {
                    "x","y","x1","y1","x2","y2","x3","y3","x4","y4",
                    "centerX","centerY","radius","width","height","lineWidth",
                    "length","nameSize","numSize"
                };

                if (boolKeys.contains(dataKey)) {
                    prim.data[dataKey] = QJsonValue(value.toBool());
                } else if (intKeys.contains(dataKey)) {
                    prim.data[dataKey] = QJsonValue(value.toInt());
                } else if (doubleKeys.contains(dataKey)) {
                    prim.data[dataKey] = QJsonValue(value.toDouble());
                } else {
                    prim.data[dataKey] = QJsonValue::fromVariant(value);
                }
            }

            m_undoStack->push(new UpdateSymbolCommand(this, oldDef, newDef, "Edit Properties"));
            return;
        }
    }
}

void SymbolEditor::onRunSRC() {
    if (!m_srcList) return;
    m_srcList->clear();
    
    struct SRCIssue { QString msg; bool isError; int primitiveIndex; };
    QList<SRCIssue> issues;

    const auto& prims = m_symbol.primitives();

    // 1. Symbol-level metadata checks
    if (m_symbol.referencePrefix().isEmpty()) {
        issues << SRCIssue{"WARNING: Reference prefix is empty (e.g. 'U', 'R').", false, -1};
    }
    if (m_symbol.name().isEmpty()) {
        issues << SRCIssue{"ERROR: Symbol name is empty.", true, -1};
    }

    // 2. Check if empty
    if (prims.isEmpty() && !m_symbol.isDerived()) {
        issues << SRCIssue{"CRITICAL: Symbol contains no primitives.", true, -1};
    }

    // 3. Scan Pins & Units
    QMap<int, int> pinNumbers; // Number -> Count
    QList<QPair<QPointF, int>> pinPositions; // pos, index
    QMap<int, QString> unitPinTypes; // pinNumber -> electricalType (for multi-unit consistency)
    
    for (int i = 0; i < prims.size(); ++i) {
        const auto& prim = prims[i];
        if (prim.type == SymbolPrimitive::Pin) {
            int num = prim.data.value("number").toInt();
            QString name = prim.data.value("name").toString();
            QString type = prim.data.value("electricalType").toString("Passive");
            QPointF pos(prim.data.value("x").toDouble(), prim.data.value("y").toDouble());

            // Duplicate Pin Number (within same unit or shared)
            QString pinKey = QString::number(num) + "_" + QString::number(prim.unit());
            // Actually, usually pin numbers must be unique across the WHOLE symbol unless they are power
            if (pinNumbers.contains(num) && type != "Power") {
                issues << SRCIssue{QString("ERROR: Duplicate Pin Number '%1' detected.").arg(num), true, i};
            }
            pinNumbers[num]++;

            // Missing Name
            if (name.trimmed().isEmpty()) {
                issues << SRCIssue{QString("WARNING: Pin %1 has no name.").arg(num), false, i};
            }

            // Overlapping Pins
            for (const auto& pair : pinPositions) {
                if (QLineF(pos, pair.first).length() < 0.1) {
                    issues << SRCIssue{QString("ERROR: Pin %1 overlaps another pin at (%2, %3)").arg(QString::number(num), QString::number(pos.x()), QString::number(pos.y())), true, i};
                }
            }
            pinPositions.append({pos, i});

            // Multi-unit consistency: Same pin number across units should have same type
            if (unitPinTypes.contains(num) && unitPinTypes[num] != type) {
                issues << SRCIssue{QString("ERROR: Pin %1 has inconsistent electrical types across units (%2 vs %3)").arg(QString::number(num), type, unitPinTypes[num]), true, i};
            }
            unitPinTypes[num] = type;
        }
    }

    // Add to list
    for (const auto& issue : issues) {
        auto* item = new QListWidgetItem((issue.isError ? "❌ " : "⚠️ ") + issue.msg);
        item->setForeground(QBrush(QColor(issue.isError ? "#f87171" : "#fbbf24"))); 
        item->setData(Qt::UserRole, issue.primitiveIndex);
        m_srcList->addItem(item);
    }

    if (issues.isEmpty()) {
        auto* item = new QListWidgetItem("✅ Symbol passed all checks. No issues found.");
        item->setForeground(QBrush(QColor("#34d399"))); // Green
        m_srcList->addItem(item);
    }

    // Switch to the SRC tab
    if (auto* dock = findChild<QDockWidget*>("SRCDock")) {
        dock->raise();
    }
}

void SymbolEditor::updateVisualForPrimitive(int index, const SymbolPrimitive& prim) {
    if (index < 0 || index >= m_drawnItems.size()) return;

    QGraphicsItem* old = m_drawnItems[index];
    QGraphicsItem* fresh = buildVisual(prim, index);
    
    if (fresh) {
        m_scene->removeItem(old);
        delete old;
        m_scene->addItem(fresh);
        m_drawnItems[index] = fresh;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  SymbolEditor – Key events
// ─────────────────────────────────────────────────────────────────────────────

void SymbolEditor::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        // Finalize pen path if active
        if (m_currentTool == Pen && m_penPoints.size() >= 2) {
            finalizePenPath();
        }
        // Switch to select tool
        m_currentTool = Select;
        if (m_view) m_view->setCurrentTool(0);
        if (m_view) m_view->setDragMode(QGraphicsView::RubberBandDrag);
        m_polyPoints.clear();
        clearPenState();
        m_penFinalizing = false;  // Reset guard flag after all cleanup
        if (m_selectAction) m_selectAction->setChecked(true);
        event->accept();
        return;
    } else if (event->key() == Qt::Key_Delete ||
               (event->key() == Qt::Key_Backspace && m_currentTool != Pen)) {
        onDelete();
        event->accept();
        return;
    } else if (event->key() == Qt::Key_R && m_currentTool == Pin) {
        // Cycle orientation: Right -> Down -> Left -> Up
        if (m_previewOrientation == "Right") m_previewOrientation = "Down";
        else if (m_previewOrientation == "Down") m_previewOrientation = "Left";
        else if (m_previewOrientation == "Left") m_previewOrientation = "Up";
        else m_previewOrientation = "Right";
        
        // Refresh preview immediately
        QPointF scenePos = m_view->snapToGrid(m_view->mapToScene(m_view->mapFromGlobal(QCursor::pos())));
        updatePinPreview(scenePos);
        event->accept();
        return;
    } else if (event->key() == Qt::Key_Return && m_currentTool == Pen) {
        // Enter key: finalize pen path
        finalizePenPath();
        event->accept();
        return;
    } else if (event->key() == Qt::Key_Backspace && m_currentTool == Pen && !m_penPoints.isEmpty()) {
        // Backspace: remove last point
        m_penPoints.removeLast();
        updatePenPreview();
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void SymbolEditor::setProjectKey(const QString& key) {
    m_projectKey = key.trimmed();

    if (!m_projectKey.isEmpty()) {
        const QString projectTpl = projectWizardTemplatesPath(m_projectKey);
        const QString legacyTpl = legacyGlobalWizardTemplatesPath();
        if (!projectTpl.isEmpty() && !QFileInfo::exists(projectTpl) && QFileInfo::exists(legacyTpl)) {
            QDir().mkpath(QFileInfo(projectTpl).absolutePath());
            QFile::copy(legacyTpl, projectTpl);
        }
        ensureProjectWizardTemplatesFile(m_projectKey);
    }

    refreshWizardTemplateList(m_wizardTemplateSearchEdit ? m_wizardTemplateSearchEdit->text() : QString());
    onWizardApplyTemplate();

    const QString stateKey = symbolEditorStateKey(m_projectKey);
    QByteArray geom = ConfigManager::instance().windowGeometry(stateKey);
    QByteArray state = ConfigManager::instance().windowState(stateKey);
    if (geom.isEmpty()) {
        // Fall back to global editor state
        geom = ConfigManager::instance().windowGeometry("SymbolEditor");
        state = ConfigManager::instance().windowState("SymbolEditor");
    }
    if (!geom.isEmpty()) {
        restoreGeometry(geom);
    } else {
        resize(1100, 720);
        if (QScreen* screen = QGuiApplication::primaryScreen()) {
            const QRect area = screen->availableGeometry();
            move(area.center() - QPoint(width() / 2, height() / 2));
        }
    }
    if (!state.isEmpty()) restoreState(state);
}

void SymbolEditor::closeEvent(QCloseEvent* event) {
    const bool metadataDirty =
        (m_symbol.name() != m_nameEdit->text()) ||
        (m_symbol.description() != m_descriptionEdit->text()) ||
        (m_symbol.category() != m_categoryCombo->currentText()) ||
        (m_symbol.referencePrefix() != m_prefixEdit->text()) ||
        (m_symbol.modelSource() != m_modelSourceCombo->currentData().toString()) ||
        (m_symbol.modelPath() != m_modelPathEdit->text()) ||
        (m_symbol.modelName() != m_modelNameEdit->text());
    const bool hasUnsavedChanges = (m_undoStack && !m_undoStack->isClean()) || metadataDirty;
    if (hasUnsavedChanges) {
        QMessageBox msg(this);
        msg.setWindowTitle("Unsaved Changes");
        msg.setText("You have unsaved changes. Would you like to save them before closing?");
        QPushButton* saveAsBtn = msg.addButton("Save As...", QMessageBox::AcceptRole);
        QPushButton* saveLibBtn = msg.addButton("Save to Library", QMessageBox::AcceptRole);
        QPushButton* discardBtn = msg.addButton("Discard", QMessageBox::DestructiveRole);
        msg.addButton("Cancel", QMessageBox::RejectRole);
        msg.setDefaultButton(saveAsBtn);

        msg.exec();
        QAbstractButton* clicked = msg.clickedButton();
        if (clicked == saveAsBtn) {
            onExportVioSym();
            if (m_undoStack->isClean()) event->accept();
            else event->ignore();
        } else if (clicked == saveLibBtn) {
            onSaveToLibrary();
            if (m_undoStack->isClean()) event->accept();
            else event->ignore();
        } else if (clicked == discardBtn) {
            event->accept();
        } else {
            event->ignore();
        }
    } else {
        event->accept();
    }
    if (event->isAccepted()) {
        ConfigManager::instance().saveWindowState(symbolEditorStateKey(m_projectKey), saveGeometry(), saveState());
    }
}

void SymbolEditor::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    if (QScreen* screen = window()->screen()) {
        const QRect area = screen->availableGeometry();
        QRect g = frameGeometry();
        if (g.height() > area.height() || g.width() > area.width()) {
            const int w = qMin(g.width(), area.width());
            const int h = qMin(g.height(), area.height());
            resize(w, h);
            g = frameGeometry();
        }
        if (!area.contains(g.center())) {
            move(area.center() - QPoint(width() / 2, height() / 2));
        }
    }
}

void SymbolEditor::onLibraryContextMenu(const QPoint& pos) {
    QTreeWidgetItem* item = m_libraryTree->itemAt(pos);
    if (!item) return;

    QString libName = item->data(0, Qt::UserRole).toString();
    if (libName.isEmpty()) return;

    QString symbolName = item->text(0);
    SymbolLibrary* lib = SymbolLibraryManager::instance().findLibrary(libName);
    if (!lib) return;

    QMenu menu(this);
    menu.setStyleSheet(ThemeManager::theme() ? ThemeManager::theme()->widgetStylesheet() : "");

    QAction* editAct = menu.addAction(getThemeIcon(":/icons/tool_line.svg"), "Edit Symbol");
    QAction* dupAct  = menu.addAction(getThemeIcon(":/icons/tool_duplicate.svg"), "Duplicate / Copy");
    QAction* deriveAct = menu.addAction(getThemeIcon(":/icons/tool_bezier.svg"), "Create Derived Symbol");
    menu.addSeparator();
    QAction* placeAct = menu.addAction(getThemeIcon(":/icons/nav_pcb.svg"), "Place in Schematic");
    menu.addSeparator();
    QAction* delAct = menu.addAction(getThemeIcon(":/icons/tool_delete.svg"), "Delete from Library");

    if (lib->isBuiltIn()) {
        editAct->setText("View Symbol (Read-Only)");
        delAct->setEnabled(false);
    }

    QAction* selected = menu.exec(m_libraryTree->mapToGlobal(pos));
    if (!selected) return;

    SymbolDefinition* def = lib->findSymbol(symbolName);
    if (!def) return;

    if (selected == editAct) {
        setSymbolDefinition(*def);
    } else if (selected == deriveAct) {
        SymbolDefinition derived;
        derived.setName(def->name() + "_Derived");
        derived.setParentName(def->name());
        derived.setParentLibrary(libName);
        derived.setCategory(def->category());
        derived.setReferencePrefix(def->referencePrefix());
        setSymbolDefinition(derived);
        statusBar()->showMessage("Created derived symbol inheriting from " + def->name(), 3000);
    } else if (selected == dupAct) {
        SymbolDefinition copy = def->clone();
        copy.setName(copy.name() + "_Copy");
        setSymbolDefinition(copy);
        statusBar()->showMessage("Symbol copied. Edit and save to library.", 3000);
    } else if (selected == placeAct) {
        Q_EMIT placeInSchematicRequested(*def);
    } else if (selected == delAct) {
        if (QMessageBox::question(this, "Delete Symbol", 
            QString("Are you sure you want to delete '%1' from library '%2'?").arg(symbolName, libName)) == QMessageBox::Yes) {
            lib->removeSymbol(symbolName);
            lib->save();
            populateLibraryTree();
        }
    }
}

void SymbolEditor::onLibraryItemClicked(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    if (!item) return;

    QString libName = item->data(0, Qt::UserRole).toString();
    if (libName.isEmpty()) {
        m_libPreviewScene->clear();
        return;
    }

    QString symbolName = item->text(0);
    SymbolLibrary* lib = SymbolLibraryManager::instance().findLibrary(libName);
    if (!lib) return;

    SymbolDefinition* def = lib->findSymbol(symbolName);
    if (!def) return;

    m_libPreviewScene->clear();
    const QList<SymbolPrimitive>& prims = def->primitives();
    for (int i = 0; i < prims.size(); ++i) {
        QGraphicsItem* visual = buildVisual(prims[i], -1);
        if (visual) {
            visual->setFlag(QGraphicsItem::ItemIsSelectable, false);
            visual->setFlag(QGraphicsItem::ItemIsMovable, false);
            m_libPreviewScene->addItem(visual);
        }
    }

    QRectF bounds = def->boundingRect();
    auto* refLabel = new QGraphicsSimpleTextItem(def->referencePrefix() + "?");
    refLabel->setBrush(QColor("#4db6ac"));
    refLabel->setFont(QFont("SansSerif", 8, QFont::Bold));
    refLabel->setPos(bounds.left(), bounds.top() - 20);
    m_libPreviewScene->addItem(refLabel);

    auto* nameLabel = new QGraphicsSimpleTextItem(def->name());
    nameLabel->setBrush(QColor("#64b5f6"));
    nameLabel->setFont(QFont("SansSerif", 8, QFont::Bold));
    nameLabel->setPos(bounds.left(), bounds.bottom() + 5);
    m_libPreviewScene->addItem(nameLabel);

    m_libPreviewView->fitInView(m_libPreviewScene->itemsBoundingRect().adjusted(-15,-15,15,15), Qt::KeepAspectRatio);
}

void SymbolEditor::onCanvasContextMenu(const QPoint& pos) {
    QGraphicsItem* item = m_view->itemAt(pos);
    QMenu menu(this);
    menu.setStyleSheet(ThemeManager::theme() ? ThemeManager::theme()->widgetStylesheet() : "");

    if (item) {
        if (!item->isSelected()) {
            m_scene->clearSelection();
            item->setSelected(true);
        }
        menu.addAction(getThemeIcon(":/icons/tool_duplicate.svg"), "Duplicate", this, &SymbolEditor::onDuplicate);
        menu.addAction(getThemeIcon(":/icons/undo.svg"), "Copy", this, &SymbolEditor::onCopy);
        menu.addSeparator();
        menu.addAction(getThemeIcon(":/icons/tool_rotate.svg"), "Rotate 90° CW", this, &SymbolEditor::onRotateCW);
        menu.addAction(getThemeIcon(":/icons/tool_rotate_ccw.svg"), "Rotate 90° CCW", this, &SymbolEditor::onRotateCCW);
        QMenu* flipMenu = menu.addMenu("Flip");
        flipMenu->addAction(getThemeIcon(":/icons/flip_h.svg"), "Flip Horizontal", this, &SymbolEditor::onFlipH);
        flipMenu->addAction(getThemeIcon(":/icons/flip_v.svg"), "Flip Vertical", this, &SymbolEditor::onFlipV);
        
        menu.addSeparator();
        menu.addAction("Match Spacing...", this, &SymbolEditor::onMatchSpacing);
        menu.addAction("Snap to Grid", this, &SymbolEditor::onSnapToGrid);
        menu.addAction("Copy to Alternate Style", this, &SymbolEditor::onCopyToAlternateStyle);

        menu.addSeparator();
        menu.addAction(getThemeIcon(":/icons/tool_delete.svg"), "Delete", this, &SymbolEditor::onDelete);
    } else {
        // Empty space context menu
        menu.addAction(getThemeIcon(":/icons/toolbar_new.png"), "New Symbol", this, &SymbolEditor::onNewSymbol);
        menu.addAction(getThemeIcon(":/icons/check.svg"), "Save to Library", this, &SymbolEditor::onSaveToLibrary);
        menu.addAction(getThemeIcon(":/icons/toolbar_file.png"), "Save As...", this, &SymbolEditor::onExportVioSym);
        menu.addAction(getThemeIcon(":/icons/schematic_editor.png"), "Place in Schematic", this, &SymbolEditor::onPlaceInSchematic);
        
        menu.addSeparator();
        menu.addAction(getThemeIcon(":/icons/redo.svg"), "Paste", this, &SymbolEditor::onPaste);
        menu.addAction("Select All", QKeySequence::SelectAll, [this](){ 
            for (auto* it : m_scene->items()) it->setSelected(true); 
        });
        
        menu.addSeparator();
        menu.addAction(getThemeIcon(":/icons/view_fit.svg"), "Zoom Fit", this, &SymbolEditor::onZoomFit);
        menu.addAction(getThemeIcon(":/icons/view_zoom_in.svg"), "Zoom Selection", this, &SymbolEditor::onZoomSelection);
        
        menu.addSeparator();
        QMenu* addMenu = menu.addMenu("Add Primitive");
        addMenu->addAction(getThemeIcon(":/icons/tool_pin.svg"), "Pin", [this, pos]() { m_currentTool = Pin; updatePinPreview(m_view->mapToScene(pos)); });
        addMenu->addAction(getThemeIcon(":/icons/tool_line.svg"), "Line", [this]() { m_currentTool = Line; });
        addMenu->addAction(getThemeIcon(":/icons/tool_rect.svg"), "Rectangle", [this]() { m_currentTool = Rect; });
        addMenu->addAction(getThemeIcon(":/icons/tool_circle.svg"), "Circle", [this]() { m_currentTool = Circle; });
        addMenu->addAction(getThemeIcon(":/icons/tool_arc.svg"), "Arc", [this]() { m_currentTool = Arc; });
        addMenu->addAction(getThemeIcon(":/icons/tool_polygon.svg"), "Polygon", [this]() { m_currentTool = Polygon; });
        addMenu->addAction(getThemeIcon(":/icons/tool_bezier.svg"), "Bezier Curve", [this]() { m_currentTool = Bezier; });
        addMenu->addAction(getThemeIcon(":/icons/tool_pen.svg"), "Pen Tool", [this]() { m_currentTool = Pen; });
        addMenu->addAction(getThemeIcon(":/icons/tool_text.svg"), "Text", [this, pos]() { 
            m_currentTool = Text; 
            QPointF scenePos = m_view->snapToGrid(m_view->mapToScene(pos));
            Q_EMIT m_view->pointClicked(scenePos); 
        });
        addMenu->addSeparator();
        addMenu->addAction(getThemeIcon(":/icons/tool_anchor.svg"), "Set Anchor Point", [this]() { m_currentTool = Anchor; });
        addMenu->addAction(getThemeIcon(":/icons/toolbar_refresh.png"), "Import Image", this, &SymbolEditor::onImportImage);

        menu.addSeparator();
        QAction* snapAct = menu.addAction(getThemeIcon(":/icons/snap_grid.svg"), "Magnet Pull (Snapping)");
        snapAct->setCheckable(true);
        snapAct->setChecked(m_view ? m_view->snapToGridEnabled() : true);
        connect(snapAct, &QAction::toggled, this, [this](bool on) {
            if (m_view) m_view->setSnapToGrid(on);
        });
    }

    menu.exec(m_view->viewport()->mapToGlobal(pos));
}


void SymbolEditor::updateCodePreview() {
    if (!m_codePreview) return;
    QJsonDocument doc(symbolDefinition().toJson());
    m_codePreview->setPlainText(doc.toJson(QJsonDocument::Indented));
}

void SymbolEditor::tryAutoDetectModelName() {
    if (!m_modelNameEdit || !m_modelPathEdit || !m_modelSourceCombo) return;
    if (!m_modelNameEdit->text().trimmed().isEmpty()) return;
    const QString rawPath = m_modelPathEdit->text().trimmed();
    if (rawPath.isEmpty()) return;
    const QString source = m_modelSourceCombo->currentData().toString();
    const QString resolved = resolveModelPathForEditor(rawPath, source, m_projectKey);
    if (resolved.isEmpty()) return;
    const QString prefix = m_prefixEdit ? m_prefixEdit->text() : QString();
    const QString detected = detectModelNameFromFile(resolved, prefix);
    if (!detected.isEmpty()) {
        m_modelNameEdit->setText(detected);
    }
}

// Select mode bezier editing - show edit points when bezier is selected
void SymbolEditor::updateBezierEditPreview() {
    // Clear previous bezier edit visualization
    for (auto* marker : m_bezierEditMarkers) {
        m_scene->removeItem(marker);
        delete marker;
    }
    m_bezierEditMarkers.clear();
    for (auto* line : m_bezierEditLines) {
        m_scene->removeItem(line);
        delete line;
    }
    m_bezierEditLines.clear();
    
    // If no bezier being edited, we're done
    if (m_editingBezierIndex < 0 || m_editingBezierIndex >= m_symbol.primitives().size()) {
        m_editingBezierIndex = -1;
        m_selectedBezierPoint = -1;
        return;
    }
    
    // Get the bezier primitive
    const SymbolPrimitive& prim = m_symbol.primitives()[m_editingBezierIndex];
    if (prim.type != SymbolPrimitive::Bezier) {
        m_editingBezierIndex = -1;
        return;
    }
    
     // Extract bezier points from data
     auto getPoint = [&](const QString& x_key, const QString& y_key) -> QPointF {
         double x = prim.data.contains(x_key) ? prim.data.value(x_key).toDouble() : 0.0;
         double y = prim.data.contains(y_key) ? prim.data.value(y_key).toDouble() : 0.0;
         return QPointF(x, y);
     };
    
    QPointF p1 = getPoint("x1", "y1");  // Start point
    QPointF cp1 = getPoint("x2", "y2"); // Control point 1
    QPointF cp2 = getPoint("x3", "y3"); // Control point 2
    QPointF p4 = getPoint("x4", "y4");  // End point
    
    m_bezierEditPoints.clear();
    m_bezierEditPoints.append({0, p1});   // Start anchor
    m_bezierEditPoints.append({1, cp1});  // Control 1
    m_bezierEditPoints.append({2, cp2});  // Control 2
    m_bezierEditPoints.append({3, p4});   // End anchor
    
    // Draw handle lines (anchor to control point)
    auto drawHandle = [&](QPointF anchor, QPointF control) {
        auto* line = m_scene->addLine(QLineF(anchor, control), 
                                     QPen(QColor(150, 150, 150), 1.5, Qt::DashLine));
        line->setZValue(1000);
        m_bezierEditLines.append(line);
    };
    
    drawHandle(p1, cp1);
    drawHandle(p4, cp2);
    
    // Draw edit point markers (anchors + control points)
    for (int i = 0; i < m_bezierEditPoints.size(); ++i) {
        const BezierEditPoint& bp = m_bezierEditPoints[i];
        
        // Determine color and size based on point type and selection
        QColor color;
        int size;
        
        if (i == m_selectedBezierPoint) {
            // Selected point - bright blue
            color = QColor(66, 165, 245);
            size = 10;
        } else if (i == 0) {
            // Start anchor - green
            color = Qt::green;
            size = 8;
        } else if (i == 3) {
            // End anchor - blue
            color = QColor(33, 150, 243);
            size = 8;
        } else {
            // Control points - purple
            color = QColor(156, 39, 176);
            size = 6;
        }
        
        int offset = size / 2;
        auto* marker = m_scene->addEllipse(bp.pos.x() - offset, bp.pos.y() - offset, size, size,
                                          QPen(color, 1.5), QBrush(color));
        marker->setZValue(1001);
        m_bezierEditMarkers.append(marker);
    }
}

// Detect and handle clicks on bezier edit points
void SymbolEditor::onBezierEditPointClicked(QPointF pos) {
    if (m_editingBezierIndex < 0 || m_bezierEditPoints.isEmpty()) return;
    
    const double HIT_RADIUS = 10.0;
    
    // Find which point was clicked
    int clickedPoint = -1;
    for (int i = 0; i < m_bezierEditPoints.size(); ++i) {
        QPointF delta = m_bezierEditPoints[i].pos - pos;
        double dist = std::sqrt(delta.x() * delta.x() + delta.y() * delta.y());
        if (dist <= HIT_RADIUS) {
            clickedPoint = i;
            break;
        }
    }
    
    if (clickedPoint >= 0) {
        m_selectedBezierPoint = clickedPoint;
        updateBezierEditPreview();
    } else {
        m_selectedBezierPoint = -1;
        updateBezierEditPreview();
    }
}

// Handle dragging of bezier edit points
void SymbolEditor::onBezierEditPointDragged(QPointF newPos) {
    if (m_editingBezierIndex < 0 || m_selectedBezierPoint < 0) return;
    if (m_editingBezierIndex >= m_symbol.primitives().size()) return;
    
    // Get mutable reference to the primitive
    SymbolPrimitive& prim = m_symbol.primitives()[m_editingBezierIndex];
    
    // Map point type to data keys
    const QStringList keys = {"x1", "y1", "x2", "y2", "x3", "y3", "x4", "y4"};
    int pointType = m_selectedBezierPoint;
    
    if (pointType >= 0 && pointType < 4) {
        int x_idx = pointType * 2;
        int y_idx = pointType * 2 + 1;
        
        prim.data[keys[x_idx]] = newPos.x();
        prim.data[keys[y_idx]] = newPos.y();
        
        // Update visual representation
        updateVisualForPrimitive(m_editingBezierIndex, prim);
        updateBezierEditPreview();
    }
}

void SymbolEditor::onMoveExactly() {}
void SymbolEditor::onAddPrimitiveExact() {}
bool SymbolEditor::importKicadSymbol(const QString&, const QString&) { return false; }
bool SymbolEditor::importLtspiceSymbol(const QString& path) {
    if (path.isEmpty()) return false;
    auto result = LtspiceSymbolImporter::importSymbolDetailed(path);
    if (!result.success || !result.symbol.isValid()) {
        const QString msg = result.errorMessage.isEmpty()
                                ? "Failed to import LTspice symbol."
                                : result.errorMessage;
        QMessageBox::critical(this, "Import Error", msg);
        return false;
    }
    setSymbolDefinition(result.symbol);
    tryAutoDetectModelName();
    return true;
}
bool SymbolEditor::loadLibrary(const QString&) { return false; }
