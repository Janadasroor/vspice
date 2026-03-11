#ifndef THEME_H
#define THEME_H

#include <QColor>
#include <QString>
#include <QPalette>

class QWidget;

class PCBTheme {
public:
    // Theme types
    enum ThemeType {
        Dark,
        Light,
        Engineering  // Monochrome high contrast for schematics
    };

    PCBTheme(ThemeType type = Dark);

    // Main UI colors
    QColor windowBackground() const { return m_windowBackground; }
    QColor panelBackground() const { return m_panelBackground; }
    QColor panelBorder() const { return m_panelBorder; }
    QColor textColor() const { return m_textColor; }
    QColor textSecondary() const { return m_textSecondary; }
    QColor accentColor() const { return m_accentColor; }
    QColor accentHover() const { return m_accentHover; }

    // PCB View colors
    QColor canvasBackground() const { return m_canvasBackground; }
    QColor gridPrimary() const { return m_gridPrimary; }
    QColor gridSecondary() const { return m_gridSecondary; }

    // PCB Layer colors
    QColor topCopper() const { return m_topCopper; }
    QColor bottomCopper() const { return m_bottomCopper; }
    QColor topSilkscreen() const { return m_topSilkscreen; }
    QColor bottomSilkscreen() const { return m_bottomSilkscreen; }
    QColor topSoldermask() const { return m_topSoldermask; }
    QColor bottomSoldermask() const { return m_bottomSoldermask; }
    QColor edgeCuts() const { return m_edgeCuts; }
    QColor drillHoles() const { return m_drillHoles; }

    // PCB Item colors
    QColor padFill() const { return m_padFill; }
    QColor padStroke() const { return m_padStroke; }
    QColor viaFill() const { return m_viaFill; }
    QColor viaStroke() const { return m_viaStroke; }
    QColor trace() const { return m_trace; }
    QColor componentOutline() const { return m_componentOutline; }
    QColor componentFill() const { return m_componentFill; }

    // Selection colors
    QColor selectionBox() const { return m_selectionBox; }
    QColor selectionHighlight() const { return m_selectionHighlight; }

    // Schematic colors
    QColor schematicLine() const { return m_schematicLine; }
    QColor schematicComponent() const { return m_schematicComponent; }
    QColor schematicBus() const { return m_schematicBus; }

    // Wire routing colors
    QColor signalWire() const { return m_signalWire; }
    QColor powerWire() const { return m_powerWire; }
    QColor wireJunction() const { return m_wireJunction; }
    QColor wireJumpOver() const { return m_wireJumpOver; }

    // Status colors
    QColor errorColor() const { return m_errorColor; }
    QColor warningColor() const { return m_warningColor; }
    QColor successColor() const { return m_successColor; }

    // Get stylesheet for Qt widgets
    QString widgetStylesheet() const;
    QString toolbarStylesheet() const;
    QString dockStylesheet() const;
    QString statusBarStylesheet() const;

    // Apply theme to QWidget
    void applyToWidget(QWidget* widget) const;

    ThemeType type() const { return m_type; }

private:
    ThemeType m_type;

    // Main UI colors
    QColor m_windowBackground;
    QColor m_panelBackground;
    QColor m_panelBorder;
    QColor m_textColor;
    QColor m_textSecondary;
    QColor m_accentColor;
    QColor m_accentHover;

    // PCB View colors
    QColor m_canvasBackground;
    QColor m_gridPrimary;
    QColor m_gridSecondary;

    // PCB Layer colors
    QColor m_topCopper;
    QColor m_bottomCopper;
    QColor m_topSilkscreen;
    QColor m_bottomSilkscreen;
    QColor m_topSoldermask;
    QColor m_bottomSoldermask;
    QColor m_edgeCuts;
    QColor m_drillHoles;

    // PCB Item colors
    QColor m_padFill;
    QColor m_padStroke;
    QColor m_viaFill;
    QColor m_viaStroke;
    QColor m_trace;
    QColor m_componentOutline;
    QColor m_componentFill;

    // Selection colors
    QColor m_selectionBox;
    QColor m_selectionHighlight;

    // Schematic colors
    QColor m_schematicLine;
    QColor m_schematicComponent;
    QColor m_schematicBus;

    // Wire routing colors
    QColor m_signalWire;
    QColor m_powerWire;
    QColor m_wireJunction;
    QColor m_wireJumpOver;

    // Status colors
    QColor m_errorColor;
    QColor m_warningColor;
    QColor m_successColor;

    void setupDarkTheme();
    void setupLightTheme();
    void setupEngineeringTheme();
};

#endif // THEME_H
