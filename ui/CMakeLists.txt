set(FLUXUI_SOURCES ${UI_SOURCES})
list(TRANSFORM FLUXUI_SOURCES PREPEND "${CMAKE_SOURCE_DIR}/")

add_library(FluxUI STATIC ${FLUXUI_SOURCES})
set_target_properties(FluxUI PROPERTIES AUTOMOC ON)
vioraeda_configure_module_target(FluxUI)
target_link_libraries(FluxUI PUBLIC FluxCore FluxSymbols FluxSchematicCore)
