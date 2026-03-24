#ifndef NETLIST_TO_SCHEMATIC_H
#define NETLIST_TO_SCHEMATIC_H

#include <QString>

class QGraphicsScene;

/**
 * @brief Converts a SPICE netlist (.cir) into a schematic (.flxsch) file.
 *
 * This class parses the netlist, creates schematic components using the
 * SchematicItemFactory, places them in a grid layout, and draws AirWireItems
 * (ratsnest lines) to show connectivity between pins.
 *
 * The generated schematic can then be opened in the editor where users
 * manually route wires following the air wire guides.
 */
class NetlistToSchematic {
public:
    struct ConvertResult {
        bool success = false;
        QString outputPath;
        int componentCount = 0;
        int airWireCount = 0;
        QString errorMessage;
    };

    /**
     * @brief Convert a SPICE netlist to a .flxsch schematic file
     * @param netlistPath Path to the input .cir file
     * @param outputPath Path for the output .flxsch file (auto-derived if empty)
     * @return Conversion result with status and statistics
     */
    static ConvertResult convert(const QString& netlistPath, const QString& outputPath = QString());

    /**
     * @brief Convert a SPICE netlist and populate an existing scene
     * @param netlistPath Path to the input .cir file
     * @param scene Scene to populate with components and air wires
     * @return Conversion result
     */
    static ConvertResult convertToScene(const QString& netlistPath, QGraphicsScene* scene);

    static constexpr qreal GRID_SPACING = 150.0;
    static constexpr qreal START_X = -1050.0; // Margin from left edge (-1260)
    static constexpr qreal START_Y = -650.0;  // Margin from top edge (-891)
    static constexpr qreal MAX_X = 1050.0;    // Margin from right edge (+1260)
};

#endif // NETLIST_TO_SCHEMATIC_H
