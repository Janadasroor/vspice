#include "theme.h"
#include <QApplication>
#include <QStyleFactory>
#include <QWidget>
#include <QPainter>

PCBTheme::PCBTheme(ThemeType type)
    : m_type(type) {
    if (type == Light) {
        setupLightTheme();
    } else if (type == Engineering) {
        setupEngineeringTheme();
    } else {
        setupDarkTheme();
    }
}

void PCBTheme::setupDarkTheme() {
    // Main UI colors - Ultra Dark professional aesthetic
    m_windowBackground = QColor(20, 20, 23);      // Deeper Zinc 950
    m_panelBackground = QColor(24, 24, 27);       // Zinc 900
    m_panelBorder = QColor(39, 39, 42);           // Zinc 800
    m_textColor = QColor(244, 244, 245);          // Zinc 100
    m_textSecondary = QColor(161, 161, 170);      // Zinc 400
    m_accentColor = QColor(37, 99, 235);          // Blue 600
    m_accentHover = QColor(59, 130, 246);         // Blue 500

    // PCB View colors
    m_canvasBackground = QColor(10, 10, 12);      // Deep canvas
    m_gridPrimary = QColor(80, 80, 90);           // Brightened primary grid
    m_gridSecondary = QColor(50, 50, 55);         // Brightened secondary grid

    // PCB Layer colors (Industry Standard Inspired)
    m_topCopper = QColor(220, 40, 40);           // Professional Red
    m_bottomCopper = QColor(59, 130, 246);       // Tech Blue
    m_topSilkscreen = QColor(240, 240, 240);    // Bright White
    m_bottomSilkscreen = QColor(200, 200, 200); // Muted White
    m_topSoldermask = QColor(20, 100, 40, 200); // Transparent Green
    m_bottomSoldermask = QColor(20, 40, 100, 200);// Transparent Blue
    m_edgeCuts = QColor(255, 200, 0);           // High-viz Yellow
    m_drillHoles = QColor(120, 120, 130);
    m_multiLayer = QColor(255, 255, 100);       // Professional Gold/Yellow for TH

    // PCB Item colors
    m_padFill = QColor(217, 119, 6);            // Rich Amber
    m_padStroke = QColor(180, 83, 9);
    m_viaFill = QColor(160, 160, 170);          // Tin/Silver
    m_viaStroke = QColor(130, 130, 140);
    m_trace = QColor(220, 40, 40);
    m_componentOutline = QColor(52, 211, 153);  // Emerald Green
    m_componentFill = QColor(30, 30, 35, 100);

    // Selection colors
    m_selectionBox = QColor(59, 130, 246);
    m_selectionHighlight = QColor(59, 130, 246, 80);

    // Schematic colors
    m_schematicLine = QColor(200, 200, 210);
    m_schematicComponent = QColor(180, 180, 190);
    m_schematicBus = QColor(59, 130, 246);

    // Wire routing
    m_signalWire = QColor(52, 211, 153);        // Emerald signal wires
    m_powerWire = QColor(248, 113, 113);         // Soft Red power wires
    m_wireJunction = QColor(59, 130, 246);
    m_wireJumpOver = QColor(100, 100, 110);

    // Status colors
    m_errorColor = QColor(239, 68, 68);
    m_warningColor = QColor(245, 158, 11);
    m_successColor = QColor(34, 197, 94);
}

void PCBTheme::setupLightTheme() {
    // Main UI colors - Clean, high-end "Studio" aesthetic
    m_windowBackground = QColor(255, 255, 255);   // Pure White
    m_panelBackground = QColor(248, 250, 252);    // Slate 50
    m_panelBorder = QColor(226, 232, 240);        // Slate 200
    m_textColor = QColor(15, 23, 42);             // Slate 900
    m_textSecondary = QColor(71, 85, 105);        // Slate 600
    m_accentColor = QColor(37, 99, 235);          // Blue 600
    m_accentHover = QColor(29, 78, 216);          // Blue 700

    // PCB View colors
    m_canvasBackground = QColor(255, 255, 255);
    m_gridPrimary = QColor(226, 232, 240);
    m_gridSecondary = QColor(241, 245, 249);

    // PCB Layer colors
    m_topCopper = QColor(230, 50, 50);           // Vibrant Red
    m_bottomCopper = QColor(30, 110, 220);        // Deep Blue
    m_topSilkscreen = QColor(40, 40, 40);         // Dark Gray
    m_bottomSilkscreen = QColor(80, 80, 80);
    m_topSoldermask = QColor(30, 150, 60, 180);
    m_bottomSoldermask = QColor(30, 60, 150, 180);
    m_edgeCuts = QColor(255, 120, 0);             // Bright Orange
    m_drillHoles = QColor(100, 100, 100);
    m_multiLayer = QColor(220, 200, 50);         // Muted Gold for TH

    // PCB Item colors
    m_padFill = QColor(220, 140, 60);
    m_padStroke = QColor(180, 110, 40);
    m_viaFill = QColor(180, 180, 185);
    m_viaStroke = QColor(140, 140, 145);
    m_trace = QColor(230, 50, 50);
    m_componentOutline = QColor(50, 50, 55);
    m_componentFill = QColor(240, 240, 245, 150);

    // Selection colors
    m_selectionBox = QColor(37, 99, 235, 60);
    m_selectionHighlight = QColor(37, 99, 235, 100);

    // Schematic colors
    m_schematicLine = QColor(30, 41, 59);
    m_schematicComponent = QColor(51, 65, 85);
    m_schematicBus = QColor(29, 78, 216);

    // Wire routing
    m_signalWire = QColor(5, 150, 105);           // Emerald 600
    m_powerWire = QColor(220, 38, 38);            // Red 600
    m_wireJunction = QColor(30, 41, 59);
    m_wireJumpOver = QColor(100, 116, 139);

    // Status colors
    m_errorColor = QColor(220, 38, 38);
    m_warningColor = QColor(217, 119, 6);
    m_successColor = QColor(22, 163, 74);
}

void PCBTheme::setupEngineeringTheme() {
    // Professional engineering theme - Neutral grays, minimal distraction (VS Code style)

    // Main UI colors
    m_windowBackground = QColor(24, 24, 27);     // Zinc 900
    m_panelBackground = QColor(18, 18, 20);      // Zinc 950 (~5% darker)
    m_panelBorder = QColor(63, 63, 70);          // Zinc 700
    m_textColor = QColor(244, 244, 245);         // Zinc 100
    m_textSecondary = QColor(161, 161, 170);     // Zinc 400
    m_accentColor = QColor(59, 130, 246);        // Blue 500
    m_accentHover = QColor(96, 165, 250);        // Blue 400

    // Schematic canvas
    m_canvasBackground = QColor(9, 9, 11);       // Zinc 950
    m_gridPrimary = QColor(39, 39, 42);
    m_gridSecondary = QColor(24, 24, 27);

    // PCB Layer colors (Standard Professional)
    m_topCopper = QColor(220, 40, 40);           // Red for top (industry standard)
    m_bottomCopper = QColor(51, 115, 184);       // Blue for bottom (common in EDA)
    m_topSilkscreen = QColor(255, 255, 255);
    m_bottomSilkscreen = QColor(200, 200, 200);
    m_topSoldermask = QColor(20, 80, 40);        // Dark green
    m_multiLayer = QColor(200, 200, 50);        // Engineering Gold for TH
    m_bottomSoldermask = QColor(20, 40, 80);     // Dark blue
    m_edgeCuts = QColor(255, 255, 0);            // Yellow for visibility
    m_drillHoles = QColor(100, 100, 100);

    // PCB Item colors
    m_padFill = QColor(180, 83, 9);           // Rich copper/amber pad fill
    m_padStroke = QColor(146, 64, 14);        // Subtle darker stroke
    m_viaFill = QColor(200, 200, 200);
    m_viaStroke = QColor(150, 150, 150);
    m_trace = QColor(220, 40, 40);
    m_componentOutline = QColor(0, 255, 0);   // Neon Green Silkscreen
    m_componentFill = QColor(0, 0, 0, 0);     // Transparent body

    // Selection colors
    m_selectionBox = QColor(59, 130, 246);
    m_selectionHighlight = QColor(59, 130, 246, 60); // Transparent blue

    // Schematic colors
    m_schematicLine = QColor(204, 204, 204);     // Light gray lines
    m_schematicComponent = QColor(180, 180, 180);
    m_schematicBus = QColor(59, 130, 246);

    // Wire routing
    m_signalWire = QColor(100, 200, 100);        // Green signal wires
    m_powerWire = QColor(255, 100, 100);         // Red power wires
    m_wireJunction = QColor(59, 130, 246);        // Blue dots
    m_wireJumpOver = QColor(100, 100, 100);

    // Status colors
    m_errorColor = QColor(241, 76, 76);
    m_warningColor = QColor(245, 158, 11);
    m_successColor = QColor(34, 197, 94);
}

QString PCBTheme::widgetStylesheet() const {
    bool isDark = (m_type != Light);
    
    // Dynamic values based on theme
    QString inputBg = isDark ? "#18181b" : "#ffffff";
    QString inputText = isDark ? m_textColor.name() : "#1e293b"; // Ensure dark text in light theme
    QString itemHover = isDark ? "#3f3f46" : "#f1f5f9";
    QString itemSelected = isDark ? "#27272a" : "#eff6ff";
    QString headerBg = isDark ? "#27272a" : "#f8fafc";
    QString menuBg = isDark ? "#18181b" : "#ffffff";
    QString btnBg = isDark ? "#27272a" : "#ffffff";
    QString btnHover = isDark ? "#3f3f46" : "#f8fafc";
    QString scrollHandle = isDark ? "#52525b" : "#cbd5e1";
    QString indicatorBg = isDark ? "#18181b" : "#ffffff";
    QString indicatorBorder = isDark ? m_panelBorder.name() : "#94a3b8";
    QString indicatorHover = m_accentColor.name();
    QString indicatorDisabledBg = isDark ? "#27272a" : "#e2e8f0";
    QString indicatorDisabledBorder = isDark ? "#52525b" : "#cbd5e1";

    return QString(
        "QWidget {"
        "   background-color: %1;"
        "   color: %2;"
        "   font-family: 'Inter', 'Segoe UI', sans-serif;"
        "   selection-background-color: %5;"
        "   selection-color: %6;"
        "}"
        "QMainWindow {"
        "   background-color: %1;"
        "}"
        "QMenuBar {"
        "   background-color: %7;"
        "   color: %8;"
        "   padding: 4px;"
        "   border-bottom: 1px solid %4;"
        "}"
        "QMenuBar::item {"
        "   background: transparent;"
        "   padding: 6px 12px;"
        "   border-radius: 6px;"
        "}"
        "QMenuBar::item:selected {"
        "   background-color: %9;"
        "   color: %10;"
        "}"
        "QMenu {"
        "   background-color: %7;"
        "   border: 1px solid %4;"
        "   border-radius: 8px;"
        "   padding: 4px;"
        "   color: %2;"
        "}"
        "QMenu::item {"
        "   padding: 8px 32px 8px 12px;"
        "   border-radius: 4px;"
        "   margin: 2px 4px;"
        "}"
        "QMenu::item:selected {"
        "   background-color: %5;"
        "   color: white;"
        "}"
        "QMenu::separator {"
        "   height: 1px;"
        "   background: %4;"
        "   margin: 6px 8px;"
        "}"
        "QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QSpinBox, QDoubleSpinBox {"
        "   background-color: %11;"
        "   color: %16;"
        "   border: 1px solid %4;"
        "   border-radius: 8px;"
        "   padding: 6px 12px;"
        "}"
        "QLineEdit:focus, QTextEdit:focus {"
        "   border-color: %5;"
        "   background-color: %3;"
        "   outline: none;"
        "}"
        "QPushButton {"
        "   background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %12, stop:1 %13);"
        "   border: 1px solid %4;"
        "   border-radius: 8px;"
        "   padding: 8px 20px;"
        "   color: %2;"
        "   font-weight: 600;"
        "}"
        "QPushButton:hover {"
        "   background: %13;"
        "   border-color: %5;"
        "}"
        "QPushButton:pressed {"
        "   background-color: %4;"
        "   padding-top: 9px;"
        "   padding-bottom: 7px;"
        "}"
        "QCheckBox, QRadioButton {"
        "   spacing: 8px;"
        "}"
        "QCheckBox::indicator, QRadioButton::indicator {"
        "   width: 14px;"
        "   height: 14px;"
        "   background-color: %17;"
        "   border: 1px solid %18;"
        "}"
        "QCheckBox::indicator {"
        "   border-radius: 3px;"
        "}"
        "QRadioButton::indicator {"
        "   border-radius: 7px;"
        "}"
        "QCheckBox::indicator:hover, QRadioButton::indicator:hover {"
        "   border-color: %19;"
        "}"
        "QCheckBox::indicator:checked {"
        "   image: url(:/icons/check.svg);"
        "   border-color: %5;"
        "}"
        "QRadioButton::indicator:checked {"
        "   background-color: %5;"
        "   border: 3px solid %17;"
        "   border-color: %5;"
        "}"
        "QCheckBox::indicator:disabled, QRadioButton::indicator:disabled {"
        "   background-color: %20;"
        "   border-color: %21;"
        "}"
        "QTreeWidget, QListWidget, QTableWidget {"
        "   background-color: %3;"
        "   border: 1px solid %4;"
        "   border-radius: 8px;"
        "   color: %2;"
        "   outline: none;"
        "}"
        "QTreeWidget::item, QListWidget::item {"
        "   border-radius: 4px;"
        "   margin: 1px 4px;"
        "   padding: 6px;"
        "}"
        "QHeaderView::section {"
        "   background-color: %14;"
        "   color: %8;"
        "   padding: 10px 12px;"
        "   border: none;"
        "   border-bottom: 1px solid %4;"
        "   font-weight: 700;"
        "   font-size: 11px;"
        "   text-transform: uppercase;"
        "   letter-spacing: 0.5px;"
        "}"
        "QScrollBar:vertical {"
        "   background: transparent;"
        "   width: 10px;"
        "   margin: 2px;"
        "}"
        "QScrollBar::handle:vertical {"
        "   background: %15;"
        "   border-radius: 5px;"
        "   min-height: 30px;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
    ).arg(m_windowBackground.name())      // 1
     .arg(m_textColor.name())             // 2
     .arg(m_panelBackground.name())       // 3
     .arg(m_panelBorder.name())           // 4
     .arg(m_accentColor.name())           // 5
     .arg("#ffffff")                      // 6
     .arg(menuBg)                         // 7
     .arg(m_textSecondary.name())         // 8
     .arg(itemHover)                      // 9
     .arg(m_textColor.name())             // 10
     .arg(inputBg)                        // 11
     .arg(btnBg)                          // 12
     .arg(btnHover)                       // 13
     .arg(headerBg)                       // 14
     .arg(scrollHandle)                   // 15
     .arg(inputText)                      // 16
     .arg(indicatorBg)                    // 17
     .arg(indicatorBorder)                // 18
     .arg(indicatorHover)                 // 19
     .arg(indicatorDisabledBg)            // 20
     .arg(indicatorDisabledBorder);       // 21
}

QString PCBTheme::toolbarStylesheet() const {
    bool isDark = (m_type != Light);
    QString btnHover = isDark ? "#3f3f46" : "#f1f5f9";
    
    return QString(
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
        "   border-radius: 8px;"
        "   padding: 4px;"
        "}"
        "QToolButton:hover {"
        "   background-color: %3;"
        "   border-color: %2;"
        "}"
        "QToolButton:checked {"
        "   background-color: %3;"
        "   border: 1px solid %4;"
        "}"
    ).arg(m_windowBackground.name())
     .arg(m_panelBorder.name())
     .arg(btnHover)
     .arg(m_accentColor.name());
}

QString PCBTheme::dockStylesheet() const {
    bool isDark = (m_type != Light);
    QString titleBg = isDark ? m_panelBackground.darker(110).name() : m_panelBackground.darker(105).name();
    QString tabSelectedBg = isDark ? "#1a1a1c" : "#cbd5e1";
    
    return QString(
        "QDockWidget {"
        "   background-color: %1;"
        "   border: 1px solid %4;"
        "   border-radius: 10px;"
        "}"
        "QDockWidget::title {"
        "   background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %2, stop:1 %1);"
        "   padding: 12px 16px;"
        "   font-weight: 800;"
        "   font-size: 10px;"
        "   letter-spacing: 1.2px;"
        "   text-transform: uppercase;"
        "   color: %3;"
        "   border-bottom: 1px solid %4;"
        "}"
        "QTabBar::tab {"
        "   background-color: transparent;"
        "   color: %3;"
        "   padding: 10px 24px;"
        "   border: none;"
        "   font-size: 11px;"
        "   font-weight: 600;"
        "}"
        "QTabBar::tab:selected {"
        "   background-color: %6;"
        "   color: %5;"
        "   border-bottom: 2px solid %5;"
        "   padding-bottom: 8px;"
        "}"
    ).arg(m_panelBackground.name())
     .arg(titleBg)
     .arg(m_textSecondary.name())
     .arg(m_panelBorder.name())
     .arg(m_accentColor.name())
     .arg(tabSelectedBg);
}

QString PCBTheme::statusBarStylesheet() const {
    bool isDark = (m_type != Light);
    QString bg = isDark ? "#151515" : "#f8f9fa";
    
    return QString(
        "QStatusBar {"
        "   background: %1;"
        "   color: %2;"
        "   border-top: 1px solid %3;"
        "   padding: 2px 8px;"
        "}"
    ).arg(bg).arg(m_textSecondary.name()).arg(m_panelBorder.name());
}

void PCBTheme::applyToWidget(QWidget* widget) const {
    if (!widget) return;

    // Apply palette for non-stylesheet colors
    QPalette palette = widget->palette();
    palette.setColor(QPalette::Window, m_windowBackground);
    palette.setColor(QPalette::WindowText, m_textColor);
    QColor inputBgColor = (m_type == Light) ? Qt::white : QColor(24, 24, 27);
    palette.setColor(QPalette::Base, inputBgColor);
    palette.setColor(QPalette::Text, (m_type == Light) ? QColor(30, 41, 59) : m_textColor);
    palette.setColor(QPalette::Highlight, m_accentColor);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    
    // Explicitly set disabled colors to muted grey
    QColor disabledColor(100, 100, 100);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, disabledColor);
    palette.setColor(QPalette::Disabled, QPalette::Text, disabledColor);
    palette.setColor(QPalette::Disabled, QPalette::Base, inputBgColor.darker(110));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabledColor);
    palette.setColor(QPalette::Disabled, QPalette::Button, m_windowBackground.darker(120));
    
    widget->setPalette(palette);

    // Main window level styles
    widget->setStyleSheet(widgetStylesheet());
}

void PCBTheme::applyToApplication() const {
    if (!qApp) return;

    // Apply palette for non-stylesheet colors globally
    QPalette palette = qApp->palette();
    palette.setColor(QPalette::Window, m_windowBackground);
    palette.setColor(QPalette::WindowText, m_textColor);
    QColor inputBgColor = (m_type == Light) ? Qt::white : QColor(24, 24, 27);
    palette.setColor(QPalette::Base, inputBgColor);
    palette.setColor(QPalette::Text, (m_type == Light) ? QColor(30, 41, 59) : m_textColor);
    palette.setColor(QPalette::Highlight, m_accentColor);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    
    // Explicitly set disabled colors to muted grey
    QColor disabledColor(100, 100, 100);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, disabledColor);
    palette.setColor(QPalette::Disabled, QPalette::Text, disabledColor);
    palette.setColor(QPalette::Disabled, QPalette::Base, inputBgColor.darker(110));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabledColor);
    palette.setColor(QPalette::Disabled, QPalette::Button, m_windowBackground.darker(120));
    
    qApp->setPalette(palette);

    // Global stylesheet
    qApp->setStyleSheet(widgetStylesheet());
}
