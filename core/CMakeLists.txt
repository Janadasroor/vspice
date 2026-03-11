set(FLUXCORE_SOURCES ${CORE_SOURCES})
list(TRANSFORM FLUXCORE_SOURCES PREPEND "${CMAKE_SOURCE_DIR}/")

add_library(FluxPythonBindings STATIC flux_python_bindings.cpp)
set_target_properties(FluxPythonBindings PROPERTIES AUTOMOC ON)
target_link_libraries(FluxPythonBindings PUBLIC FluxSchematicCore FluxCore pybind11::embed Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets)
target_include_directories(FluxPythonBindings PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_library(FluxCore STATIC ${FLUXCORE_SOURCES})
set_target_properties(FluxCore PROPERTIES AUTOMOC ON)
vioraeda_configure_module_target(FluxCore)
if(NGSPICE_FOUND)
    target_link_libraries(FluxCore PRIVATE NGSPICE::ngspice)
endif()

# Headless library for Clean Architecture models and engines
set(HEADLESS_SOURCES
    ../schematic/models/schematic_item_model.h
    ../schematic/models/wire_model.h
    ../schematic/models/schematic_component_model.h
    ../schematic/models/schematic_page_model.h
    ../schematic/models/erc_engine.h
    ../schematic/analysis/netlist_engine.h
    sync_engine.h
)
add_library(FluxHeadless STATIC ${HEADLESS_SOURCES})
set_target_properties(FluxHeadless PROPERTIES AUTOMOC ON)
target_include_directories(FluxHeadless PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(FluxHeadless PUBLIC Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui)
target_link_libraries(FluxCore PRIVATE pybind11::embed)
