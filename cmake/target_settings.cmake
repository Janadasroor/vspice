set(VIORAEDA_COMMON_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/symbols
    ${CMAKE_CURRENT_SOURCE_DIR}/schematic
    ${CMAKE_CURRENT_SOURCE_DIR}/schematic/editor
    ${CMAKE_CURRENT_SOURCE_DIR}/schematic/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/schematic/dialogs
    ${CMAKE_CURRENT_SOURCE_DIR}/schematic/items
    ${CMAKE_CURRENT_SOURCE_DIR}/schematic/tools
    ${CMAKE_CURRENT_SOURCE_DIR}/schematic/io
    ${CMAKE_CURRENT_SOURCE_DIR}/schematic/analysis
    ${CMAKE_CURRENT_SOURCE_DIR}/schematic/factories
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/python
    ${CMAKE_CURRENT_SOURCE_DIR}/simulator
    ${CMAKE_CURRENT_SOURCE_DIR}/footprints
    ${CMAKE_CURRENT_SOURCE_DIR}/pcb
)
list(REMOVE_DUPLICATES VIORAEDA_COMMON_INCLUDE_DIRS)

set(VIORAEDA_QT_LINK_LIBS
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::PrintSupport
    Qt${QT_VERSION_MAJOR}::Sql
    Qt${QT_VERSION_MAJOR}::OpenGLWidgets
    Qt${QT_VERSION_MAJOR}::Charts
    Qt${QT_VERSION_MAJOR}::Svg
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Multimedia
    Qt${QT_VERSION_MAJOR}::Qml
)

if(TARGET Qt${QT_VERSION_MAJOR}::WebSockets)
    list(APPEND VIORAEDA_QT_LINK_LIBS Qt${QT_VERSION_MAJOR}::WebSockets)
endif()

set(VIORAEDA_APP_LINK_LIBS
    FluxCore
    FluxSymbols
    FluxSchematicCore
    FluxSchematicUI
    FluxUI
    FluxScript
    FluxFootprints
    VioraPCBCore
    ${VIORAEDA_QT_LINK_LIBS}
)

set(VIORAEDA_CLI_LINK_LIBS
    FluxCore
    FluxSymbols
    FluxSchematicCore
    FluxSchematicUI
    FluxUI
    FluxScript
    FluxFootprints
    VioraPCBCore
    ${VIORAEDA_QT_LINK_LIBS}
)

set(VIORAEDA_PCH_HEADER "${CMAKE_SOURCE_DIR}/cmake/vioraeda_pch.h")

function(vioraeda_configure_module_target target)
    target_include_directories(${target}
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE ${VIORAEDA_COMMON_INCLUDE_DIRS}
    )
    target_link_libraries(${target} PRIVATE ${VIORAEDA_QT_LINK_LIBS} FluxScript)
    if(VIORAEDA_ENABLE_PCH)
        target_precompile_headers(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${VIORAEDA_PCH_HEADER}>")
    endif()
endfunction()

function(vioraeda_configure_app_target target)
    set(options INCLUDE_CLI_DIR)
    cmake_parse_arguments(FLUX "${options}" "" "" ${ARGN})

    target_include_directories(${target} PRIVATE ${VIORAEDA_COMMON_INCLUDE_DIRS})
    if(FLUX_INCLUDE_CLI_DIR)
        target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cli)
    endif()

    target_link_libraries(${target} PRIVATE ${VIORAEDA_APP_LINK_LIBS})
    if(VIORAEDA_ENABLE_PCH)
        target_precompile_headers(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${VIORAEDA_PCH_HEADER}>")
    endif()
endfunction()

function(vioraeda_configure_cli_target target)
    set(options INCLUDE_CLI_DIR)
    cmake_parse_arguments(FLUX "${options}" "" "" ${ARGN})

    target_include_directories(${target} PRIVATE ${VIORAEDA_COMMON_INCLUDE_DIRS})
    if(FLUX_INCLUDE_CLI_DIR)
        target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cli)
    endif()

    target_link_libraries(${target} PRIVATE ${VIORAEDA_CLI_LINK_LIBS})
    if(VIORAEDA_ENABLE_PCH)
        target_precompile_headers(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${VIORAEDA_PCH_HEADER}>")
    endif()
endfunction()
