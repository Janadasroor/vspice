set(VIOUI_SOURCES ${UI_SOURCES})
list(TRANSFORM VIOUI_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/../")

# Add UI Command Server files
list(APPEND VIOUI_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/ui_command_server.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui_command_server.cpp"
)

# Add Python REPL Console files
list(APPEND VIOUI_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/python_console_widget.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/python_console_widget.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/python_executor.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/python_executor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/python_hooks.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/python_hooks.cpp"
)

add_library(VioUI STATIC ${VIOUI_SOURCES})
set_target_properties(VioUI PROPERTIES AUTOMOC ON)
vioraeda_configure_module_target(VioUI)
target_link_libraries(VioUI PUBLIC VioCore VioSymbols VioSchematic VioFootprints VioraPCBCore)
target_link_libraries(VioUI PRIVATE Qt${QT_VERSION_MAJOR}::WebSockets)

# Enable Python embedding in UI components
if(Python3_FOUND)
    target_compile_definitions(VioUI PRIVATE HAVE_PYTHON)
    target_include_directories(VioUI PRIVATE ${Python3_INCLUDE_DIRS})
    target_link_libraries(VioUI PRIVATE Python3::Python)
endif()

# Disable AUTOUIC for files without .ui files
set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/ui_command_server.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/python_executor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/python_hooks.cpp"
    PROPERTIES SKIP_AUTOUIC ON
)
