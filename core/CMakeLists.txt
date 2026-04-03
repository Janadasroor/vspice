# Strip 'core/' prefix from CORE_SOURCES defined in project_sources.cmake
set(LOCAL_CORE_SOURCES ${CORE_SOURCES})
list(TRANSFORM LOCAL_CORE_SOURCES REPLACE "^core/" "")

list(APPEND LOCAL_CORE_SOURCES
    flux_script_engine.cpp
)

add_library(FluxCore STATIC ${LOCAL_CORE_SOURCES})
set_target_properties(FluxCore PROPERTIES AUTOMOC ON)
vioraeda_configure_module_target(FluxCore)
target_link_libraries(FluxCore PUBLIC FluxScript)

# Headless library for Clean Architecture models and engines
set(HEADLESS_SOURCES
    # Schematic models
    ../schematic/models/schematic_item_model.h
    ../schematic/models/wire_model.h
    ../schematic/models/schematic_component_model.h
    ../schematic/models/schematic_page_model.h
    ../schematic/models/erc_engine.h
    ../schematic/analysis/netlist_engine.h
    # PCB models
    ../pcb/models/drc_types.h
    ../pcb/models/drc_types.cpp
    ../pcb/models/trace_model.h
    ../pcb/models/via_model.h
    ../pcb/models/pad_model.h
    ../pcb/models/component_model.h
    ../pcb/models/copper_pour_model.h
    ../pcb/models/board_model.h
    ../pcb/analysis/connectivity_engine.h
    ../pcb/analysis/drc_engine.h
    sync_manager.h
)
add_library(FluxHeadless STATIC ${HEADLESS_SOURCES})
set_target_properties(FluxHeadless PROPERTIES AUTOMOC ON)
target_include_directories(FluxHeadless PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(FluxHeadless PUBLIC Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui)

