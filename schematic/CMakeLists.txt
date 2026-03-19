set(FLUXSCHEMATIC_SOURCES ${SCHEMATIC_SOURCES})
list(TRANSFORM FLUXSCHEMATIC_SOURCES PREPEND "${CMAKE_SOURCE_DIR}/")

add_library(FluxSchematic STATIC ${FLUXSCHEMATIC_SOURCES})
set_target_properties(FluxSchematic PROPERTIES AUTOMOC ON)
vioraeda_configure_module_target(FluxSchematic)
target_link_libraries(FluxSchematic PUBLIC FluxCore FluxSymbols FluxSimulator Qt${QT_VERSION_MAJOR}::Concurrent)
target_include_directories(FluxSchematic PRIVATE 
    ${CMAKE_SOURCE_DIR}/simulator/core
    ${CMAKE_SOURCE_DIR}/simulator/bridge
)

# For backward compatibility if other modules use FluxSchematicCore or FluxSchematicUI
add_library(FluxSchematicCore ALIAS FluxSchematic)
add_library(FluxSchematicUI ALIAS FluxSchematic)
