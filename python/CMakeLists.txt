find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

add_library(FluxPython STATIC
    python_manager.cpp
    python_manager.h
    gemini_dialog.cpp
    gemini_dialog.h
    gemini_panel.cpp
    gemini_panel.h
)

target_link_libraries(FluxPython PRIVATE 
    Qt6::Core
    Qt6::Widgets
)

target_include_directories(FluxPython PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../core
    ${CMAKE_CURRENT_SOURCE_DIR}/../schematic/io
    ${CMAKE_CURRENT_SOURCE_DIR}/../schematic/items
)
