# Footprint Module - Footprint editor and library management
set(FOOTPRINTS_SOURCES
    # Models
    models/footprint_primitive.cpp
    models/footprint_primitive.h
    models/footprint_definition.cpp
    models/footprint_definition.h

    # Items
    items/footprint_primitive_item.cpp
    items/footprint_primitive_item.h

    # Analysis
    analysis/footprint_engine.cpp
    analysis/footprint_engine.h

    # Editor
    footprint_editor.cpp
    footprint_editor.h
    footprint_commands.cpp
    footprint_commands.h
    footprint_library.cpp
    footprint_library.h

    # Importers
    kicad_footprint_importer.cpp
    kicad_footprint_importer.h

    # Wizard
    footprint_wizard.cpp
    footprint_wizard.h

    # UI
    ui/footprint_editor_view.cpp
    ui/footprint_editor_view.h
    ui/footprint_wizard_dialog.cpp
    ui/footprint_wizard_dialog.h
)

add_library(VioFootprints STATIC ${FOOTPRINTS_SOURCES})
set_target_properties(VioFootprints PROPERTIES AUTOMOC ON)
vioraeda_configure_module_target(VioFootprints)

target_link_libraries(VioFootprints PUBLIC
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Sql
    Qt${QT_VERSION_MAJOR}::OpenGL
    Qt${QT_VERSION_MAJOR}::OpenGLWidgets
)
