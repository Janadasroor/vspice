find_package(Qt6 REQUIRED COMPONENTS Core Widgets Quick QuickWidgets)

add_library(FluxScriptManager STATIC
    cpp/core/flux_script_manager.cpp
    cpp/core/flux_script_manager.h
    cpp/dialogs/gemini_dialog.cpp
    cpp/dialogs/gemini_dialog.h
    cpp/gemini/gemini_bridge.cpp
    cpp/gemini/gemini_bridge.h
    cpp/gemini/gemini_panel.cpp
    cpp/gemini/gemini_panel.h
    cpp/dialogs/gemini_instructions_dialog.cpp
    cpp/dialogs/gemini_instructions_dialog.h
)

vioraeda_configure_module_target(FluxScriptManager)

target_include_directories(FluxScriptManager PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/core
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/gemini
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/dialogs
    ${CMAKE_CURRENT_SOURCE_DIR}/../schematic/io
    ${CMAKE_CURRENT_SOURCE_DIR}/../schematic/items
)

target_link_libraries(FluxScriptManager PRIVATE 
    Qt6::Core
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickWidgets
)
