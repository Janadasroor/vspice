#include "pcb_layer.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <cmath>

// ============================================================================
// PCBLayer Implementation
// ============================================================================

PCBLayer::PCBLayer(int id, const QString& name, LayerType type, Side side)
    : m_id(id)
    , m_name(name)
    , m_type(type)
    , m_side(side)
    , m_visible(true)
    , m_locked(false)
    , m_opacity(1.0)
{
    // Set default colors based on layer type and side
    switch (type) {
        case Copper:
            m_color = (side == Top) ? PCBLayerManager::copperTopColor() 
                                    : PCBLayerManager::copperBottomColor();
            break;
        case Silkscreen:
            m_color = PCBLayerManager::silkscreenColor();
            break;
        case Soldermask:
            m_color = PCBLayerManager::soldermaskColor();
            break;
        case EdgeCuts:
            m_color = PCBLayerManager::edgeCutsColor();
            break;
        case Drill:
            m_color = PCBLayerManager::drillColor();
            break;
        default:
            m_color = QColor(128, 128, 128);
            break;
    }
}

QString PCBLayer::typeString() const {
    switch (m_type) {
        case Copper: return "Copper";
        case Silkscreen: return "Silkscreen";
        case Soldermask: return "Soldermask";
        case Paste: return "Paste";
        case Courtyard: return "Courtyard";
        case EdgeCuts: return "Edge Cuts";
        case Drill: return "Drill";
        case UserDefined: return "User Defined";
        default: return "Unknown";
    }
}

QString PCBLayer::sideString() const {
    switch (m_side) {
        case Top: return "Top";
        case Bottom: return "Bottom";
        case Internal: return "Internal";
        case Both: return "Both";
        default: return "Unknown";
    }
}

// ============================================================================
// PCBLayerManager Implementation
// ============================================================================

PCBLayerManager::PCBLayerManager()
    : m_activeLayerId(TopCopper)
    , m_copperLayerCount(2)
{
    initializeStandardLayers();
    updateStackupFromLayerCount(2);
}

PCBLayerManager& PCBLayerManager::instance() {
    static PCBLayerManager instance;
    return instance;
}

void PCBLayerManager::initializeStandardLayers() {
    m_layers.clear();

    // Copper layers
    m_layers.append(PCBLayer(TopCopper, "Top Copper", PCBLayer::Copper, PCBLayer::Top));
    m_layers.append(PCBLayer(BottomCopper, "Bottom Copper", PCBLayer::Copper, PCBLayer::Bottom));

    // Silkscreen layers
    m_layers.append(PCBLayer(TopSilkscreen, "Top Silkscreen", PCBLayer::Silkscreen, PCBLayer::Top));
    m_layers.append(PCBLayer(BottomSilkscreen, "Bottom Silkscreen", PCBLayer::Silkscreen, PCBLayer::Bottom));

    // Soldermask layers
    m_layers.append(PCBLayer(TopSoldermask, "Top Soldermask", PCBLayer::Soldermask, PCBLayer::Top));
    m_layers.append(PCBLayer(BottomSoldermask, "Bottom Soldermask", PCBLayer::Soldermask, PCBLayer::Bottom));

    // Paste layers
    m_layers.append(PCBLayer(TopPaste, "Top Paste", PCBLayer::Paste, PCBLayer::Top));
    m_layers.append(PCBLayer(BottomPaste, "Bottom Paste", PCBLayer::Paste, PCBLayer::Bottom));

    // Mechanical layers
    m_layers.append(PCBLayer(EdgeCuts, "Edge Cuts", PCBLayer::EdgeCuts, PCBLayer::Both));
    m_layers.append(PCBLayer(Drills, "Drills", PCBLayer::Drill, PCBLayer::Both));

    emit layerListChanged();
}

PCBLayer* PCBLayerManager::layer(int id) {
    for (int i = 0; i < m_layers.size(); ++i) {
        if (m_layers[i].id() == id) {
            return &m_layers[i];
        }
    }
    return nullptr;
}

PCBLayer* PCBLayerManager::layer(const QString& name) {
    for (int i = 0; i < m_layers.size(); ++i) {
        if (m_layers[i].name() == name) {
            return &m_layers[i];
        }
    }
    return nullptr;
}

PCBLayer* PCBLayerManager::activeLayer() {
    return layer(m_activeLayerId);
}

void PCBLayerManager::setActiveLayer(int id) {
    if (m_activeLayerId != id) {
        PCBLayer* l = layer(id);
        if (l && !l->isLocked()) {
            m_activeLayerId = id;
            emit activeLayerChanged(id);
        }
    }
}

void PCBLayerManager::setActiveLayer(const QString& name) {
    PCBLayer* l = layer(name);
    if (l) {
        setActiveLayer(l->id());
    }
}

void PCBLayerManager::setLayerVisible(int id, bool visible) {
    PCBLayer* l = layer(id);
    if (l && l->isVisible() != visible) {
        l->setVisible(visible);
        emit layerVisibilityChanged(id, visible);
    }
}

void PCBLayerManager::setLayerLocked(int id, bool locked) {
    PCBLayer* l = layer(id);
    if (l) {
        l->setLocked(locked);
    }
}

void PCBLayerManager::toggleLayerVisibility(int id) {
    PCBLayer* l = layer(id);
    if (l) {
        setLayerVisible(id, !l->isVisible());
    }
}

QList<PCBLayer*> PCBLayerManager::copperLayers() {
    QList<PCBLayer*> result;
    for (int i = 0; i < m_layers.size(); ++i) {
        if (m_layers[i].isCopperLayer()) {
            result.append(&m_layers[i]);
        }
    }
    return result;
}

int PCBLayerManager::copperLayerCount() const {
    return m_copperLayerCount;
}

void PCBLayerManager::setCopperLayerCount(int count) {
    if (count != m_copperLayerCount && count >= 2 && count <= 32) {
        m_copperLayerCount = count;
        
        // Ensure standard layers exist
        initializeStandardLayers();
        
        // Add internal layers if count > 2
        for (int i = 1; i < count - 1; ++i) {
            int id = 100 + i; // Internal layer IDs start at 100
            m_layers.append(PCBLayer(id, QString("In%1.Cu").arg(i), PCBLayer::Copper, PCBLayer::Internal));
        }

        updateStackupFromLayerCount(count);
        emit layerListChanged();
    }
}

void PCBLayerManager::setStackup(const BoardStackup& stackup) {
    m_stackup = stackup;
    emit layerListChanged();
}

void PCBLayerManager::updateStackupFromLayerCount(int count) {
    m_stackup.stack.clear();
    m_stackup.finishThickness = 1.6; // Default standard thickness
    m_stackup.surfaceFinish = "ENIG";
    m_stackup.solderMaskExpansion = 0.05;
    m_stackup.pasteExpansion = 0.00;

    // Top Solder Mask
    m_stackup.stack.append({TopSoldermask, "Top Solder Mask", "Soldermask", 0.02, 3.5, "Epoxy", 0.0});
    
    // Top Copper
    m_stackup.stack.append({TopCopper, "Top Copper", "Copper", 0.035, 0, "Copper", 1.0});

    // Internal Layers
    if (count > 2) {
        double coreThickness = 1.6 / (count - 1);
        for (int i = 1; i < count - 1; ++i) {
            m_stackup.stack.append({-1, "Dielectric", "Prepreg", coreThickness, 4.2, "FR-4", 0.0});
            m_stackup.stack.append({100 + i, QString("In%1.Cu").arg(i), "Copper", 0.035, 0, "Copper", 1.0});
        }
        m_stackup.stack.append({-1, "Dielectric", "Core", coreThickness, 4.2, "FR-4", 0.0});
    } else {
        // Standard FR-4 Core
        m_stackup.stack.append({-1, "Dielectric", "Core", 1.5, 4.2, "FR-4", 0.0});
    }

    // Bottom Copper
    m_stackup.stack.append({BottomCopper, "Bottom Copper", "Copper", 0.035, 0, "Copper", 1.0});
    
    // Bottom Solder Mask
    m_stackup.stack.append({BottomSoldermask, "Bottom Solder Mask", "Soldermask", 0.02, 3.5, "Epoxy", 0.0});
}

QJsonObject PCBLayerManager::toJson() const {
    QJsonObject json;
    QJsonArray stackArray;
    for (const auto& layer : m_stackup.stack) {
        QJsonObject layerObj;
        layerObj["id"] = layer.layerId;
        layerObj["name"] = layer.name;
        layerObj["type"] = layer.type;
        layerObj["thickness"] = layer.thickness;
        layerObj["er"] = layer.dielectricConstant;
        layerObj["material"] = layer.material;
        layerObj["copperWeightOz"] = layer.copperWeightOz;
        stackArray.append(layerObj);
    }
    json["stackup"] = stackArray;
    json["finishThickness"] = m_stackup.finishThickness;
    json["surfaceFinish"] = m_stackup.surfaceFinish;
    json["solderMaskExpansion"] = m_stackup.solderMaskExpansion;
    json["pasteExpansion"] = m_stackup.pasteExpansion;
    json["copperLayerCount"] = m_copperLayerCount;
    return json;
}

void PCBLayerManager::fromJson(const QJsonObject& json) {
    if (json.contains("copperLayerCount")) {
        setCopperLayerCount(json["copperLayerCount"].toInt(2));
    }
    if (json.contains("stackup")) {
        m_stackup.stack.clear();
        QJsonArray stackArray = json["stackup"].toArray();
        for (const QJsonValue& val : stackArray) {
            QJsonObject obj = val.toObject();
            StackupLayer layer;
            layer.layerId = obj["id"].toInt();
            layer.name = obj["name"].toString();
            layer.type = obj["type"].toString();
            layer.thickness = obj["thickness"].toDouble();
            layer.dielectricConstant = obj["er"].toDouble();
            layer.material = obj["material"].toString();
            layer.copperWeightOz = obj["copperWeightOz"].toDouble(layer.type == "Copper" ? 1.0 : 0.0);
            m_stackup.stack.append(layer);
        }
        m_stackup.finishThickness = json["finishThickness"].toDouble(1.6);
        m_stackup.surfaceFinish = json["surfaceFinish"].toString("ENIG");
        m_stackup.solderMaskExpansion = json["solderMaskExpansion"].toDouble(0.05);
        m_stackup.pasteExpansion = json["pasteExpansion"].toDouble(0.0);
        emit layerListChanged();
    }
}
