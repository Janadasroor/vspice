set(VIOSCHEMATIC_SOURCES ${SCHEMATIC_SOURCES})
list(TRANSFORM VIOSCHEMATIC_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/../")

add_library(VioSchematic STATIC ${VIOSCHEMATIC_SOURCES})
set_target_properties(VioSchematic PROPERTIES AUTOMOC ON)
vioraeda_configure_module_target(VioSchematic)

target_include_directories(VioSchematic PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/editor
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/dialogs
    ${CMAKE_CURRENT_SOURCE_DIR}/items
    ${CMAKE_CURRENT_SOURCE_DIR}/tools
    ${CMAKE_CURRENT_SOURCE_DIR}/io
    ${CMAKE_CURRENT_SOURCE_DIR}/analysis
    ${CMAKE_CURRENT_SOURCE_DIR}/factories
)

target_link_libraries(VioSchematic PUBLIC VioCore VioSymbols VioSimulator FluxScriptManager VioUI Qt${QT_VERSION_MAJOR}::Concurrent)
target_include_directories(VioSchematic PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../simulator/core
    ${CMAKE_CURRENT_SOURCE_DIR}/../simulator/bridge
)

# New Phase 2 Components
target_sources(VioSchematic PRIVATE
    ui/schematic_minimap.cpp
    ui/schematic_minimap.h
    ui/smart_probe_overlay.cpp
    ui/smart_probe_overlay.h
    ui/smart_probe_engine.cpp
    ui/smart_probe_engine.h
    ui/smith_chart_widget.cpp
    ui/smith_chart_widget.h
)

if(VIOSPICE_ENGINE_FOUND)
    target_include_directories(VioSchematic PRIVATE ${VIOSPICE_ENGINE_INCLUDE_DIRS})
endif()

# For backward compatibility if other modules use VioSchematicCore or VioSchematicUI
add_library(VioSchematicCore ALIAS VioSchematic)
add_library(VioSchematicUI ALIAS VioSchematic)
