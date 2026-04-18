set(VIOSYMBOLS_SOURCES ${SYMBOLS_SOURCES})
list(TRANSFORM VIOSYMBOLS_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/../")

add_library(VioSymbols STATIC ${VIOSYMBOLS_SOURCES})
vioraeda_configure_module_target(VioSymbols)
target_link_libraries(VioSymbols PUBLIC VioCore)
