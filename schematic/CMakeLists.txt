set(FLUXSCHEMATIC_SOURCES ${SCHEMATIC_SOURCES})
list(TRANSFORM FLUXSCHEMATIC_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/../")

add_library(FluxSchematic STATIC ${FLUXSCHEMATIC_SOURCES})
set_target_properties(FluxSchematic PROPERTIES AUTOMOC ON)
vioraeda_configure_module_target(FluxSchematic)
target_link_libraries(FluxSchematic PUBLIC FluxCore FluxSymbols FluxSimulator Qt${QT_VERSION_MAJOR}::Concurrent)
target_include_directories(FluxSchematic PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../simulator/core
    ${CMAKE_CURRENT_SOURCE_DIR}/../simulator/bridge
)

# New Phase 2 Components
target_sources(FluxSchematic PRIVATE
    ui/schematic_minimap.cpp
    ui/schematic_minimap.h
    ui/smart_probe_overlay.cpp
    ui/smart_probe_overlay.h
    ui/smart_probe_engine.cpp
    ui/smart_probe_engine.h
    dialogs/circuit_template_gallery.cpp
    dialogs/circuit_template_gallery.h
)

if(VIOSPICE_ENGINE_FOUND)
    target_include_directories(FluxSchematic PRIVATE ${VIOSPICE_ENGINE_INCLUDE_DIRS})
endif()

# For backward compatibility if other modules use FluxSchematicCore or FluxSchematicUI
add_library(FluxSchematicCore ALIAS FluxSchematic)
add_library(FluxSchematicUI ALIAS FluxSchematic)
