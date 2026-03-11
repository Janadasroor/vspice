#include "visual_spice_mapper_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QFormLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QGroupBox>

VisualSpiceMapperDialog::VisualSpiceMapperDialog(const SymbolDefinition& symbol, QWidget* parent)
    : QDialog(parent), m_symbol(symbol) {
    setWindowTitle(QString("SPICE Model Mapper - %1").arg(symbol.name()));
    resize(600, 500);
    setStyleSheet("QDialog { background-color: #1e1e1e; color: #cccccc; }");
    
    setupUI();
}

VisualSpiceMapperDialog::~VisualSpiceMapperDialog() {}

void VisualSpiceMapperDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // 1. Model Name
    QGroupBox* modelGroup = new QGroupBox("SPICE Model Settings", this);
    modelGroup->setStyleSheet("QGroupBox { color: #888888; font-weight: bold; }");
    QFormLayout* modelForm = new QFormLayout(modelGroup);
    m_modelNameEdit = new QLineEdit(m_symbol.spiceModelName(), this);
    m_modelNameEdit->setPlaceholderText("e.g. 2N3904, TL072, etc.");
    m_modelNameEdit->setStyleSheet("background: #2d2d2d; border: 1px solid #3d3d3d; padding: 5px; color: #ffffff;");
    modelForm->addRow("Model/Subcircuit Name:", m_modelNameEdit);
    mainLayout->addWidget(modelGroup);
    
    // 2. Mapper Widget
    mainLayout->addWidget(new QLabel("Map Symbol Pins (Left) to SPICE Model Nodes (Right):", this));
    
    QStringList pinNames;
    for (const auto& prim : m_symbol.primitives()) {
        if (prim.type == Flux::Model::SymbolPrimitive::Pin) {
            pinNames.append(prim.data["number"].toString());
        }
    }
    
    // Infer node count from symbol pins (usually same, but can be more/less for subcircuits)
    int nodeCount = qMax(2, (int)pinNames.size());
    m_mapperWidget = new SpiceMappingWidget(pinNames, nodeCount, this);
    m_mapperWidget->setMapping(m_symbol.spiceNodeMapping());
    mainLayout->addWidget(m_mapperWidget, 1);
    
    // 3. Actions
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* cancelBtn = new QPushButton("Cancel", this);
    QPushButton* okBtn = new QPushButton("Save Mapping", this);
    okBtn->setStyleSheet("background-color: #007acc; color: white; padding: 8px 20px; font-weight: bold;");
    
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(okBtn, &QPushButton::clicked, this, &VisualSpiceMapperDialog::onAccept);
    
    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(okBtn);
    mainLayout->addLayout(btnLayout);
}

void VisualSpiceMapperDialog::onAccept() {
    m_mapping = m_mapperWidget->mapping();
    accept();
}

QString VisualSpiceMapperDialog::spiceModelName() const {
    return m_modelNameEdit->text().trimmed();
}

// -----------------------------------------------------------------------------
// SpiceMappingWidget
// -----------------------------------------------------------------------------

SpiceMappingWidget::SpiceMappingWidget(const QStringList& pins, int nodeCount, QWidget* parent)
    : QWidget(parent), m_pins(pins), m_nodeCount(nodeCount) {
    setMinimumHeight(300);
}

void SpiceMappingWidget::setMapping(const QMap<int, QString>& mapping) {
    m_mapping = mapping;
    update();
}

void SpiceMappingWidget::updateLayout() {
    m_pinPoints.clear();
    m_nodePoints.clear();
    
    int w = width();
    int h = height();
    int margin = 40;
    
    // Left: Pins
    int pinStep = (h - 2 * margin) / qMax(1, (int)m_pins.size());
    for (int i = 0; i < m_pins.size(); ++i) {
        PinPoint p;
        p.name = m_pins[i];
        p.rect = QRectF(margin, margin + i * pinStep - 10, 80, 20);
        m_pinPoints.append(p);
    }
    
    // Right: Nodes
    int nodeStep = (h - 2 * margin) / qMax(1, m_nodeCount);
    for (int i = 0; i < m_nodeCount; ++i) {
        NodePoint n;
        n.index = i + 1;
        n.rect = QRectF(w - margin - 80, margin + i * nodeStep - 10, 80, 20);
        m_nodePoints.append(n);
    }
}

void SpiceMappingWidget::paintEvent(QPaintEvent*) {
    updateLayout();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    painter.fillRect(rect(), QColor(25, 25, 25));
    
    // 1. Draw connections
    painter.setPen(QPen(QColor(0, 122, 204), 2));
    for (auto it = m_mapping.begin(); it != m_mapping.end(); ++it) {
        int nodeIdx = it.key();
        QString pinName = it.value();
        
        QPointF pinPos, nodePos;
        bool foundPin = false, foundNode = false;
        
        for (const auto& p : m_pinPoints) if (p.name == pinName) { pinPos = p.rect.center(); foundPin = true; break; }
        for (const auto& n : m_nodePoints) if (n.index == nodeIdx) { nodePos = n.rect.center(); foundNode = true; break; }
        
        if (foundPin && foundNode) {
            painter.drawLine(pinPos, nodePos);
        }
    }
    
    // 2. Draw dragging line
    if (m_isDragging && m_activePin != "") {
        painter.setPen(QPen(Qt::white, 1, Qt::DashLine));
        QPointF pinPos;
        for (const auto& p : m_pinPoints) if (p.name == m_activePin) { pinPos = p.rect.center(); break; }
        painter.drawLine(pinPos, m_dragPos);
    }
    
    // 3. Draw Pins (Left)
    for (const auto& p : m_pinPoints) {
        bool active = (m_activePin == p.name);
        painter.setPen(active ? Qt::white : QColor(150, 150, 150));
        painter.setBrush(active ? QColor(0, 122, 204) : QColor(45, 45, 45));
        painter.drawRoundedRect(p.rect, 4, 4);
        painter.drawText(p.rect, Qt::AlignCenter, p.name);
    }
    
    // 4. Draw Nodes (Right)
    for (const auto& n : m_nodePoints) {
        bool mapped = m_mapping.contains(n.index);
        painter.setPen(mapped ? Qt::white : QColor(100, 100, 100));
        painter.setBrush(mapped ? QColor(0, 122, 204) : QColor(35, 35, 35));
        painter.drawRoundedRect(n.rect, 4, 4);
        painter.drawText(n.rect, Qt::AlignCenter, QString("Node %1").arg(n.index));
    }
}

void SpiceMappingWidget::mousePressEvent(QMouseEvent* event) {
    for (const auto& p : m_pinPoints) {
        if (p.rect.contains(event->pos())) {
            m_activePin = p.name;
            m_isDragging = true;
            m_dragPos = event->pos();
            update();
            return;
        }
    }
    // Also allow clicking node to remove mapping
    for (const auto& n : m_nodePoints) {
        if (n.rect.contains(event->pos())) {
            if (m_mapping.contains(n.index)) {
                m_mapping.remove(n.index);
                update();
            }
            return;
        }
    }
}

void SpiceMappingWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_isDragging) {
        m_dragPos = event->pos();
        update();
    }
}

void SpiceMappingWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (m_isDragging) {
        for (const auto& n : m_nodePoints) {
            if (n.rect.contains(event->pos())) {
                // Map it!
                m_mapping[n.index] = m_activePin;
                break;
            }
        }
        m_isDragging = false;
        m_activePin = "";
        update();
    }
}
