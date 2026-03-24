// schematic_editor_ui.cpp
// Menu bar, toolbar, dock widgets, status bar creation for SchematicEditor

#include "schematic_editor.h"
#include "schematic_file_io.h"
#include "../analysis/schematic_erc.h"
#include "theme_manager.h"
#include "../../python/gemini_panel.h"
#include "schematic_commands.h"
#include "spice_directive_classifier.h"
#include "../dialogs/spice_mean_dialog.h"
#include "../../symbols/models/symbol_definition.h"
#include "../items/generic_component_item.h"
#include "../ui/schematic_components_widget.h"
#include "../ui/schematic_hierarchy_panel.h"
#include "../ui/simulation_panel.h"
#include "../ui/logic_analyzer_window.h"
#include "../../ui/source_control_panel.h"
#include "../../symbols/symbol_library.h"
#include "../dialogs/simulation_debugger_dialog.h"
#include "../dialogs/spice_directive_dialog.h"
#include "../../simulator/bridge/sim_manager.h"
#include "../tools/schematic_zoom_area_tool.h"

using Flux::Model::SymbolDefinition;
using Flux::Model::SymbolPrimitive;
#include "../items/schematic_item.h"
#include "../items/smart_signal_item.h"
#include "../ui/logic_editor_panel.h"
#include "../items/schematic_waveform_marker.h"
#include "../../simulator/bridge/sim_schematic_bridge.h"
#include "../../ui/source_control_panel.h"

#include <QMenuBar>
#include <QMenu>
#include <QActionGroup>
#include <QComboBox>
#include <QPainter>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QStatusBar>
#include <QGraphicsItem>
#include <QDir>
#include <QScrollArea>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QToolButton>
#include <QTimer>
#include <QApplication>
#include <QClipboard>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <array>

void SchematicEditor::createMenuBar() {
    // Hide traditional menu bar for modern UI
    menuBar()->hide();
}

QIcon SchematicEditor::getThemeIcon(const QString& path) {
    QIcon icon(path);
    if (!ThemeManager::theme() || ThemeManager::theme()->type() == PCBTheme::Dark) {
        return icon; // Keep original for dark theme
    }

    // List of icons that should keep their original multi-color design
    static const QStringList multiColorIcons = {
        "probe", "ammeter", "voltmeter", "power_meter", "scissor", "n-v-probe", "p-v-probe"
    };

    bool isMultiColor = false;
    for (const auto& tag : multiColorIcons) {
        if (path.contains(tag, Qt::CaseInsensitive)) {
            isMultiColor = true;
            break;
        }
    }

    if (isMultiColor) {
        return icon;
    }

    // For light theme, we need to tint the monochrome icons
    QPixmap pixmap = icon.pixmap(QSize(32, 32));
    if (pixmap.isNull()) return icon;

    QPainter painter(&pixmap);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), ThemeManager::theme()->textColor());
    painter.end();
    return QIcon(pixmap);
}

// Helper to create simple programmatic icons for components/tools
QIcon SchematicEditor::createComponentIcon(const QString& name) {
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Use theme color if possible, else fallback
    QColor color = Qt::white;
    if (ThemeManager::theme()) {
        color = ThemeManager::theme()->textColor();
    }
    QPen pen(color, 2);
    painter.setPen(pen);

    if (name == "Select") {
        painter.drawLine(8, 8, 24, 24);
        painter.drawLine(8, 8, 8, 24);
        painter.drawLine(8, 8, 24, 8);
    } else if (name == "Zoom Area") {
        painter.drawRect(6, 6, 20, 20);
        painter.drawLine(16, 16, 26, 26);
        painter.drawEllipse(10, 10, 10, 10);
    } else if (name == "Zoom Components") {
        painter.drawRect(6, 6, 20, 20);
        painter.drawLine(16, 16, 26, 26);
        // Draw small IC shape inside
        painter.drawRect(10, 10, 8, 8);
        painter.drawLine(8, 11, 10, 11); painter.drawLine(8, 13, 10, 13); painter.drawLine(8, 15, 10, 15); painter.drawLine(8, 17, 10, 17);
        painter.drawLine(18, 11, 20, 11); painter.drawLine(18, 13, 20, 13); painter.drawLine(18, 15, 20, 15); painter.drawLine(18, 17, 20, 17);
    } else if (name == "Sync") {
        painter.drawArc(6, 6, 14, 14, 35 * 16, 260 * 16);
        painter.drawLine(18, 6, 20, 9);
        painter.drawLine(18, 6, 15, 7);
        painter.drawArc(12, 12, 14, 14, 215 * 16, 260 * 16);
        painter.drawLine(14, 24, 11, 22);
        painter.drawLine(14, 24, 17, 23);
    } else if (name == "Leave Sheet") {
        painter.drawLine(8, 16, 24, 16); // Arrow shaft
        painter.drawLine(8, 16, 16, 8);  // Arrow head top
        painter.drawLine(8, 16, 16, 24); // Arrow head bottom
        painter.drawRect(22, 10, 4, 12); // Vertical bar (door/up)
    } else if (name == "Wire") {
        painter.drawLine(4, 28, 12, 16);
        painter.drawLine(12, 16, 20, 16);
        painter.drawLine(20, 16, 28, 4);
    } else if (name == "Probe" || name == "Voltage Probe" || name == "Current Probe" || name == "Power Probe" || name == "Logic Probe" || name == "Simulator") {
        QString iconPath = ":/icons/tool_probe.svg";
        if (name == "Voltage Probe") iconPath = ":/icons/tool_voltage_probe.svg";
        else if (name == "Current Probe") iconPath = ":/icons/tool_current_probe.svg";
        else if (name == "Power Probe") iconPath = ":/icons/tool_power_probe.svg";
        return getThemeIcon(iconPath);
    } else if (name == "Logic Probe") {
        painter.drawLine(24, 5, 24, 10);
        painter.setPen(QPen(QColor("#60a5fa"), 2));
        painter.drawRect(7, 5, 8, 8);
        painter.drawText(QRectF(7, 5, 8, 8), Qt::AlignCenter, "1");
    } else if (name == "Bus") {
        painter.setPen(QPen(Qt::blue, 4));
        painter.drawLine(4, 24, 12, 12);
        painter.drawLine(12, 12, 28, 12);
    } else if (name == "Bus Entry") {
        painter.setPen(QPen(Qt::blue, 1.5));
        painter.drawLine(10, 22, 22, 10);
    } else if (name == "No-Connect") {
        painter.setPen(QPen(Qt::red, 2));
        painter.drawLine(10, 10, 22, 22);
        painter.drawLine(10, 22, 22, 10);
    } else if (name == "Scissors" || name == "Erase") {
        painter.setPen(QPen(Qt::black, 2));
        painter.drawEllipse(4, 18, 8, 8);
        painter.drawEllipse(4, 6, 8, 8);
        painter.drawLine(11, 19, 25, 9);
        painter.drawLine(11, 13, 25, 23);
        painter.setBrush(Qt::black);
        painter.drawEllipse(11, 15, 2, 2);
    } else if (name == "Resistor") {
        painter.drawPolyline(QVector<QPointF>{
            {4, 16}, {8, 16}, {10, 10}, {14, 22}, {18, 10}, {22, 22}, {24, 16}, {28, 16}
        });
    } else if (name == "Capacitor") {
        painter.drawLine(4, 16, 12, 16);
        painter.drawLine(20, 16, 28, 16);
        painter.drawLine(12, 8, 12, 24);
        painter.drawLine(20, 8, 20, 24);
    } else if (name == "Inductor") {
        painter.drawArc(4, 12, 8, 8, 0, 180 * 16);
        painter.drawArc(12, 12, 8, 8, 0, 180 * 16);
        painter.drawArc(20, 12, 8, 8, 0, 180 * 16);
        painter.drawLine(4, 16, 4, 16); // Start point
    } else if (name == "Spice Directive") {
        painter.setPen(QPen(color, 2));
        painter.drawRect(4, 4, 24, 24);
        painter.setFont(QFont("Arial", 10, QFont::Bold));
        painter.drawText(QRect(4, 4, 24, 24), Qt::AlignCenter, ".op");
    } else if (name == "BV") {
        painter.setPen(QPen(color, 2));
        painter.drawEllipse(6, 6, 20, 20);
        painter.setFont(QFont("Arial", 10, QFont::Bold));
        painter.drawText(QRect(6, 6, 20, 20), Qt::AlignCenter, "B");
    } else if (name == "Diode") {
        painter.drawLine(4, 16, 28, 16);
        QPolygonF triangle;
        triangle << QPointF(12, 10) << QPointF(12, 22) << QPointF(20, 16);
        painter.drawPolygon(triangle);
        painter.drawLine(20, 10, 20, 22);
    } else if (name == "Transistor") {
        painter.drawEllipse(4, 4, 24, 24);
        painter.drawLine(16, 8, 16, 24); // Base
        painter.drawLine(16, 14, 22, 8); // Collector
        painter.drawLine(16, 18, 22, 24); // Emitter
    } else if (name == "IC") {
        painter.drawRect(8, 8, 16, 16);
        // Pins
        painter.drawLine(4, 10, 8, 10);
        painter.drawLine(4, 14, 8, 14);
        painter.drawLine(4, 18, 8, 18);
        painter.drawLine(4, 22, 8, 22);
        painter.drawLine(24, 10, 28, 10);
        painter.drawLine(24, 14, 28, 14);
        painter.drawLine(24, 18, 28, 18);
        painter.drawLine(24, 22, 28, 22);
    } else if (name == "GND") {
        painter.drawLine(8, 16, 24, 16);
        painter.drawLine(12, 20, 20, 20);
        painter.drawLine(15, 24, 17, 24);
        painter.drawLine(16, 8, 16, 16);
    } else if (name == "VCC" || name == "3.3V" || name == "5V" || name == "12V" || name == "VDD") {
        painter.drawLine(16, 8, 16, 24);
        painter.drawLine(12, 8, 20, 8);
    } else if (name == "Sheet") {
        painter.setPen(QPen(Qt::cyan, 2));
        painter.drawRect(4, 6, 24, 18);
        painter.setPen(QPen(Qt::cyan, 1));
        painter.drawLine(4, 10, 28, 10);
    } else if (name == "Rectangle") {
        painter.drawRect(4, 8, 24, 16);
    } else if (name == "Circle") {
        painter.drawEllipse(4, 4, 24, 24);
    } else if (name == "Line") {
        painter.drawLine(4, 28, 28, 4);
    } else if (name == "Polygon") {
        QPolygonF poly; poly << QPointF(16, 4) << QPointF(28, 12) << QPointF(22, 28) << QPointF(10, 28) << QPointF(4, 12);
        painter.drawPolygon(poly);
    } else if (name == "Bezier") {
        QPainterPath path; path.moveTo(4, 24); path.cubicTo(8, 4, 24, 4, 28, 24);
        painter.drawPath(path);
    } else if (name == "Text") {
        painter.setFont(QFont("Times", 20, QFont::Bold));
        painter.drawText(pixmap.rect(), Qt::AlignCenter, "T");
    } else if (name == "Voltmeter (DC)" || name == "Voltmeter (AC)") {
        painter.drawEllipse(4, 4, 24, 24);
        painter.setFont(QFont("Arial", 12, QFont::Bold));
        painter.drawText(pixmap.rect(), Qt::AlignCenter, "V");
    } else if (name == "Ammeter (DC)" || name == "Ammeter (AC)") {
        painter.drawEllipse(4, 4, 24, 24);
        painter.setFont(QFont("Arial", 12, QFont::Bold));
        painter.drawText(pixmap.rect(), Qt::AlignCenter, "A");
    } else if (name == "Wattmeter" || name == "Power Meter") {
        painter.drawEllipse(4, 4, 24, 24);
        painter.setFont(QFont("Arial", 11, QFont::Bold));
        painter.drawText(pixmap.rect(), Qt::AlignCenter, "W");
    } else if (name == "Frequency Counter") {
        painter.drawEllipse(4, 4, 24, 24);
        painter.setFont(QFont("Arial", 11, QFont::Bold));
        painter.drawText(pixmap.rect(), Qt::AlignCenter, "Hz");
    } else if (name == "Oscilloscope Instrument") {
        QPainterPath wave;
        wave.moveTo(4, 18);
        wave.lineTo(8, 18);
        wave.lineTo(8, 10);
        wave.lineTo(12, 10);
        wave.lineTo(12, 22);
        wave.cubicTo(15, 22, 17, 8, 20, 8);
        wave.cubicTo(22, 8, 24, 14, 28, 14);
        painter.drawRect(3, 5, 26, 18);
        painter.drawPath(wave);
    } else if (name == "Net Label") {
        painter.setFont(QFont("Arial", 10, QFont::Bold));
        painter.drawText(pixmap.rect(), Qt::AlignCenter, "ABC");
        painter.drawRect(2, 8, 28, 16);
    } else if (name == "Annotate") {
        painter.setFont(QFont("Inter", 10, QFont::Bold));
        painter.setPen(QPen(QColor("#3b82f6"), 2)); // Blue
        painter.drawText(pixmap.rect(), Qt::AlignCenter, "1..N");
        painter.drawRect(2, 2, 28, 28);
    } else if (name == "ERC") {
        painter.setFont(QFont("Inter", 10, QFont::Bold));
        painter.setPen(QPen(QColor("#10b981"), 2)); // Green
        painter.drawText(pixmap.rect(), Qt::AlignCenter, "ERC");
        painter.drawRect(2, 2, 28, 28);
    } else if (name == "Rotate CW") {
        painter.drawArc(8, 8, 16, 16, 45 * 16, 270 * 16);
        painter.drawLine(24, 16, 28, 16);
        painter.drawLine(24, 16, 24, 12);
    } else if (name == "Rotate CCW") {
        painter.save();
        painter.translate(0, 32);
        painter.scale(1, -1);
        painter.drawArc(8, 8, 16, 16, 45 * 16, -270 * 16);
        painter.drawLine(8, 16, 4, 16);
        painter.drawLine(8, 16, 8, 12);
        painter.restore();
    } else if (name == "Flip H") {
        painter.drawLine(16, 4, 16, 28); // Mirror line
        painter.drawPolygon(QPolygonF() << QPointF(6, 10) << QPointF(14, 16) << QPointF(6, 22));
        painter.drawPolygon(QPolygonF() << QPointF(26, 10) << QPointF(18, 16) << QPointF(26, 22));
    } else if (name == "Flip V") {
        painter.drawLine(4, 16, 28, 16); // Mirror line
        painter.drawPolygon(QPolygonF() << QPointF(10, 6) << QPointF(16, 14) << QPointF(22, 6));
        painter.drawPolygon(QPolygonF() << QPointF(10, 26) << QPointF(16, 18) << QPointF(22, 26));
    } else if (name == "Simulator") {
        // Draw Waveform ( sine + pulse mix)
        QPainterPath wave;
        wave.moveTo(4, 20);
        wave.lineTo(8, 20); wave.lineTo(8, 10); wave.lineTo(12, 10); wave.lineTo(12, 20);
        // Sine part
        for(int x=12; x<=28; ++x) {
            qreal y = 16 + 6 * std::sin((x-12) * 0.4);
            wave.lineTo(x, y);
        }
        painter.setPen(QPen(QColor("#a78bfa"), 2)); // Purple
        painter.drawPath(wave);
    } else if (name == "Front") {
        painter.setBrush(color);
        painter.drawRect(4, 4, 16, 16);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(12, 12, 16, 16);
    } else if (name == "Back") {
        painter.drawRect(4, 4, 16, 16);
        painter.setBrush(color);
        painter.drawRect(12, 12, 16, 16);
    } else if (name == "Align Left") {
        painter.drawLine(4, 4, 4, 28);
        painter.drawRect(6, 8, 10, 4);
        painter.drawRect(6, 16, 20, 4);
    } else if (name == "Align Right") {
        painter.drawLine(28, 4, 28, 28);
        painter.drawRect(18, 8, 10, 4);
        painter.drawRect(8, 16, 20, 4);
    } else if (name == "Align Top") {
        painter.drawLine(4, 4, 28, 4);
        painter.drawRect(8, 6, 4, 10);
        painter.drawRect(16, 6, 4, 20);
    } else if (name == "Align Bottom") {
        painter.drawLine(4, 28, 28, 28);
        painter.drawRect(8, 18, 4, 10);
        painter.drawRect(16, 8, 4, 20);
    } else if (name == "Center X") {
        painter.setPen(QPen(color, 1, Qt::DashLine));
        painter.drawLine(16, 2, 16, 30);
        painter.setPen(pen);
        painter.drawRect(10, 8, 12, 4);
        painter.drawRect(6, 18, 20, 4);
    } else if (name == "Center Y") {
        painter.setPen(QPen(color, 1, Qt::DashLine));
        painter.drawLine(2, 16, 30, 16);
        painter.setPen(pen);
        painter.drawRect(8, 10, 4, 12);
        painter.drawRect(18, 6, 4, 20);
    } else if (name == "Distribute H") {
        painter.drawLine(4, 4, 4, 28);
        painter.drawLine(28, 4, 28, 28);
        painter.drawRect(10, 12, 4, 8);
        painter.drawRect(18, 12, 4, 8);
    } else if (name == "Distribute V") {
        painter.drawLine(4, 4, 28, 4);
        painter.drawLine(4, 28, 28, 28);
        painter.drawRect(12, 10, 8, 4);
        painter.drawRect(12, 18, 8, 4);
    } else if (name == "Search") {
        painter.drawEllipse(8, 8, 12, 12);
        painter.drawLine(18, 18, 26, 26);
    } else if (name == "Global Label") {
        // Banner shape
        QPolygonF banner;
        banner << QPointF(6, 10) << QPointF(20, 10) << QPointF(26, 16) 
               << QPointF(20, 22) << QPointF(6, 22) << QPointF(2, 16);
        painter.drawPolygon(banner);
        painter.drawLine(6, 10, 6, 22);
    } else if (name == "Hierarchical Port") {
        // Port shape (trapezoid)
        QPolygonF port;
        port << QPointF(4, 12) << QPointF(24, 12) << QPointF(28, 16) 
             << QPointF(24, 20) << QPointF(4, 20);
        painter.drawPolygon(port);
        painter.drawLine(4, 12, 4, 20);
    } else if (name == "New") {
        QPolygonF filePoly;
        filePoly << QPointF(8, 4) << QPointF(18, 4) << QPointF(24, 10) << QPointF(24, 28) << QPointF(8, 28);
        painter.drawPolygon(filePoly);
        painter.drawLine(18, 4, 18, 10);
        painter.drawLine(18, 10, 24, 10);
        painter.setPen(QPen(ThemeManager::theme()->accentColor(), 3, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(12, 20, 20, 20);
        painter.drawLine(16, 16, 16, 24);
    } else if (name == "Open") {
        QPolygonF folder;
        folder << QPointF(4, 10) << QPointF(12, 10) << QPointF(15, 6) << QPointF(28, 6) << QPointF(28, 26) << QPointF(4, 26);
        painter.drawPolygon(folder);
        painter.drawLine(4, 14, 28, 14);
    } else if (name == "Save") {
        painter.drawRect(6, 6, 20, 20);
        painter.drawRect(10, 6, 12, 8);
        painter.drawRect(10, 18, 12, 8);
        painter.drawLine(12, 20, 20, 20);
        painter.drawLine(12, 22, 20, 22);
    } else if (name == "New Symbol") {
        painter.drawRect(8, 8, 16, 16);
        painter.setPen(QPen(ThemeManager::theme()->accentColor(), 2));
        painter.drawLine(4, 12, 8, 12); painter.drawLine(4, 20, 8, 20);
        painter.drawLine(24, 12, 28, 12); painter.drawLine(24, 20, 28, 20);
        painter.drawLine(16, 4, 16, 8); painter.drawLine(16, 24, 16, 28);
    } else if (name == "Netlist") {
        painter.drawRect(6, 4, 20, 24);
        painter.drawLine(10, 10, 22, 10);
        painter.drawLine(10, 15, 22, 15);
        painter.drawLine(10, 20, 18, 20);
    } else if (name == "Exit") {
        painter.drawArc(6, 6, 20, 20, -45 * 16, 270 * 16);
        painter.setPen(QPen(Qt::red, 2.5, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(16, 4, 16, 14);
    } else if (name == "About") {
        painter.drawEllipse(6, 6, 20, 20);
        painter.setFont(QFont("Times New Roman", 16, QFont::Bold | QFont::StyleItalic));
        painter.drawText(pixmap.rect(), Qt::AlignCenter, "i");
    } else if (name == "Panel Sidebar Left") {
        painter.drawRect(6, 8, 20, 16);
        painter.setBrush(color);
        painter.drawRect(6, 8, 6, 16);
    } else if (name == "Panel Bottom") {
        painter.drawRect(6, 8, 20, 16);
        painter.setBrush(color);
        painter.drawRect(6, 18, 20, 6);
    } else if (name == "Panel Sidebar Right") {
        painter.drawRect(6, 8, 20, 16);
        painter.setBrush(color);
        painter.drawRect(20, 8, 6, 16);
    } else if (name == "Breadcrumb Sep") {
        painter.drawLine(10, 8, 22, 16);
        painter.drawLine(22, 16, 10, 24);
    } else {
        // Fallback: draw first letter
        painter.setFont(QFont("Arial", 14, QFont::Bold));
        painter.drawText(pixmap.rect(), Qt::AlignCenter, name.left(1));
    }

    return QIcon(pixmap);
}

QIcon SchematicEditor::createItemPreviewIcon(SchematicItem* item) {
    if (!item) return QIcon();
    
    QRectF rect = item->boundingRect();
    if (rect.isEmpty()) return createComponentIcon(item->itemTypeName());
    
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Scale and center the item
    qreal margin = 8.0;
    qreal availableSize = 64.0 - 2.0 * margin;
    qreal scale = qMin(availableSize / rect.width(), availableSize / rect.height());
    
    // Safety check for infinite or zero scale
    if (scale <= 0 || scale > 1000) scale = 1.0;

    painter.translate(32, 32);
    painter.scale(scale, scale);
    painter.translate(-rect.center().x(), -rect.center().y());
    
    QStyleOptionGraphicsItem opt;
    item->paint(&painter, &opt, nullptr);
    
    return QIcon(pixmap);
}

void SchematicEditor::createToolBar() {
    // ─── Main Toolbar ────────────────────────────────────────────────────────
    QToolBar *mainToolbar = addToolBar("Main");
    mainToolbar->setObjectName("MainToolbar");
    mainToolbar->setIconSize(QSize(18, 18));
    mainToolbar->setMovable(false);
    mainToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    mainToolbar->setMinimumHeight(28);
    
    // 1. CREATE MODERN HAMBURGER MENU BUTTON
    QToolButton* menuBtn = new QToolButton(this);
    menuBtn->setText("Menu");
    // Simple 3-line hamburger icon
    QPixmap menuPix(24, 24);
    menuPix.fill(Qt::transparent);
    QPainter p(&menuPix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(ThemeManager::theme()->textColor(), 2, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(4, 6, 20, 6);
    p.drawLine(4, 12, 20, 12);
    p.drawLine(4, 18, 20, 18);
    menuBtn->setIcon(QIcon(menuPix));
    menuBtn->setPopupMode(QToolButton::InstantPopup);
    menuBtn->setStyleSheet(QString("QToolButton { background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 4px; } "
                           "QToolButton:hover { background-color: %1; } "
                           "QToolButton::menu-indicator { image: none; }")
                           .arg((ThemeManager::theme()->type() == PCBTheme::Light) ? "#e9ecef" : "#3c3c3c"));

    QMenu* mainAppMenu = new QMenu(menuBtn);
    mainAppMenu->setStyleSheet(ThemeManager::theme() ? ThemeManager::theme()->widgetStylesheet() : "");
    
    // File Menu
    QMenu* fileMenu = mainAppMenu->addMenu("&File");
    fileMenu->addAction(createComponentIcon("New"), "New Schematic", this, &SchematicEditor::onNewSchematic, QKeySequence::New);
    fileMenu->addAction(createComponentIcon("Open"), "Open Schematic...", this, &SchematicEditor::onOpenSchematic, QKeySequence::Open);
    fileMenu->addAction(createComponentIcon("Save"), "Save Schematic", this, &SchematicEditor::onSaveSchematic, QKeySequence::Save);
    fileMenu->addSeparator();
    fileMenu->addAction(createComponentIcon("New Symbol"), "Create New Symbol", this, &SchematicEditor::onOpenSymbolEditor);
    fileMenu->addAction(createComponentIcon("New Symbol"), "Create New Symbol from Schematic", this, &SchematicEditor::onCreateSymbolFromSchematic);
    fileMenu->addSeparator();
    QMenu* exportMenu = fileMenu->addMenu("Export");
    exportMenu->addAction("Export as PDF", this, &SchematicEditor::onExportPDF);
    exportMenu->addAction("Export as SVG", this, &SchematicEditor::onExportSVG);
    exportMenu->addAction("Export as Image", this, &SchematicEditor::onExportImage);
    exportMenu->addSeparator();
    exportMenu->addAction("Export AI JSON...", this, &SchematicEditor::onExportAISchematic);
    fileMenu->addSeparator();
    fileMenu->addAction(createComponentIcon("Exit"), "Exit", this, &QWidget::close, QKeySequence::Quit);

    // Edit Menu
    QMenu* editMenu = mainAppMenu->addMenu("&Edit");
    QAction* menuUndoAct = m_undoStack->createUndoAction(this);
    menuUndoAct->setShortcut(QKeySequence());  // Toolbar owns Ctrl+Z shortcut
    editMenu->addAction(menuUndoAct);
    QAction* menuRedoAct = m_undoStack->createRedoAction(this);
    menuRedoAct->setShortcut(QKeySequence());  // Toolbar owns Ctrl+Shift+Z shortcut
    editMenu->addAction(menuRedoAct);
    editMenu->addSeparator();
    editMenu->addAction(getThemeIcon(":/icons/tool_cut.svg"), "Cut", this, &SchematicEditor::onCut, QKeySequence::Cut);
    editMenu->addAction(getThemeIcon(":/icons/tool_copy.svg"), "Copy", this, &SchematicEditor::onCopy, QKeySequence::Copy);
    editMenu->addAction(getThemeIcon(":/icons/tool_paste.svg"), "Paste", this, &SchematicEditor::onPaste, QKeySequence::Paste);
    editMenu->addSeparator();
    QAction* deleteAction = editMenu->addAction(getThemeIcon(":/icons/tool_delete.svg"), "Delete", this, &SchematicEditor::onDelete);
    deleteAction->setShortcut(QKeySequence());
    editMenu->addAction("Select All", this, &SchematicEditor::onSelectAll, QKeySequence::SelectAll);
    editMenu->addSeparator();
    editMenu->addAction(getThemeIcon(":/icons/tool_search.svg"), "Find and Replace...", this, &SchematicEditor::onOpenFindReplace, QKeySequence::Find);

    // View Menu
    QMenu* viewMenu = mainAppMenu->addMenu("&View");
    viewMenu->addAction(getThemeIcon(":/icons/view_zoom_in.svg"), "Zoom In", this, &SchematicEditor::onZoomIn, QKeySequence::ZoomIn);
    viewMenu->addAction(getThemeIcon(":/icons/view_zoom_out.svg"), "Zoom Out", this, &SchematicEditor::onZoomOut, QKeySequence::ZoomOut);
    viewMenu->addAction(getThemeIcon(":/icons/view_fit.svg"), "Fit All", this, &SchematicEditor::onZoomFit, QKeySequence("F"));
    viewMenu->addAction(getThemeIcon(":/icons/view_zoom_components.svg"), "Zoom to Components", this, &SchematicEditor::onZoomAllComponents, QKeySequence("Alt+F"));
    viewMenu->addSeparator();
    viewMenu->addAction(getThemeIcon(":/icons/toolbar_new.png"), "Show Netlist", this, &SchematicEditor::onOpenNetlistEditor, QKeySequence("Ctrl+G"));
    m_showDetailedLogAction = viewMenu->addAction("Show Detailed Log", this, [this]() {
        if (m_simulationPanel) m_simulationPanel->showDetailedLog();
    }, QKeySequence("Ctrl+L"));
    if (m_showDetailedLogAction) {
        m_showDetailedLogAction->setEnabled(m_workspaceTabs->currentWidget() == m_simulationPanel);
    }

    viewMenu->addSeparator();

    QAction* crosshairAct = viewMenu->addAction("Show Crosshair");
    crosshairAct->setCheckable(true);
    crosshairAct->setChecked(m_view->isCrosshairEnabled());
    connect(crosshairAct, &QAction::toggled, this, [this](bool checked) {
        if (m_view) m_view->setShowCrosshair(checked);
    });

    QMenu* gridStyleMenu = viewMenu->addMenu("Grid Style");
    QActionGroup* gridStyleGroup = new QActionGroup(this);
    
    QAction* linesAct = gridStyleMenu->addAction("Lines");
    linesAct->setCheckable(true);
    linesAct->setActionGroup(gridStyleGroup);
    linesAct->setChecked(m_view->gridStyle() == SchematicView::Lines);
    connect(linesAct, &QAction::triggered, this, [this]() {
        if (m_view) m_view->setGridStyle(SchematicView::Lines);
    });

    QAction* pointsAct = gridStyleMenu->addAction("Points");
    pointsAct->setCheckable(true);
    pointsAct->setActionGroup(gridStyleGroup);
    pointsAct->setChecked(m_view->gridStyle() == SchematicView::Points);
    connect(pointsAct, &QAction::triggered, this, [this]() {
        if (m_view) m_view->setGridStyle(SchematicView::Points);
    });

    viewMenu->addSeparator();
    
    QMenu* panelsMenu = viewMenu->addMenu("Panels");
    auto addToggle = [&](QDockWidget* d, const QString& t) {
        if (!d) return;
        QAction* a = d->toggleViewAction();
        a->setText(t);
        panelsMenu->addAction(a);
    };
    addToggle(m_componentDock, "Component Library");
    addToggle(m_geminiDock, "Gemini Assistant");
    addToggle(m_hierarchyDock, "Sheet Hierarchy");
    addToggle(m_ercDock, "ERC Results");
    addToggle(m_oscilloscopeDock, "Analog Oscilloscope");
    addToggle(m_sourceControlDock, "Source Control");


    // Simulation Menu
    QMenu* simMenu = mainAppMenu->addMenu("&Simulation");
    m_runSimMenuAction = simMenu->addAction(createComponentIcon("Simulator"), "Run Simulation", this, &SchematicEditor::onRunSimulation, QKeySequence("F8"));
    m_stopSimMenuAction = simMenu->addAction(getThemeIcon(":/icons/tool_delete.svg"), "Stop Simulation", this, &SchematicEditor::onPauseSimulation, QKeySequence("Shift+F8"));
    simMenu->addSeparator();
    simMenu->addAction(getThemeIcon(":/icons/tool_gear.svg"), "Simulation Setup...", this, &SchematicEditor::onOpenSimulationSetup);

    // Tools Menu
    QMenu* toolsMenu = mainAppMenu->addMenu("&Tools");
    toolsMenu->addAction(createComponentIcon("Annotate"), "Annotate Components", this, &SchematicEditor::onAnnotate);
    toolsMenu->addAction(createComponentIcon("ERC"), "Run ERC Checker", this, &SchematicEditor::onRunERC, QKeySequence("F7"));
    toolsMenu->addAction("Configure ERC Rules...", this, &SchematicEditor::onOpenERCRulesConfig);
    toolsMenu->addAction("Clear ERC Exclusions", this, &SchematicEditor::onClearErcExclusions);
    toolsMenu->addAction("Bus Aliases...", this, &SchematicEditor::onOpenBusAliasesManager);
    toolsMenu->addAction(createComponentIcon("Netlist"), "Netlist Editor", this, &SchematicEditor::onOpenNetlistEditor);
    toolsMenu->addAction(getThemeIcon(":/icons/tool_gear.svg"), "SPICE Model Architect", this, &SchematicEditor::onOpenModelArchitect);
    toolsMenu->addSeparator();
    
    QAction* askModeAct = toolsMenu->addAction("Ask Co-Pilot (Mode)", this, [this](bool checked) {
        if (m_geminiPanel) {
            m_geminiPanel->setMode(checked ? "ask" : "schematic");
            if (m_geminiDock) m_geminiDock->show();
        }
    });
    askModeAct->setCheckable(true);
    askModeAct->setToolTip("Toggle AI mode to general circuit explanation/review.");
    
    toolsMenu->addSeparator();
    toolsMenu->addAction(getThemeIcon(":/icons/tool_search.svg"), "Command Palette", this, &SchematicEditor::onOpenCommandPalette, QKeySequence("Ctrl+K"));
    
    QAction* openCompAct = toolsMenu->addAction(getThemeIcon(":/icons/comp_ic.svg"), "Place Component...", this, &SchematicEditor::onOpenComponentBrowser, QKeySequence("A"));
    openCompAct->setToolTip("Open component browser and search (A)");

    // Settings (top-level, above Help)
    mainAppMenu->addSeparator();
    mainAppMenu->addAction(getThemeIcon(":/icons/tool_gear.svg"), "Settings...", this, &SchematicEditor::onSettings);

    QMenu* helpMenu = mainAppMenu->addMenu("&Help");
    helpMenu->addAction(createComponentIcon("About"), "About viospice", this, &SchematicEditor::onAbout);
    helpMenu->addAction("Help & Guides", this, &SchematicEditor::onShowHelp, QKeySequence::HelpContents);
    helpMenu->addAction("Developer Documentation", this, &SchematicEditor::onShowDeveloperHelp, QKeySequence("Ctrl+Shift+F1"));
    helpMenu->addSeparator();
    helpMenu->addAction("Project Health Audit...", this, &SchematicEditor::onProjectAudit);

    menuBtn->setMenu(mainAppMenu);
    mainToolbar->addWidget(menuBtn);

    mainToolbar->addSeparator();

    // Custom "New File" icon (File + Plus)
    QPixmap newFilePix(48, 48); // High DPI
    newFilePix.fill(Qt::transparent);
    {
        QPainter p(&newFilePix);
        p.setRenderHint(QPainter::Antialiasing);
        QColor iconColor = ThemeManager::theme()->textColor();
        p.setPen(QPen(iconColor, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        
        // Draw file shape
        QPolygonF filePoly;
        filePoly << QPointF(12, 8) << QPointF(28, 8) << QPointF(36, 16) << QPointF(36, 40) << QPointF(12, 40);
        p.drawPolygon(filePoly);
        p.drawLine(28, 8, 28, 16);
        p.drawLine(28, 16, 36, 16);

        // Draw plus mark
        p.setPen(QPen(ThemeManager::theme()->accentColor(), 4.5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(20, 28, 28, 28);
        p.drawLine(24, 24, 24, 32);
    }
    QToolButton* newFileBtn = new QToolButton(this);
    newFileBtn->setIcon(QIcon(newFilePix));
    newFileBtn->setToolTip("New Schematic (Ctrl+N)");
    connect(newFileBtn, &QToolButton::clicked, this, &SchematicEditor::onNewSchematic);
    mainToolbar->addWidget(newFileBtn);

    // Custom "Open File" icon (Folder)
    QPixmap openFilePix(48, 48);
    openFilePix.fill(Qt::transparent);
    {
        QPainter p(&openFilePix);
        p.setRenderHint(QPainter::Antialiasing);
        QColor iconColor = ThemeManager::theme()->textColor();
        p.setPen(QPen(iconColor, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        
        // Draw folder shape
        QPolygonF folderPoly;
        folderPoly << QPointF(8, 16) << QPointF(20, 16) << QPointF(24, 12) << QPointF(40, 12) << QPointF(40, 36) << QPointF(8, 36);
        p.drawPolygon(folderPoly);
        p.drawLine(8, 20, 40, 20);
    }
    QToolButton* openFileBtn = new QToolButton(this);
    openFileBtn->setIcon(QIcon(openFilePix));
    openFileBtn->setToolTip("Open Schematic (Ctrl+O)");
    connect(openFileBtn, &QToolButton::clicked, this, &SchematicEditor::onOpenSchematic);
    mainToolbar->addWidget(openFileBtn);

    mainToolbar->addSeparator();

    // Quick Undo button (Primary Ctrl+Z handler)
    QAction* undoAct = m_undoStack->createUndoAction(this);
    undoAct->setIcon(getThemeIcon(":/icons/undo.svg"));
    undoAct->setShortcut(QKeySequence::Undo);
    undoAct->setToolTip("Undo last action (Ctrl+Z)");
    mainToolbar->addAction(undoAct);

    mainToolbar->addSeparator();

    mainToolbar->setStyleSheet(
        QString("QToolBar#MainToolbar {"
        "  background-color: %1;"
        "  border-bottom: 1px solid %2;"
        "  padding: 2px 6px;"
        "  spacing: 2px;"
        "}"
        "QToolBar#MainToolbar QToolButton {"
        "  background: transparent;"
        "  border: 1px solid transparent;"
        "  border-radius: 4px;"
        "  padding: 2px;"
        "  color: %3;"
        "}"
        "QToolBar#MainToolbar QToolButton:hover {"
        "  background-color: %4;"
        "}"
        "QToolBar#MainToolbar QToolButton:checked, QToolBar#MainToolbar QToolButton:pressed {"
        "  background-color: %5;"
        "  color: white;"
        "}")
        .arg(ThemeManager::theme()->windowBackground().name())
        .arg(ThemeManager::theme()->panelBorder().name())
        .arg(ThemeManager::theme()->textColor().name())
        .arg((ThemeManager::theme()->type() == PCBTheme::Light) ? "#e9ecef" : "#3c3c3c")
        .arg(ThemeManager::theme()->accentColor().name())
    );

    // Command Palette / Search
    QAction* openPaletteAct = mainToolbar->addAction(getThemeIcon(":/icons/tool_search.svg"), "Search (Ctrl+K)");
    openPaletteAct->setShortcut(QKeySequence("Ctrl+K"));
    connect(openPaletteAct, &QAction::triggered, this, &SchematicEditor::onOpenCommandPalette);

    // Place Component Browser
    QAction* openBrowserAct = mainToolbar->addAction(getThemeIcon(":/icons/comp_ic.svg"), "Place Component (A)");
    openBrowserAct->setShortcut(QKeySequence("A"));
    connect(openBrowserAct, &QAction::triggered, this, &SchematicEditor::onOpenComponentBrowser);

    mainToolbar->addSeparator();

    // Zoom & View
    QAction* zoomInAct = mainToolbar->addAction(getThemeIcon(":/icons/view_zoom_in.svg"), "Zoom In");
    zoomInAct->setShortcut(QKeySequence::ZoomIn);
    connect(zoomInAct, &QAction::triggered, this, [this]() {
        m_view->setCurrentTool("Zoom Area");
        if (auto* z = qobject_cast<SchematicZoomAreaTool*>(m_view->currentTool())) {
            z->setDefaultMode(SchematicZoomAreaTool::ZoomMode::ZoomIn);
            m_view->setCursor(z->cursor());
            m_view->viewport()->setCursor(z->cursor());
        }
        statusBar()->showMessage("Zoom In tool active", 2000);
    });
    
    QAction* zoomOutAct = mainToolbar->addAction(getThemeIcon(":/icons/view_zoom_out.svg"), "Zoom Out");
    zoomOutAct->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOutAct, &QAction::triggered, this, [this]() {
        m_view->setCurrentTool("Zoom Area");
        if (auto* z = qobject_cast<SchematicZoomAreaTool*>(m_view->currentTool())) {
            z->setDefaultMode(SchematicZoomAreaTool::ZoomMode::ZoomOut);
            m_view->setCursor(z->cursor());
            m_view->viewport()->setCursor(z->cursor());
        }
        statusBar()->showMessage("Zoom Out tool active", 2000);
    });

    mainToolbar->addSeparator();

    // Breadcrumbs
    m_breadcrumbWidget = new QWidget();
    QHBoxLayout* bcLayout = new QHBoxLayout(m_breadcrumbWidget);
    bcLayout->setContentsMargins(5, 0, 5, 0);
    bcLayout->setSpacing(2);
    mainToolbar->addWidget(m_breadcrumbWidget);
    updateBreadcrumbs();
    
    mainToolbar->addSeparator();

    // Manipulation (Instant)
    QAction* rotateAct = mainToolbar->addAction(getThemeIcon(":/icons/tool_rotate.svg"), "Rotate");
    connect(rotateAct, &QAction::triggered, [this]() {
         if (m_scene && !m_scene->selectedItems().isEmpty()) {
             QList<SchematicItem*> items;
             for (auto* it : m_scene->selectedItems()) {
                 if (auto* si = dynamic_cast<SchematicItem*>(it)) items.append(si);
             }
             if (!items.isEmpty()) {
                 m_undoStack->push(new RotateItemCommand(m_scene, items, 90));
             }
         }
    });

    mainToolbar->addSeparator();

    QAction* ercAct = mainToolbar->addAction(createComponentIcon("ERC"), "Run ERC (F7)");
    connect(ercAct, &QAction::triggered, this, &SchematicEditor::onRunERC);

    mainToolbar->addSeparator();

    // Professional Simulation Control Group
    QAction* setupSimAct = mainToolbar->addAction(getThemeIcon(":/icons/tool_gear.svg"), "Simulation Setup...");
    setupSimAct->setToolTip("Configure Simulation Analysis (Transient, AC, DC)");
    connect(setupSimAct, &QAction::triggered, this, &SchematicEditor::onOpenSimulationSetup);

    mainToolbar->addSeparator();

    // Professional Simulation Control Group (LTspice style: Run/Pause toggle + Stop)
    QWidget* simGroup = new QWidget();
    QHBoxLayout* simLayout = new QHBoxLayout(simGroup);
    simLayout->setContentsMargins(0, 0, 0, 0);
    simLayout->setSpacing(4);

    m_runSimToolbarAction = new QAction(getThemeIcon(":/icons/tool_run.svg"), "Run Simulation (F8)", this);
    m_runSimToolbarAction->setShortcut(QKeySequence("F8"));
    m_runSimToolbarAction->setToolTip("Run Analysis (F8)");
    connect(m_runSimToolbarAction, &QAction::triggered, this, &SchematicEditor::onRunSimulation);
    
    QToolButton* mainBtn = new QToolButton();
    mainBtn->setDefaultAction(m_runSimToolbarAction);
    mainBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    mainBtn->setIconSize(QSize(24, 24));
    mainBtn->setStyleSheet("QToolButton { background-color: transparent; border: 1px solid transparent; border-radius: 4px; padding: 2px; } "
                          "QToolButton:hover { background-color: rgba(0, 0, 0, 0.05); border-color: #cbd5e1; }");
    simLayout->addWidget(mainBtn);

    m_stopSimToolbarAction = new QAction(getThemeIcon(":/icons/tool_stop.svg"), "Stop", this);
    m_stopSimToolbarAction->setShortcut(QKeySequence("Shift+F8"));
    connect(m_stopSimToolbarAction, &QAction::triggered, this, []() { SimManager::instance().stopAll(); });
    
    QToolButton* stopBtn = new QToolButton();
    stopBtn->setDefaultAction(m_stopSimToolbarAction);
    stopBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    stopBtn->setIconSize(QSize(24, 24));
    stopBtn->setStyleSheet("QToolButton { background-color: transparent; border: 1px solid transparent; border-radius: 4px; padding: 2px; } "
                           "QToolButton:hover { background-color: rgba(0, 0, 0, 0.05); border-color: #cbd5e1; }");
    simLayout->addWidget(stopBtn);
    
    // Store stop widget for visibility control
    m_simControlSubGroup = stopBtn;
    mainToolbar->addWidget(simGroup);

    // Initial state: hide Stop button
    m_simControlSubGroup->setVisible(false);

    updateSimulationUiState(m_simulationRunning);

    updateSimulationUiState(m_simulationRunning);

    // --- PANEL TOGGLES (VS CODE STYLE) ---
    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    mainToolbar->addWidget(spacer);

    auto addPanelToggle = [&](const QString& iconName, const QString& tooltip, auto slot) {
        QToolButton* btn = new QToolButton(this);
        btn->setIcon(createComponentIcon(iconName));
        btn->setToolTip(tooltip);
        btn->setCheckable(true);
        btn->setChecked(true); // Assuming visible by default
        connect(btn, &QToolButton::clicked, this, slot);
        mainToolbar->addWidget(btn);
        return btn;
    };

    addPanelToggle("Panel Sidebar Left", "Toggle Left Sidebar", &SchematicEditor::onToggleLeftSidebar);
    addPanelToggle("Panel Bottom", "Toggle Bottom Panel", &SchematicEditor::onToggleBottomPanel);
    addPanelToggle("Panel Sidebar Right", "Toggle Right Sidebar", &SchematicEditor::onToggleRightSidebar);

    mainToolbar->addSeparator();

    // ─── Property Bar (Dynamic Selection Properties) ─────────────────────────

    m_propertyBar = new QToolBar("Selection Properties", this);
    m_propertyBar->setObjectName("PropertyBar");
    m_propertyBar->setIconSize(QSize(18, 18));
    m_propertyBar->setMovable(false);
    m_propertyBar->setFixedHeight(40);
    m_propertyBar->setStyleSheet(
        "QToolBar#PropertyBar {"
        "  background: #1e1e20;"
        "  border-bottom: 1px solid #333336;"
        "  spacing: 15px;"
        "  padding-left: 15px;"
        "}"
        "QLabel { color: #3b82f6; font-weight: 600; font-size: 11px; text-transform: uppercase; }"
        "QLineEdit, QComboBox, QDoubleSpinBox {"
        "  background: #121214;"
        "  border: 1px solid #3f3f46;"
        "  border-radius: 4px;"
        "  padding: 3px 8px;"
        "  color: #ffffff;"
        "  min-width: 80px;"
        "}"
        "QLineEdit:focus { border-color: #3b82f6; }"
    );
    addToolBar(Qt::TopToolBarArea, m_propertyBar);
    
    // Default state
    updatePropertyBar();

    // ─── Schematic Tools Toolbar (Wiring & Placement) ────────────────────────
    QToolBar *schToolbar = addToolBar("Schematic Tools");
    schToolbar->setObjectName("SchematicToolbar");
    schToolbar->setIconSize(QSize(22, 22));
    schToolbar->setMovable(false);
    schToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    schToolbar->setOrientation(Qt::Vertical);
    addToolBar(Qt::LeftToolBarArea, schToolbar);
    schToolbar->setStyleSheet(
        "QToolBar#SchematicToolbar {"
        "  background-color: #1e1e1e;"
        "  border-right: 1px solid #3c3c3c;"
        "  padding: 6px 4px;"
        "  spacing: 2px;"
        "}"
        "QToolBar#SchematicToolbar QToolButton {"
        "  background: transparent;"
        "  border: 1px solid transparent;"
        "  border-radius: 4px;"
        "  padding: 5px;"
        "  margin: 1px 2px;"
        "  color: #cccccc;"
        "}"
        "QToolBar#SchematicToolbar QToolButton:hover {"
        "  border-color: #555;"
        "  background-color: #3c3c3c;"
        "}"
        "QToolBar#SchematicToolbar QToolButton:checked, QToolBar#SchematicToolbar QToolButton:pressed {"
        "  background-color: #094771;"
        "  border-color: #094771;"
        "  color: white;"
        "}"
        "QToolBar#SchematicToolbar::extension {"
        "  image: url(:/icons/chevron_down.svg);"
        "  background-color: #2d2d30;"
        "  border: 1px solid #555;"
        "  border-radius: 4px;"
        "  margin: 2px;"
        "}"
        "QToolBar#SchematicToolbar::extension:hover {"
        "  background-color: #3c3c3c;"
        "  border-color: #6a6a6a;"
        "}"
        "QToolBar#SchematicToolbar QToolButton#qt_toolbar_ext_button {"
        "  background-color: #2d2d30;"
        "  border: 1px solid #6a6a6a;"
        "  border-radius: 4px;"
        "  padding: 5px;"
        "  margin: 2px;"
        "}"
        "QToolBar#SchematicToolbar QToolButton#qt_toolbar_ext_button:hover {"
        "  background-color: #3c3c3c;"
        "  border-color: #8a8a8a;"
        "}"
        "QToolButton#qt_toolbar_ext_button {"
        "  background-color: #2d2d30;"
        "  border: 1px solid #6a6a6a;"
        "  border-radius: 4px;"
        "  min-height: 24px;"
        "  min-width: 24px;"
        "}"
        "QToolButton#qt_toolbar_ext_button:hover {"
        "  background-color: #3c3c3c;"
        "  border-color: #8a8a8a;"
        "}"
    );

    // ─── Layout Toolbar (Alignment & Distribution) ──────────────────────────
    QToolBar *layoutToolbar = addToolBar("Layout");
    layoutToolbar->setObjectName("LayoutToolbar");
    layoutToolbar->setIconSize(QSize(20, 20));
    layoutToolbar->setMovable(false);
    layoutToolbar->setOrientation(Qt::Vertical);
    addToolBar(Qt::LeftToolBarArea, layoutToolbar);
    layoutToolbar->setStyleSheet(schToolbar->styleSheet().replace("SchematicToolbar", "LayoutToolbar"));
    
    // Add grid size to Main Toolbar
    mainToolbar->addSeparator();
    mainToolbar->addWidget(new QLabel("  Grid: "));
    auto* gridCombo = new QComboBox();
    gridCombo->addItems({"1.0", "2.5", "5.0", "10.0", "25.0", "50.0"});
    gridCombo->setCurrentText(QString::number(m_view ? m_view->gridSize() : 10.0, 'f', 1));
    gridCombo->setFixedWidth(60);
    connect(gridCombo, &QComboBox::currentTextChanged, this, [this](const QString& text){
        if (m_view) {
            m_view->setGridSize(text.toDouble());
        }
    });
    mainToolbar->addWidget(gridCombo);

    QActionGroup* toolGroup = new QActionGroup(this);
    toolGroup->setExclusive(true);

    auto addSchTool = [&](const QString& toolName, const QString& label, const QString& iconName, const QString& shortcut = "") {
        QIcon icon = createComponentIcon(toolName);
        
        if (icon.isNull() && !iconName.isEmpty()) {
            if (iconName.startsWith("tool_") || iconName.startsWith("comp_")) {
                icon = getThemeIcon(QString(":/icons/%1.svg").arg(iconName));
            }
        }
        
        if (icon.isNull() && !iconName.isEmpty()) {
            icon = getThemeIcon(QString(":/icons/%1.svg").arg(iconName));
        }

        QAction* action = schToolbar->addAction(icon, label);
        action->setCheckable(true);
        action->setData(toolName);
        if (!shortcut.isEmpty()) {
            action->setShortcut(QKeySequence(shortcut));
            action->setToolTip(label + " (" + shortcut + ")");
        } else {
            action->setToolTip(label);
        }
        toolGroup->addAction(action);
        m_toolActions[toolName] = action;
        connect(action, &QAction::triggered, this, &SchematicEditor::onToolSelected);
        return action;
    };

    // Wiring Tools
    addSchTool("Select", "Select", "tool_select", "Esc");
    addSchTool("Probe", "Probe Signal", "tool_probe", "K");
    addSchTool("Voltage Probe", "Voltage Probe", "tool_voltage_probe", "Shift+K");
    addSchTool("Current Probe", "Current Probe", "tool_current_probe", "Alt+K");
    addSchTool("Power Probe", "Power Probe", "tool_power_probe", "Ctrl+Shift+P");
    addSchTool("Spice Directive", "SPICE Directive (.op)", "tool_spice_directive", "P");
    addSchTool("BV", "Arbitrary Behavioral Source", "comp_bv", "B");
    addSchTool("Scissors", "Delete (Scissors Tool)", "tool_scissors", "F5");
    addSchTool("Zoom Area", "Zoom to Area", "tool_zoom_area", "Z");
    addSchTool("Wire", "Place Wire", "tool_wire", "W");
    addSchTool("Bus", "Place Bus", "tool_bus", "Shift+B");
    addSchTool("Bus Entry", "Place Bus Entry", "tool_bus_entry", "");
    addSchTool("Net Label", "Place Net Label (Local)", "tool_net_label", "N");
    addSchTool("Global Label", "Place Global Label", "tool_global_label", "Ctrl+L");
    addSchTool("Hierarchical Port", "Place Hierarchical Port", "tool_hierarchical_port", "H");
    addSchTool("Sheet", "Place Hierarchical Sheet", "tool_sheet", "Shift+S");
    addSchTool("No-Connect", "No-Connect Flag", "tool_no_connect", "X");
    addSchTool("GND", "Place Power GND", "comp_gnd", "G");
    addSchTool("VCC", "Place Power VCC", "comp_vcc", "V");
    
    schToolbar->addSeparator();

    // Simulation Instruments
    addSchTool("Voltmeter (DC)", "Place Voltmeter (DC)", "tool_voltmeter");
    addSchTool("Voltmeter (AC)", "Place Voltmeter (AC)", "tool_voltmeter");
    addSchTool("Ammeter (DC)", "Place Ammeter (DC)", "tool_ammeter");
    addSchTool("Ammeter (AC)", "Place Ammeter (AC)", "tool_ammeter");
    addSchTool("Wattmeter", "Place Wattmeter", "tool_power_meter");
    addSchTool("Power Meter", "Place Power Meter", "tool_power_meter");
    addSchTool("Frequency Counter", "Place Frequency Counter", "tool_meter");
    addSchTool("Logic Probe", "Place Logic Probe", "tool_probe");
    addSchTool("Oscilloscope Instrument", "Place Oscilloscope", "tool_oscilloscope");

    schToolbar->addSeparator();

    // Fast Components
    addSchTool("Resistor", "Place Resistor", "comp_resistor", "R");
    addSchTool("Inductor", "Place Inductor", "comp_inductor", "L");
    addSchTool("Capacitor", "Place Capacitor", "comp_capacitor", "C");
    addSchTool("Diode", "Place Diode", "comp_diode", "D");
    addSchTool("Transistor", "Place Transistor", "comp_transistor", "Q");
    addSchTool("IC", "Place IC", "comp_ic", "U");
    addSchTool("RAM", "Place RAM Module", "comp_ram", "M");
    addSchTool("Gate_AND", "Place AND Gate", "comp_ic");
    addSchTool("Gate_OR", "Place OR Gate", "comp_ic");
    addSchTool("Gate_XOR", "Place XOR Gate", "comp_ic");
    addSchTool("Gate_NAND", "Place NAND Gate", "comp_ic");
    addSchTool("Gate_NOR", "Place NOR Gate", "comp_ic");
    addSchTool("Gate_NOT", "Place NOT Gate", "comp_ic");
    addSchTool("Switch", "Place Switch", "comp_switch", "S");

    // Set default tool
    if (m_toolActions.contains("Select")) {
        m_toolActions["Select"]->setChecked(true);
    }

    QToolButton* moreBtn = new QToolButton(this);
    moreBtn->setIcon(QIcon(":/icons/chevron_down.svg"));
    moreBtn->setToolTip("More...");
    moreBtn->setPopupMode(QToolButton::InstantPopup);
    QMenu* moreMenu = new QMenu(moreBtn);
    connect(moreMenu, &QMenu::aboutToShow, this, [this, moreMenu]() {
        moreMenu->clear();
        QStringList ordered = {
            "Select", "Probe", "Voltage Probe", "Current Probe", "Power Probe",
            "Zoom Area", "Wire", "Bus", "Bus Entry", "Net Label", "Global Label",
            "Hierarchical Port", "Sheet", "No-Connect", "GND", "VCC",
            "Voltmeter (DC)", "Voltmeter (AC)", "Ammeter (DC)", "Ammeter (AC)",
            "Wattmeter", "Power Meter", "Frequency Counter", "Logic Probe", "Oscilloscope Instrument",
            "Resistor", "Capacitor", "Diode", "Transistor", "IC", "RAM",
            "Gate_AND", "Gate_OR", "Gate_XOR", "Gate_NAND", "Gate_NOR", "Gate_NOT"
        };
        for (const QString& key : ordered) {
            QAction* a = m_toolActions.value(key, nullptr);
            if (!a) continue;
            moreMenu->addAction(a->icon(), a->text(), [a]() { a->trigger(); });
        }
    });
    moreBtn->setMenu(moreMenu);
    moreBtn->setStyleSheet(
        "QToolButton {"
        "  background-color: #2d2d30;"
        "  border: 1px solid #6a6a6a;"
        "  border-radius: 4px;"
        "  min-height: 24px;"
        "  min-width: 24px;"
        "  padding: 4px;"
        "}"
        "QToolButton:hover {"
        "  background-color: #3c3c3c;"
        "  border-color: #8a8a8a;"
        "}"
    );
    // Keep this button near the top so it stays visible on small screens.
    if (QAction* anchor = m_toolActions.value("Zoom Area", nullptr)) {
        schToolbar->insertWidget(anchor, moreBtn);
    } else {
        schToolbar->addWidget(moreBtn);
    }

    // Make overflow button explicit/visible when toolbar has hidden actions.
    QTimer::singleShot(0, this, [schToolbar]() {
        if (QToolButton* extBtn = schToolbar->findChild<QToolButton*>("qt_toolbar_ext_button")) {
            extBtn->setToolTip("More...");
            extBtn->setIcon(QIcon(":/icons/chevron_down.svg"));
            extBtn->setIconSize(QSize(12, 12));
            extBtn->setAutoRaise(false);
            extBtn->show();
            extBtn->raise();
        }
    });
}

void SchematicEditor::createDockWidgets() {
    // Configure Dock Corners so bottom dock doesn't stretch across the entire width
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    // === Component Library Dock ===
    m_componentDock = new QDockWidget("Components", this);
    m_componentDock->setObjectName("ComponentDock");
    m_componentDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    
    m_componentsPanel = new SchematicComponentsWidget(this);
    
    connect(m_componentsPanel, &SchematicComponentsWidget::toolSelected, [this](const QString& toolName) {
        if (!toolName.isEmpty()) {
            m_view->setCurrentTool(toolName);
            statusBar()->showMessage("" + toolName + " tool selected", 2000);
        }
    });

    connect(m_componentsPanel, &SchematicComponentsWidget::symbolCreated, [this](const QString& symbolName) {
        statusBar()->showMessage("Symbol '" + symbolName + "' created", 3000);
    });

    connect(m_componentsPanel, &SchematicComponentsWidget::symbolPlacementRequested, this, [this](const SymbolDefinition& symbol) {
        QPointF center = m_view->mapToScene(m_view->viewport()->rect().center());
        auto* item = new GenericComponentItem(symbol);
        item->setPos(center);
        m_undoStack->push(new AddItemCommand(m_scene, item));
        statusBar()->showMessage("Placed symbol: " + symbol.name(), 3000);
    });

    connect(m_componentsPanel, &SchematicComponentsWidget::modelAssignmentRequested, this, &SchematicEditor::onAssignModel);

    // Create a container for the dock to hold both components and filter
    QWidget* dockContainer = new QWidget(this);
    QVBoxLayout* dockLayout = new QVBoxLayout(dockContainer);
    dockLayout->setContentsMargins(0, 0, 0, 0);
    dockLayout->setSpacing(0);

    dockLayout->addWidget(m_componentsPanel, 1);

    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidget(dockContainer);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (ThemeManager::theme()) {
        scrollArea->setStyleSheet(QString("QScrollArea { background-color: %1; border: none; }").arg(ThemeManager::theme()->panelBackground().name()));
    }
    
    m_componentDock->setWidget(scrollArea);
    m_componentDock->setMinimumWidth(260);
    m_componentDock->setMaximumWidth(400);
    addDockWidget(Qt::LeftDockWidgetArea, m_componentDock);

    // === Project Explorer Dock ===
    m_projectExplorerDock = new QDockWidget("Explorer", this);
    m_projectExplorerDock->setObjectName("ProjectExplorerDock");
    m_projectExplorerDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    
    m_projectExplorer = new ProjectExplorerWidget(this);
    
    connect(m_projectExplorer, &ProjectExplorerWidget::fileDoubleClicked, this, &SchematicEditor::openFile);
    
    m_projectExplorerDock->setWidget(m_projectExplorer);
    addDockWidget(Qt::LeftDockWidgetArea, m_projectExplorerDock);
    tabifyDockWidget(m_componentDock, m_projectExplorerDock);
    m_projectExplorerDock->raise(); // Show explorer by default

    // === ERC Results Dock ===
    m_ercDock = new QDockWidget("ERC Results", this);
    m_ercDock->setObjectName("ERCDock");
    m_ercDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    
    QWidget* ercContainer = new QWidget(this);
    QVBoxLayout* ercLayout = new QVBoxLayout(ercContainer);
    ercLayout->setContentsMargins(4, 4, 4, 4);

    m_ercList = new QListWidget();
    if (ThemeManager::theme()) {
        m_ercList->setStyleSheet(QString("QListWidget { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; }")
            .arg(ThemeManager::theme()->panelBackground().name())
            .arg(ThemeManager::theme()->textColor().name())
            .arg(ThemeManager::theme()->panelBorder().name()));
    }
    ercLayout->addWidget(m_ercList);

    QHBoxLayout* ercControlLayout = new QHBoxLayout();
    QPushButton* ignoreSelectedBtn = new QPushButton("Ignore Selected");
    QPushButton* clearIgnoredBtn = new QPushButton("Clear Ignored");
    ercControlLayout->addWidget(ignoreSelectedBtn);
    ercControlLayout->addWidget(clearIgnoredBtn);
    ercControlLayout->addStretch();
    ercLayout->addLayout(ercControlLayout);

    QPushButton* aiFixBtn = new QPushButton("Ask Gemini for Fixes ✨");
    if (ThemeManager::theme() && ThemeManager::theme()->type() == PCBTheme::Light) {
        aiFixBtn->setStyleSheet("background-color: #f5f3ff; border: 1px solid #c084fc; color: #6b21a8; font-weight: bold; padding: 6px; border-radius: 6px;");
    } else {
        aiFixBtn->setStyleSheet("background-color: #3d2b3d; border: 1px solid #6b21a8; color: #f5d0fe; font-weight: bold; padding: 6px; border-radius: 6px;");
    }
    ercLayout->addWidget(aiFixBtn);

    QScrollArea* ercScroll = new QScrollArea(this);
    ercScroll->setWidget(ercContainer);
    ercScroll->setWidgetResizable(true);
    ercScroll->setFrameShape(QFrame::NoFrame);
    m_ercDock->setWidget(ercScroll);
    addDockWidget(Qt::RightDockWidgetArea, m_ercDock);
    m_ercDock->show(); // Show by default as ERC is required

    connect(aiFixBtn, &QPushButton::clicked, this, [this]() {
        if (!m_geminiPanel) return;
        
        QString violations;
        for (int i = 0; i < m_ercList->count(); ++i) {
            violations += "- " + m_ercList->item(i)->text() + "\n";
        }
        
        if (violations.isEmpty()) return;

        m_geminiDock->show();
        m_geminiDock->raise();
        m_geminiPanel->askPrompt("I have the following ERC (Electrical Rules Check) violations in my schematic:\n\n" + violations + "\nCan you explain why these are happening and suggest FluxScript or schematic changes to fix them?");
    });

    m_ercList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_ercList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QListWidgetItem* item = m_ercList->itemAt(pos);
        if (!item) return;

        QMenu menu;
        menu.addAction("Ignore This Violation", this, &SchematicEditor::onIgnoreSelectedErc);
        menu.addAction("Clear All Exclusions", this, &SchematicEditor::onClearErcExclusions);
        menu.addSeparator();
        menu.addAction("Copy Message", [item]() {
            QApplication::clipboard()->setText(item->text());
        });
        menu.exec(m_ercList->mapToGlobal(pos));
    });

    connect(m_ercList, &QListWidget::itemDoubleClicked, this, &SchematicEditor::onIssueItemDoubleClicked);
    connect(ignoreSelectedBtn, &QPushButton::clicked, this, &SchematicEditor::onIgnoreSelectedErc);
    connect(clearIgnoredBtn, &QPushButton::clicked, this, &SchematicEditor::onClearErcExclusions);
    
    // === Gemini AI Dock ===
    m_geminiDock = new QDockWidget("viospice AI Co-Pilot", this);
    m_geminiDock->setObjectName("GeminiDock");
    m_geminiDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    m_geminiPanel = new GeminiPanel(m_scene, this);
    m_geminiPanel->setNetManager(m_netManager);
    m_geminiPanel->setUndoStack(m_undoStack);
    connect(m_geminiPanel, &GeminiPanel::itemsHighlighted, this, &SchematicEditor::onItemsHighlighted);
    connect(m_geminiPanel, &GeminiPanel::snippetGenerated, this, &SchematicEditor::onSnippetGenerated);
    
    m_geminiPanel->setContextProvider([this]() {
        QJsonObject ctx = SchematicFileIO::serializeSceneToJson(m_scene);
        
        // Add ERC Violations to context
        QJsonArray ercArray;
        for (const auto& v : getErcViolations()) {
            QJsonObject vObj;
            vObj["severity"] = v.severity == ERCViolation::Critical ? "Critical" : (v.severity == ERCViolation::Error ? "Error" : "Warning");
            vObj["message"] = v.message;
            if (v.item) vObj["item"] = v.item->reference();
            if (!v.netName.isEmpty()) vObj["net"] = v.netName;
            ercArray.append(vObj);
        }
        ctx["erc_violations"] = ercArray;

        return QString::fromUtf8(QJsonDocument(ctx).toJson(QJsonDocument::Compact));
    });
    
    QScrollArea* geminiScroll = new QScrollArea(this);
    geminiScroll->setWidget(m_geminiPanel);
    geminiScroll->setWidgetResizable(true);
    geminiScroll->setFrameShape(QFrame::NoFrame);
    geminiScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (ThemeManager::theme()) {
        geminiScroll->setStyleSheet(QString("QScrollArea { background-color: %1; border: none; }").arg(ThemeManager::theme()->panelBackground().name()));
    }
    m_geminiDock->setWidget(geminiScroll);
    addDockWidget(Qt::LeftDockWidgetArea, m_geminiDock);

    // Tabify docks so they don't crush each other
    tabifyDockWidget(m_componentDock, m_geminiDock);
    m_componentDock->raise();

    // === Sheet Hierarchy Dock ===
    m_hierarchyDock = new QDockWidget("Sheet Hierarchy", this);
    m_hierarchyDock->setObjectName("HierarchyDock");
    m_hierarchyDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);

    QWidget* hierarchyContainer = new QWidget(this);
    QVBoxLayout* hierarchyLayout = new QVBoxLayout(hierarchyContainer);
    hierarchyLayout->setContentsMargins(0, 0, 0, 0);
    hierarchyLayout->setSpacing(0);

    // Toolbar row at top
    QWidget* hierHeader = new QWidget();
    QHBoxLayout* hierHeaderLayout = new QHBoxLayout(hierHeader);
    hierHeaderLayout->setContentsMargins(6, 4, 6, 4);
    hierHeaderLayout->setSpacing(4);

    QLabel* hierTitle = new QLabel("Sheets");
    if (ThemeManager::theme()) {
        hierTitle->setStyleSheet(QString("color: %1; font-size: 11px; font-weight: bold;").arg(ThemeManager::theme()->textSecondary().name()));
    }
    hierHeaderLayout->addWidget(hierTitle, 1);

    QToolButton* hierRefreshBtn = new QToolButton();
    hierRefreshBtn->setIcon(getThemeIcon(":/icons/view_fit.svg"));
    hierRefreshBtn->setToolTip("Refresh hierarchy");
    hierRefreshBtn->setIconSize(QSize(14, 14));
    hierRefreshBtn->setStyleSheet("QToolButton { background: transparent; border: none; }");
    hierHeaderLayout->addWidget(hierRefreshBtn);

    QToolButton* hierCollapseBtn = new QToolButton();
    hierCollapseBtn->setToolTip("Collapse all");
    hierCollapseBtn->setText("−");
    hierCollapseBtn->setStyleSheet(QString("QToolButton { background: transparent; border: none; color: %1; font-size: 14px; }")
        .arg(ThemeManager::theme() ? ThemeManager::theme()->textSecondary().name() : "#888"));
    hierHeaderLayout->addWidget(hierCollapseBtn);

    hierarchyLayout->addWidget(hierHeader);

    // Separator line
    QFrame* hierSep = new QFrame();
    hierSep->setFrameShape(QFrame::HLine);
    if (ThemeManager::theme()) {
        hierSep->setStyleSheet(QString("background-color: %1;").arg(ThemeManager::theme()->panelBorder().name()));
    }
    hierSep->setFixedHeight(1);
    hierarchyLayout->addWidget(hierSep);

    // Tree widget
    m_hierarchyTree = new QTreeWidget();
    m_hierarchyTree->setHeaderHidden(true);
    m_hierarchyTree->setRootIsDecorated(true);
    m_hierarchyTree->setAnimated(true);
    m_hierarchyTree->setIndentation(16);
    m_hierarchyTree->setIconSize(QSize(16, 16));
    m_hierarchyTree->setUniformRowHeights(true);
    m_hierarchyTree->setContextMenuPolicy(Qt::CustomContextMenu);
    
    if (ThemeManager::theme()) {
        QString bg = ThemeManager::theme()->panelBackground().name();
        QString fg = ThemeManager::theme()->textColor().name();
        QString selBg = ThemeManager::theme()->accentColor().name();
        QString hoverBg = (ThemeManager::theme()->type() == PCBTheme::Light) ? "#f1f5f9" : "#2d2d30";
        
        m_hierarchyTree->setStyleSheet(QString(R"(
            QTreeWidget {
                background-color: %1;
                color: %2;
                border: none;
                font-size: 12px;
                outline: none;
            }
            QTreeWidget::item {
                padding: 4px 6px;
                border-radius: 4px;
            }
            QTreeWidget::item:selected {
                background-color: %3;
                color: #ffffff;
            }
            QTreeWidget::item:hover:!selected {
                background-color: %4;
            }
            QTreeWidget::branch:has-children:!has-siblings:closed,
            QTreeWidget::branch:closed:has-children:has-siblings {
                image: url(:/icons/chevron_right.svg);
            }
            QTreeWidget::branch:open:has-children:!has-siblings,
            QTreeWidget::branch:open:has-children:has-siblings {
                image: url(:/icons/chevron_down.svg);
            }
        )").arg(bg, fg, selBg, hoverBg));
    }
    hierarchyLayout->addWidget(m_hierarchyTree, 1);

    m_hierarchyPanel = new SchematicHierarchyPanel(this);
    m_hierarchyDock->setWidget(m_hierarchyPanel);
    m_hierarchyDock->setMinimumWidth(200);
    addDockWidget(Qt::LeftDockWidgetArea, m_hierarchyDock);

    // -- Connect signals --
    connect(m_hierarchyTree, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem* item, int) {
        if (item) {
            QString fileName = item->data(0, Qt::UserRole).toString();
            if (!fileName.isEmpty()) openFile(fileName);
        }
    });

    connect(m_hierarchyTree, &QTreeWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        QTreeWidgetItem* item = m_hierarchyTree->itemAt(pos);
        if (!item) return;
        
        QMenu menu(this);
        menu.addAction("Open Sheet", [this, item]() {
            QString fileName = item->data(0, Qt::UserRole).toString();
            if (!fileName.isEmpty()) openFile(fileName);
        });
        
        menu.exec(m_hierarchyTree->mapToGlobal(pos));
    });

    connect(hierRefreshBtn, &QToolButton::clicked, this, &SchematicEditor::refreshHierarchyPanel);
    connect(hierCollapseBtn, &QToolButton::clicked, m_hierarchyTree, &QTreeWidget::collapseAll);

    // === Simulation Window ===

    connect(m_geminiPanel, &GeminiPanel::fluxScriptGenerated, this, [this](const QString& code) {
        if (m_scriptPanel) {
            m_scriptPanel->setScript(code);
            onOpenFluxScript(); // Show the script dock
            statusBar()->showMessage("AI generated FluxScript is ready in the editor!", 5000);
        }
    }, Qt::QueuedConnection);

    // === FluxScript Dock ===
    m_scriptDock = new QDockWidget("FluxScript Editor", this);
    m_scriptDock->setObjectName("FluxScriptDock");
    m_scriptPanel = new Flux::ScriptPanel(m_scene, m_netManager, this);
    
    QScrollArea* scriptScroll = new QScrollArea(this);
    scriptScroll->setWidget(m_scriptPanel);
    scriptScroll->setWidgetResizable(true);
    scriptScroll->setFrameShape(QFrame::NoFrame);
    if (ThemeManager::theme()) {
        scriptScroll->setStyleSheet(QString("QScrollArea { background-color: %1; border: none; }").arg(ThemeManager::theme()->panelBackground().name()));
    }
    
    m_scriptDock->setWidget(scriptScroll);
    addDockWidget(Qt::LeftDockWidgetArea, m_scriptDock);
    m_scriptDock->hide();
    m_componentDock->raise(); // Show components by default

    // === Source Control Dock ===
    m_sourceControlDock = new QDockWidget("Source Control", this);
    m_sourceControlDock->setObjectName("SourceControlDock");
    m_sourceControlDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    m_sourceControlPanel = new SourceControlPanel(this);
    m_sourceControlDock->setWidget(m_sourceControlPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_sourceControlDock);
    tabifyDockWidget(m_ercDock, m_sourceControlDock);
    m_sourceControlDock->raise();

    if (ThemeManager::theme()) {
        m_sourceControlDock->setStyleSheet(QString(
            "QDockWidget { border: none; }"
            "QDockWidget::title { background: %1; color: %2; padding: 6px; border-bottom: 1px solid %3; font-weight: bold; }"
        ).arg(ThemeManager::theme()->panelBackground().name(),
              ThemeManager::theme()->textColor().name(),
              ThemeManager::theme()->panelBorder().name()));
    }


    // Stack the left docks
    tabifyDockWidget(m_componentDock, m_hierarchyDock);
    tabifyDockWidget(m_hierarchyDock, m_geminiDock);
    tabifyDockWidget(m_geminiDock, m_scriptDock);
    m_componentDock->raise();

    // === Oscilloscope Dock ===
    m_oscilloscopeDock = new QDockWidget("Analog Oscilloscope", this);
    m_oscilloscopeDock->setObjectName("AnalogOscilloscopeDock");
    m_oscilloscopeDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    m_oscilloscopeDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);

    if (ThemeManager::theme()) {        m_oscilloscopeDock->setStyleSheet(QString(
            "QDockWidget { border: none; }"
            "QDockWidget::title { background: %1; color: %2; padding: 6px; border-bottom: 1px solid %3; font-weight: bold; }"
        ).arg(ThemeManager::theme()->panelBackground().name(), 
              ThemeManager::theme()->textColor().name(),
              ThemeManager::theme()->panelBorder().name()));
    }
    
    m_oscilloscopeDock->setMinimumHeight(300);
    m_oscilloscopeDock->setMinimumWidth(0);
    m_oscilloscopeDock->hide();
    addDockWidget(Qt::BottomDockWidgetArea, m_oscilloscopeDock);

    connect(m_oscilloscopeDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (m_view) {
            m_view->setProbingEnabled(visible);
        }
    });

    // Initialize Simulation Panel (but don't add to tabs yet) so Oscilloscope is available
    if (m_scene && m_netManager) {
        m_simulationPanel = new SimulationPanel(m_scene, m_netManager, m_projectDir, this);
        
        SimulationPanel::AnalysisConfig pCfg;
        pCfg.type = m_simConfig.type;
        pCfg.stop = m_simConfig.stop;
        pCfg.step = m_simConfig.step;
        pCfg.fStart = m_simConfig.fStart;
        pCfg.fStop = m_simConfig.fStop;
        pCfg.pts = m_simConfig.pts;
        m_simulationPanel->setAnalysisConfig(pCfg);

        connect(m_simulationPanel, &SimulationPanel::resultsReady, this, &SchematicEditor::onSimulationResultsReady);
        connect(m_simulationPanel, &SimulationPanel::timeSnapshotReady, this, &SchematicEditor::onTimeTravelSnapshot);
        connect(m_simulationPanel, &SimulationPanel::probeRequested, this, [this]() {
            m_view->setCurrentTool("Probe");
            ensureProbeToolConnected();
            statusBar()->showMessage("Click on a net or pin to probe signal", 5000);
        });
        connect(m_simulationPanel, &SimulationPanel::placementToolRequested, this, [this](const QString& toolName) {
            if (!m_view) return;
            m_view->setCurrentTool(toolName);
            if (toolName == "Probe" ||
                toolName == "Oscilloscope Instrument" ||
                toolName == "Voltmeter (DC)" ||
                toolName == "Voltmeter (AC)" ||
                toolName == "Ammeter (DC)" ||
                toolName == "Ammeter (AC)" ||
                toolName == "Wattmeter" ||
                toolName == "Power Meter" ||
                toolName == "Frequency Counter" ||
                toolName == "Logic Probe") {
                ensureProbeToolConnected();
            }
            statusBar()->showMessage(QString("Placement tool active: %1").arg(toolName), 4000);
        });
        connect(m_simulationPanel, &SimulationPanel::simulationTargetRequested, this,
                [this](const QString& type, const QString& id) {
            navigateToSimulationTarget(type, id);
            if (m_view) m_view->setFocus();
            statusBar()->showMessage(QString("Navigated to %1: %2").arg(type, id), 4000);
        });
        connect(m_simulationPanel, &SimulationPanel::overlayVisibilityChanged,
                this, &SchematicEditor::onOverlayVisibilityChanged, Qt::UniqueConnection);
        connect(m_simulationPanel, &SimulationPanel::clearOverlaysRequested,
                this, &SchematicEditor::onClearSimulationOverlays, Qt::UniqueConnection);

        m_oscilloscopeDock->setWidget(m_simulationPanel->getOscilloscopeContainer());
    }
}

void SchematicEditor::createStatusBar() {
    statusBar()->setStyleSheet(
        "QStatusBar { background: #f5f5f5; border-top: 1px solid #d1d5db; }"
        "QStatusBar QLabel { padding: 0 8px; color: #111827; }"
        "QStatusBar::item { border: none; }"
    );

    // Coordinate display with icon
    m_coordLabel = new QLabel("X: 0.00  Y: 0.00 mm");
    m_coordLabel->setMinimumWidth(200);
    statusBar()->addWidget(m_coordLabel);

    // Unit Switcher
    auto* unitCombo = new QComboBox();
    unitCombo->addItems({"mm", "mil", "in"});
    unitCombo->setStyleSheet(
        "QComboBox { background: #ffffff; color: #111827; border: 1px solid #d1d5db; font-size: 10px; height: 18px; margin: 4px 0; }"
        "QComboBox QAbstractItemView { background: #ffffff; color: #111827; selection-background-color: #e5e7eb; selection-color: #111827; }"
    );
    connect(unitCombo, &QComboBox::currentTextChanged, this, [this](const QString& unit) {
        m_view->setProperty("currentUnit", unit);
        // Refresh coords if mouse is over view
        QPointF scenePos = m_view->mapToScene(m_view->mapFromGlobal(QCursor::pos()));
        updateCoordinates(scenePos);
    });
    statusBar()->addWidget(unitCombo);

    // Separator
    QFrame* sep1 = new QFrame();
    sep1->setFrameShape(QFrame::VLine);
    sep1->setFixedWidth(1);
    sep1->setStyleSheet("QFrame { background: #d1d5db; margin: 3px 6px; }");
    statusBar()->addWidget(sep1);

    // Grid display
    m_gridLabel = new QLabel("Grid: 10mil");
    statusBar()->addPermanentWidget(m_gridLabel);

    // Separator
    QFrame* sep2 = new QFrame();
    sep2->setFrameShape(QFrame::VLine);
    sep2->setFixedWidth(1);
    sep2->setStyleSheet("QFrame { background: #d1d5db; margin: 3px 6px; }");
    statusBar()->addPermanentWidget(sep2);

    // Page size indicator
    QLabel* pageLabel = new QLabel("Page: " + m_currentPageSize);
    statusBar()->addPermanentWidget(pageLabel);

    // Separator
    QFrame* sep3 = new QFrame();
    sep3->setFrameShape(QFrame::VLine);
    sep3->setFixedWidth(1);
    sep3->setStyleSheet("QFrame { background: #d1d5db; margin: 3px 6px; }");
    statusBar()->addPermanentWidget(sep3);

    // Layer indicator
    m_layerLabel = new QLabel("Layer: Schematic");
    statusBar()->addPermanentWidget(m_layerLabel);

    // Theme Switcher
    QPushButton* themeBtn = new QPushButton("Theme");
    themeBtn->setFlat(true);
    themeBtn->setCursor(Qt::PointingHandCursor);
    themeBtn->setStyleSheet("QPushButton { color: #374151; font-weight: 600; border: none; padding: 0 6px; } QPushButton:hover { color: #111827; }");
    connect(themeBtn, &QPushButton::clicked, this, []() {
        auto& tm = ThemeManager::instance();
        if (tm.currentTheme()->type() == PCBTheme::Engineering) tm.setTheme(PCBTheme::Dark);
        else if (tm.currentTheme()->type() == PCBTheme::Dark) tm.setTheme(PCBTheme::Light);
        else tm.setTheme(PCBTheme::Engineering);
    });
    statusBar()->addPermanentWidget(themeBtn);

    // Ready message
    statusBar()->showMessage("Ready - Select a component or tool to begin", 5000);
}

void SchematicEditor::createDrawingToolbar() {
    QToolBar *drawToolbar = addToolBar("Drawing Tools");
    addToolBar(Qt::RightToolBarArea, drawToolbar);
    drawToolbar->setObjectName("DrawingToolbar");
    drawToolbar->setIconSize(QSize(24, 24));
    drawToolbar->setMovable(false);
    drawToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    drawToolbar->setOrientation(Qt::Vertical);

    // Attempt to join the existing exclusive group
    QActionGroup* group = nullptr;
    if (m_toolActions.contains("Select")) {
        group = m_toolActions["Select"]->actionGroup();
    }
    if (!group) {
        group = new QActionGroup(this);
        group->setExclusive(true);
    }

    auto addTool = [&](const QString& toolName, const QString& label) {
        QIcon icon = createComponentIcon(toolName); // Use our programmatic icons
        
        QAction* action = drawToolbar->addAction(icon, label);
        action->setCheckable(true);
        action->setData(toolName);
        action->setToolTip(label);
        
        group->addAction(action);
        m_toolActions[toolName] = action;
        
        connect(action, &QAction::triggered, this, &SchematicEditor::onToolSelected);
        return action;
    };

    addTool("Rectangle", "Draw Rectangle");
    addTool("Circle", "Draw Circle");
    addTool("Line", "Draw Line");
    addTool("Polygon", "Draw Polygon");
    addTool("Bezier", "Draw Bezier Curve");
    addTool("Text", "Add Text");
    addTool("Net Label", "Place Net Label (Local)");
    addTool("Scissors", "Scissors Items (F5)")->setShortcut(QKeySequence("F5"));
    addTool("Spice Directive", "SPICE Directive (S)")->setShortcut(QKeySequence("S"));

    drawToolbar->addSeparator();

    // ─── Manipulation Tools (Not exclusive group, these are actions) ───────
    auto addManipAction = [&](const QString& iconName, const QString& tooltip, auto slot) {
        QAction* action = drawToolbar->addAction(createComponentIcon(iconName), tooltip);
        connect(action, &QAction::triggered, this, slot);
        return action;
    };

    addManipAction("Rotate CW", "Rotate Clockwise (90°) (Ctrl+R)", &SchematicEditor::onRotateCW)->setShortcut(QKeySequence("Ctrl+R"));
    addManipAction("Rotate CCW", "Rotate Counter-Clockwise (90°) (Ctrl+Shift+R)", &SchematicEditor::onRotateCCW)->setShortcut(QKeySequence("Ctrl+Shift+R"));
    
    drawToolbar->addSeparator();
    
    addManipAction("Flip H", "Flip Horizontal (Ctrl+Shift+E)", &SchematicEditor::onFlipHorizontal)->setShortcut(QKeySequence("Ctrl+Shift+E"));
    addManipAction("Flip V", "Flip Vertical (Ctrl+E)", &SchematicEditor::onFlipVertical)->setShortcut(QKeySequence("Ctrl+E"));

    drawToolbar->addSeparator();

    addManipAction("Front", "Bring to Front", &SchematicEditor::onBringToFront);
    addManipAction("Back", "Send to Back", &SchematicEditor::onSendToBack);

    drawToolbar->addSeparator();

    addManipAction("Align Left", "Align Left", &SchematicEditor::onAlignLeft);
    addManipAction("Align Right", "Align Right", &SchematicEditor::onAlignRight);
    addManipAction("Align Top", "Align Top", &SchematicEditor::onAlignTop);
    addManipAction("Align Bottom", "Align Bottom", &SchematicEditor::onAlignBottom);
    addManipAction("Center X", "Align Center X", &SchematicEditor::onAlignCenterX);
    addManipAction("Center Y", "Align Center Y", &SchematicEditor::onAlignCenterY);
}

#include "../items/oscilloscope_item.h"

void SchematicEditor::updateSimulationUiState(bool running, const QString& statusMessage) {
    if (m_view) m_view->setSimulationRunning(running);
    m_simulationRunning = running;
    
    // Primary Action (Run/Pause toggle)
    if (m_runSimToolbarAction) {
        if (running && !m_simPaused) {
            m_runSimToolbarAction->setIcon(getThemeIcon(":/icons/tool_pause.svg"));
            m_runSimToolbarAction->setText("Pause Simulation");
            m_runSimToolbarAction->setToolTip("Pause current simulation");
        } else if (m_simPaused) {
            m_runSimToolbarAction->setIcon(getThemeIcon(":/icons/tool_run.svg"));
            m_runSimToolbarAction->setText("Resume Simulation");
            m_runSimToolbarAction->setToolTip("Resume current simulation");
        } else {
            m_runSimToolbarAction->setIcon(getThemeIcon(":/icons/tool_run.svg"));
            m_runSimToolbarAction->setText("Run Simulation (F8)");
            m_runSimToolbarAction->setToolTip("Run Analysis (F8)");
        }
        // Button remains enabled so it can toggle between Run and Pause
        m_runSimToolbarAction->setEnabled(true);
    }

    if (m_runSimMenuAction) m_runSimMenuAction->setEnabled(!running || m_simPaused);
    if (m_stopSimMenuAction) m_stopSimMenuAction->setEnabled(running);
    
    // Stop button visibility
    if (m_simControlSubGroup) m_simControlSubGroup->setVisible(running);

    if (!statusMessage.isEmpty()) {
        statusBar()->showMessage(statusMessage, running ? 0 : 3000);
    }
}

void SchematicEditor::onSimulationPaused(bool paused) {
    m_simPaused = paused;
    updateSimulationUiState(m_simulationRunning, paused ? "Simulation paused." : "Simulation resumed.");
}

void SchematicEditor::connectSimulationSignals() {
    auto& sim = SimManager::instance();

    connect(&sim, &SimManager::simulationStarted, this, [this]() {
        m_simulationRunning = true;
        updateSimulationUiState(true, "Simulation running...");
        
        // Find and switch to Simulation Tab
        for (int i = 0; i < m_workspaceTabs->count(); ++i) {
            if (qobject_cast<SimulationPanel*>(m_workspaceTabs->widget(i))) {
                m_workspaceTabs->setCurrentIndex(i);
                break;
            }
        }
    });

    connect(&sim, &SimManager::simulationFinished, this, [this](const SimResults&) {
        if (m_simConfig.type != SimAnalysisType::RealTime) {
            m_simulationRunning = false;
            updateSimulationUiState(false, "Simulation finished.");
        }
    });

    connect(&sim, &SimManager::simulationPaused, this, &SchematicEditor::onSimulationPaused);

    connect(&sim, &SimManager::errorOccurred, this, [this](const QString& message) {
        m_simulationRunning = false;
        updateSimulationUiState(false, "Simulation error.");
        statusBar()->showMessage(QString("Simulation error: %1").arg(message), 5000);
        appendSimulationIssue(message);
    });
}

void SchematicEditor::appendSimulationIssue(const QString& message) {
    if (!m_ercList) return;

    const auto target = SimSchematicBridge::extractDiagnosticTarget(message);
    QListWidgetItem* item = new QListWidgetItem(QString("[SIM] %1").arg(message));
    item->setData(Qt::UserRole + 1, "simulation");
    item->setData(Qt::UserRole + 2, static_cast<int>(target.type));
    item->setData(Qt::UserRole + 3, target.id);
    item->setForeground(QColor("#f59e0b"));
    item->setFont(QFont("Inter", 9, QFont::Bold));
    m_ercList->addItem(item);

    if (m_ercDock) {
        m_ercDock->show();
        m_ercDock->raise();
    }
}

void SchematicEditor::navigateToSimulationTarget(const QString& targetType, const QString& targetId) {
    if (targetId.trimmed().isEmpty()) return;

    if (targetType == "component") {
        if (!findAndSelectInScene(m_scene, targetId)) {
            navigateAndSelectHierarchical(targetId);
        }
        return;
    }

    if (targetType == "net") {
        if (m_netManager) {
            const auto conns = m_netManager->getConnections(targetId);
            if (!conns.isEmpty()) {
                m_view->centerOn(conns.first().connectionPoint);
            }
        }
    }
}

void SchematicEditor::onIssueItemDoubleClicked(QListWidgetItem* item) {
    if (!item || !m_view) return;

    if (item->data(Qt::UserRole + 1).toString() == "simulation") {
        const int t = item->data(Qt::UserRole + 2).toInt();
        const QString id = item->data(Qt::UserRole + 3).toString();
        if (t == static_cast<int>(SimSchematicBridge::DiagnosticTarget::Type::Component)) {
            navigateToSimulationTarget("component", id);
            return;
        }
        if (t == static_cast<int>(SimSchematicBridge::DiagnosticTarget::Type::Net)) {
            navigateToSimulationTarget("net", id);
            return;
        }
    }

    QPointF pos = item->data(Qt::UserRole).toPointF();
    if (!pos.isNull()) {
        m_view->centerOn(pos);
    }
}

void SchematicEditor::onOpenSimulationSetup() {
    SimulationSetupDialog dlg(this);
    dlg.setConfig(m_simConfig);
    if (dlg.exec() == QDialog::Accepted) {
        m_simConfig = dlg.getConfig();

        if (m_simulationPanel) {
            SimulationPanel::AnalysisConfig pCfg;
            pCfg.type = m_simConfig.type;
            pCfg.stop = m_simConfig.stop;
            pCfg.step = m_simConfig.step;
            pCfg.fStart = m_simConfig.fStart;
            pCfg.fStop = m_simConfig.fStop;
            pCfg.pts = m_simConfig.pts;
            m_simulationPanel->setAnalysisConfig(pCfg);
            
            // Sync schematic directive with the command text from dialog
            if (!m_simConfig.commandText.isEmpty()) {
                m_simulationPanel->updateSchematicDirectiveFromCommand(m_simConfig.commandText);
            }
        }

        statusBar()->showMessage("Simulation parameters updated.", 3000);
    }
}

void SchematicEditor::applyDirectiveText(SchematicSpiceDirectiveItem* item, const QString& newText) {
    if (!item) return;
    const QString trimmed = newText.trimmed();
    if (trimmed.isEmpty() || item->text() == trimmed) return;
    if (m_undoStack && m_scene) {
        m_undoStack->push(new ChangePropertyCommand(m_scene, item, "Text", item->text(), trimmed));
    } else {
        item->setText(trimmed);
        item->update();
    }
}

SchematicSpiceDirectiveItem* SchematicEditor::resolveDirectiveItemForEdit(const QString& currentCommand) const {
    SchematicSpiceDirectiveItem* directiveItem = qobject_cast<SchematicSpiceDirectiveItem*>(sender());
    if (!directiveItem && m_scene) {
        for (auto* gi : m_scene->selectedItems()) {
            if (auto* selectedDirective = dynamic_cast<SchematicSpiceDirectiveItem*>(gi)) {
                directiveItem = selectedDirective;
                break;
            }
        }
    }
    if (!directiveItem && m_scene) {
        for (auto* gi : m_scene->items()) {
            if (auto* candidate = dynamic_cast<SchematicSpiceDirectiveItem*>(gi)) {
                if (candidate->text().trimmed() == currentCommand.trimmed()) {
                    directiveItem = candidate;
                    break;
                }
            }
        }
    }
    return directiveItem;
}

bool SchematicEditor::editDirectiveWithSimulationSetup(const QString& currentCommand, SchematicSpiceDirectiveItem* directiveItem) {
    m_simConfig.commandText = currentCommand;

    SimulationSetupDialog dlg(this);
    dlg.setConfig(m_simConfig);
    if (dlg.exec() != QDialog::Accepted) {
        return true;
    }

    m_simConfig = dlg.getConfig();

    if (m_simulationPanel) {
        SimulationPanel::AnalysisConfig pCfg;
        pCfg.type = m_simConfig.type;
        pCfg.stop = m_simConfig.stop;
        pCfg.step = m_simConfig.step;
        pCfg.fStart = m_simConfig.fStart;
        pCfg.fStop = m_simConfig.fStop;
        pCfg.pts = m_simConfig.pts;
        m_simulationPanel->setAnalysisConfig(pCfg);
    }

    if (!m_simConfig.commandText.isEmpty()) {
        if (directiveItem) {
            applyDirectiveText(directiveItem, m_simConfig.commandText);
        } else if (m_simulationPanel) {
            m_simulationPanel->updateSchematicDirectiveFromCommand(m_simConfig.commandText);
        }
    }
    statusBar()->showMessage("Simulation directive updated.", 3000);
    return true;
}

bool SchematicEditor::editDirectiveWithMeanDialog(const QString& currentCommand, SchematicSpiceDirectiveItem* directiveItem) {
    SpiceMeanDialog dlg(currentCommand, this);
    if (dlg.exec() != QDialog::Accepted) {
        return true;
    }

    const QString newCommand = dlg.commandText();
    if (directiveItem) {
        applyDirectiveText(directiveItem, newCommand);
    } else if (m_simulationPanel) {
        m_simulationPanel->updateSchematicDirectiveFromCommand(newCommand);
    }
    statusBar()->showMessage("SPICE .mean directive updated.", 3000);
    return true;
}

bool SchematicEditor::editDirectiveWithGenericDialog(const QString& currentCommand, SchematicSpiceDirectiveItem* directiveItem) {
    if (directiveItem) {
        SpiceDirectiveDialog dlg(directiveItem, m_undoStack, m_scene, this);
        if (dlg.exec() == QDialog::Accepted) {
            statusBar()->showMessage("SPICE directive updated.", 3000);
        }
        return true;
    }

    m_simConfig.commandText = currentCommand;
    SimulationSetupDialog dlg(this);
    dlg.setConfig(m_simConfig);
    if (dlg.exec() == QDialog::Accepted) {
        m_simConfig = dlg.getConfig();
        if (!m_simConfig.commandText.isEmpty() && m_simulationPanel) {
            m_simulationPanel->updateSchematicDirectiveFromCommand(m_simConfig.commandText);
        }
    }
    return true;
}

void SchematicEditor::onEditSimulationFromDirective(const QString& currentCommand) {
    SchematicSpiceDirectiveItem* directiveItem = resolveDirectiveItemForEdit(currentCommand);

    const SpiceDirectiveClassification classification = SpiceDirectiveClassifier::classify(currentCommand);

    struct EditRoute {
        SpiceDirectiveEditTarget target;
        bool (SchematicEditor::*handler)(const QString&, SchematicSpiceDirectiveItem*);
    };

    static const std::array<EditRoute, 3> routes = {{
        {SpiceDirectiveEditTarget::SimulationSetup, &SchematicEditor::editDirectiveWithSimulationSetup},
        {SpiceDirectiveEditTarget::MeanDialog, &SchematicEditor::editDirectiveWithMeanDialog},
        {SpiceDirectiveEditTarget::GenericDirective, &SchematicEditor::editDirectiveWithGenericDialog},
    }};

    for (const EditRoute& route : routes) {
        if (route.target == classification.target) {
            (this->*route.handler)(currentCommand, directiveItem);
            return;
        }
    }

    editDirectiveWithGenericDialog(currentCommand, directiveItem);
}

void SchematicEditor::onRunSimulation() {
    if (!m_scene || !m_netManager) {
        updateSimulationUiState(false, "Simulation unavailable: scene or net manager is not ready.");
        return;
    }

    if (m_simulationRunning) {
        onPauseSimulation(); // Toggle pause/resume if already running
        return;
    }

    // Auto-save pending Smart Signal code edits before building simulator netlist.
    if (m_logicEditorPanel) {
        m_logicEditorPanel->flushEdits();
    }

    // Auto-annotate if duplicate references exist (prevents netlist collisions like V1/V1/V1).
    {
        QSet<QString> seen;
        bool hasDup = false;
        for (auto* gi : m_scene->items()) {
            auto* si = dynamic_cast<SchematicItem*>(gi);
            if (!si) continue;
            const int t = si->itemType();
            if (t == SchematicItem::WireType ||
                t == SchematicItem::LabelType ||
                t == SchematicItem::NetLabelType ||
                t == SchematicItem::JunctionType ||
                t == SchematicItem::NoConnectType ||
                t == SchematicItem::BusType ||
                t == SchematicItem::SheetType ||
                t == SchematicItem::HierarchicalPortType) {
                continue;
            }
            const QString ref = si->reference().trimmed();
            if (ref.isEmpty()) continue;
            const QString key = ref.toUpper();
            if (seen.contains(key)) { hasDup = true; break; }
            seen.insert(key);
        }
        if (hasDup) {
            onAnnotate();
        }
    }

    m_simulationRunning = true;
    updateSimulationUiState(true, "Starting simulation...");
    
    // Force UI update to show Pause/Stop buttons before heavy netlist building blocks the main thread
    qApp->processEvents();

    // Netlist generation now happens in the SimulationPanel background worker.
    // Avoid blocking the UI with a full net rebuild here.

    // Keep results in the oscilloscope dock only (no Simulation Results tab)
    for (int i = 0; i < m_workspaceTabs->count(); ++i) {
        if (auto* panel = qobject_cast<SimulationPanel*>(m_workspaceTabs->widget(i))) {
            m_simulationPanel = panel;
            m_workspaceTabs->removeTab(i);
            break;
        }
    }
    if (m_oscilloscopeDock && m_simulationPanel) {
        m_oscilloscopeDock->setWidget(m_simulationPanel->getOscilloscopeContainer());
        m_oscilloscopeDock->setFloating(false);
        m_oscilloscopeDock->show();
    }

    if (m_simulationPanel) {
        m_simulationPanel->setTargetScene(m_scene, m_netManager, m_projectDir, true);
    }
    
    // Ensure all logic analyzer windows are ready.
    for (auto* item : m_scene->items()) {
        auto* sItem = dynamic_cast<SchematicItem*>(item);
        if (!sItem) continue;
        const QString typeName = sItem->itemTypeName().toLower();
        if (!typeName.contains("logicanalyzer") && !typeName.contains("logic analyzer")) continue;

        QString id = sItem->id().toString();
        if (id.isEmpty()) id = QString("LA_%1").arg(reinterpret_cast<quintptr>(sItem), 0, 16);

        if (!m_laWindows.contains(id)) {
            auto* win = new LogicAnalyzerWindow("Logic Analyzer - " + sItem->reference(), this);
            win->setInstrumentId(id);
            connect(win, &LogicAnalyzerWindow::windowClosing, this, [this](const QString& windowId) {
                m_laWindows.remove(windowId);
            });
            m_laWindows[id] = win;
        }

        const QStringList nets = resolveConnectedInstrumentNets(sItem);
        m_laWindows[id]->setChannels(nets);
        m_laWindows[id]->show();
        m_laWindows[id]->raise();
        m_laWindows[id]->activateWindow();
    }

    // Clear previous results so failed runs don't show stale data
    if (m_simulationPanel) {
        m_simulationPanel->clearResults();
    }

    // Route probes and hardware oscilloscopes to the bottom dock simulation panel
    QStringList probedNets;
    for (auto* item : m_scene->items()) {
        if (auto* m = dynamic_cast<SchematicWaveformMarker*>(item)) {
            probedNets << m->netName();
            if (m_simulationPanel) {
                m_simulationPanel->addProbe(m->netName());
            }
        } else if (auto* sItem = dynamic_cast<SchematicItem*>(item)) {
            const QString typeName = sItem->itemTypeName().toLower();
            const QString ref = sItem->reference().toLower();
            if (typeName.contains("oscilloscope") || ref.startsWith("osc")) {
                const QStringList oscNets = resolveConnectedInstrumentNets(sItem);
                for (const QString& net : oscNets) {
                    probedNets << net;
                    if (m_simulationPanel) m_simulationPanel->addProbe(net);
                }
            }
        }
    }
    
    // 1. Preflight check and Debugger
    SimNetlist netlist;
    QStringList diagnostics = SimManager::instance().preflightCheck(m_scene, m_netManager, netlist);

    if (m_simulationPanel) {
        const auto cfg = m_simulationPanel->getAnalysisConfig();
        m_simConfig.type = cfg.type;
        m_simConfig.stop = cfg.stop;
        m_simConfig.step = cfg.step;
        m_simConfig.fStart = cfg.fStart;
        m_simConfig.fStop = cfg.fStop;
        m_simConfig.pts = cfg.pts;
    }

    // Apply simulation config to the pre-built netlist
    SimAnalysisConfig config;
    if (m_simConfig.type == SimAnalysisType::Transient) {
        config.type = SimAnalysisType::Transient;
        config.tStart = 0;
        config.tStop = m_simConfig.stop;
        config.tStep = m_simConfig.step;
        config.transientStorageMode = SimTransientStorageMode::AutoDecimate;
        config.transientMaxStoredPoints = 50000;
    } else if (m_simConfig.type == SimAnalysisType::OP) {
        config.type = SimAnalysisType::OP;
    } else if (m_simConfig.type == SimAnalysisType::AC) {
        config.type = SimAnalysisType::AC;
        config.fStart = m_simConfig.fStart > 0.0 ? m_simConfig.fStart : 10.0;
        config.fStop = m_simConfig.fStop > 0.0 ? m_simConfig.fStop : 1e6;
        config.fPoints = m_simConfig.pts > 0 ? m_simConfig.pts : 10;
    }
    netlist.setAnalysis(config);

    QStringList warnOrError;
    for (const auto& d : diagnostics) {
        if (d.contains("[warn]", Qt::CaseInsensitive) || d.contains("[error]", Qt::CaseInsensitive)) {
            warnOrError << d;
        }
    }
    if (!warnOrError.isEmpty()) {
        SimulationDebuggerDialog dlg(warnOrError, this);
        if (dlg.exec() != QDialog::Accepted) {
            m_simulationRunning = false;
            updateSimulationUiState(false, "Simulation aborted by user.");
            return;
        }
    }

    // 2. Trigger Engine via Ngspice backend (direct SimNetlist execution is not supported).
    SimManager::instance().runNgspiceSimulation(m_scene, m_netManager, config);
}

void SchematicEditor::onPauseSimulation() {
    if (!m_simulationRunning) {
        onRunSimulation();
        return;
    }

    SimManager::instance().pauseSimulation(!m_simPaused);
}

void SchematicEditor::onOpenFluxScript() {
    if (m_scriptDock) {
        m_scriptDock->show();
        m_scriptDock->raise();
    }
}

void SchematicEditor::updateBreadcrumbs() {
    if (!m_breadcrumbWidget || !m_breadcrumbWidget->layout()) return;
    
    // Clear layout
    QLayoutItem* item;
    while ((item = m_breadcrumbWidget->layout()->takeAt(0)) != nullptr) {
        if (item->widget()) delete item->widget();
        delete item;
    }
    
    // Add "Root"
    QPushButton* rootBtn = new QPushButton("ROOT");
    rootBtn->setFlat(true);
    rootBtn->setCursor(Qt::PointingHandCursor);
    rootBtn->setStyleSheet("QPushButton { color: #94a3b8; font-family: 'Inter'; font-size: 10px; font-weight: bold; border: none; padding: 2px 5px; } "
                          "QPushButton:hover { color: #f8fafc; background: rgba(255,255,255,0.1); border-radius: 4px; }");
    m_breadcrumbWidget->layout()->addWidget(rootBtn);
    
    connect(rootBtn, &QPushButton::clicked, this, [this]() {
        if (m_navigationStack.isEmpty()) return;
        QString rootPath = m_navigationStack.first();
        m_navigationStack.clear();
        openFile(rootPath);
        updateBreadcrumbs();
    });

    // Add path segments
    for (int i = 0; i < m_navigationStack.size(); ++i) {
        QLabel* sep = new QLabel(""); // Modern arrow glyph if font supports it, or just >
        sep->setStyleSheet("color: #475569; font-size: 10px;");
        m_breadcrumbWidget->layout()->addWidget(sep);
        
        QString path = m_navigationStack[i];
        QFileInfo info(path);
        QPushButton* btn = new QPushButton(info.baseName().toUpper());
        btn->setFlat(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet("QPushButton { color: #38bdf8; font-family: 'Inter'; font-size: 10px; border: none; padding: 2px 5px; } "
                          "QPushButton:hover { color: #7dd3fc; background: rgba(56,189,248,0.1); border-radius: 4px; }");
        m_breadcrumbWidget->layout()->addWidget(btn);
        
        connect(btn, &QPushButton::clicked, this, [this, i]() {
            QString targetPath = m_navigationStack[i];
            while (m_navigationStack.size() > i) {
                m_navigationStack.removeLast();
            }
            openFile(targetPath);
            updateBreadcrumbs();
        });
    }
    
    // Current File
    if (!m_currentFilePath.isEmpty()) {
        QLabel* sep = new QLabel("");
        sep->setStyleSheet("color: #475569; font-size: 10px;");
        m_breadcrumbWidget->layout()->addWidget(sep);
        
        QFileInfo info(m_currentFilePath);
        QLabel* label = new QLabel(info.baseName().toUpper());
        label->setStyleSheet("color: #ec4899; font-family: 'Inter'; font-size: 10px; font-weight: bold; padding: 2px 5px; background: rgba(236,72,153,0.1); border-radius: 4px;");
        m_breadcrumbWidget->layout()->addWidget(label);
    }

    // Sync the hierarchy tree with the current navigation state
    refreshHierarchyPanel();
}
