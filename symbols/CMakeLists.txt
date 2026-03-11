set(FLUXSYMBOLS_SOURCES ${SYMBOLS_SOURCES})
list(TRANSFORM FLUXSYMBOLS_SOURCES PREPEND "${CMAKE_SOURCE_DIR}/")

add_library(FluxSymbols STATIC ${FLUXSYMBOLS_SOURCES})
vioraeda_configure_module_target(FluxSymbols)
target_link_libraries(FluxSymbols PUBLIC FluxCore)
