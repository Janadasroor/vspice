cmake_minimum_required(VERSION 3.16)

project(viospice VERSION 0.1 LANGUAGES CXX)
enable_testing()
include(CTest)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets PrintSupport Sql OpenGLWidgets Charts Svg Network Multimedia Concurrent Test Qml Quick QuickWidgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets PrintSupport Sql OpenGLWidgets Charts Svg Network Multimedia Concurrent Test Qml Quick QuickWidgets)
find_package(Qt${QT_VERSION_MAJOR} QUIET COMPONENTS Pdf PdfWidgets)
find_package(Qt${QT_VERSION_MAJOR} QUIET COMPONENTS WebSockets)
if(TARGET Qt${QT_VERSION_MAJOR}::WebSockets)
    set(VIOSPICE_HAS_QT_WEBSOCKETS ON)
else()
    set(VIOSPICE_HAS_QT_WEBSOCKETS OFF)
    message(WARNING "Qt WebSockets not found. Remote display streaming will be disabled.")
endif()
if(TARGET Qt${QT_VERSION_MAJOR}::Pdf AND TARGET Qt${QT_VERSION_MAJOR}::PdfWidgets)
    set(VIOSPICE_HAS_QT_PDF ON)
else()
    set(VIOSPICE_HAS_QT_PDF OFF)
    message(WARNING "Qt Pdf/PdfWidgets not found. PDF viewer will use fallback behavior.")
endif()

# --- VIOSPICE ENGINE Support ---
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
find_package(VIOSPICE_ENGINE)
if(VIOSPICE_ENGINE_FOUND)
    message(STATUS "VIOSPICE ENGINE found: ${VIOSPICE_ENGINE_LIBRARIES}")
    message(STATUS "VIOSPICE ENGINE include dirs: ${VIOSPICE_ENGINE_INCLUDE_DIRS}")
    add_compile_definitions(HAVE_NGSPICE) # Keep for code compatibility
else()
    message(WARNING "VIOSPICE ENGINE not found. Simulation features will be disabled.")
endif()
# -----------------------

# --- nanobind Python Bindings ---
option(VIOSPICE_ENABLE_NANOBIND "Build Python bindings via nanobind" ON)
if(VIOSPICE_ENABLE_NANOBIND)
    find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module)

    # Also embed Python in the main app (Phase 4 — Blender-style integration)
    find_package(Python3 3.10 COMPONENTS Development QUIET)
    if(Python3_FOUND)
        add_compile_definitions(HAVE_PYTHON)
        message(STATUS "Embedded Python enabled: ${Python3_INCLUDE_DIRS}")
    else()
        message(STATUS "Embedded Python DISABLED (Python3 development files not found)")
    endif()

    add_subdirectory(ext/nanobind)

    # Headless solver library (zero Qt, shared by simulator and Python bindings)
    add_library(VioSolverCore STATIC
        simulator/core/sim_value_parser.cpp
        simulator/core/sim_results.h
        simulator/core/sim_netlist.h
        simulator/core/sim_netlist.cpp
        simulator/core/sim_math.h
        simulator/core/sim_expression.h
        simulator/core/sim_meas_evaluator.h
        simulator/core/sim_meas_evaluator.cpp
        simulator/core/sim_model_parser.h
        simulator/core/sim_model_parser.cpp
        simulator/core/raw_data_parser.h
        simulator/core/raw_data_parser.cpp
    )
    target_include_directories(VioSolverCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/simulator)
    set_target_properties(VioSolverCore PROPERTIES
        CXX_STANDARD 17
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
        POSITION_INDEPENDENT_CODE ON
    )
    target_compile_definitions(VioSolverCore PRIVATE VIO_SOLVER_HEADLESS)

    # Python binding module
    nanobind_add_module(vspice_core MODULE LTO NOMINSIZE
        bindings/bindings_core.cpp
    )
    target_include_directories(vspice_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/simulator
        ${CMAKE_CURRENT_SOURCE_DIR}/simulator/core
        ${CMAKE_CURRENT_SOURCE_DIR}/bindings
    )
    target_link_libraries(vspice_core PRIVATE VioSolverCore)
    # Output as "_core" inside the vspice/ Python package
    set_target_properties(vspice_core PROPERTIES
        OUTPUT_NAME "_core"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python/vspice"
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    message(STATUS "nanobind Python bindings enabled: output → ${CMAKE_BINARY_DIR}/python/vspice")
endif()
# -----------------------

# --- FluxScript Support ---
set(FLUX_BUILD_EDITOR ON CACHE BOOL "Build the FluxEditor GUI module" FORCE)
set(VIOSPICE_ENABLE_PYTHON ON CACHE BOOL "Enable Python-backed editor features" FORCE)
add_subdirectory(../fluxscript fluxscript_build)
# -----------------------

include(cmake/project_sources.cmake)
include(cmake/target_settings.cmake)

# Define FluxSchematicCore and FluxSchematicUI targets
add_subdirectory(schematic)
add_subdirectory(core)
add_subdirectory(symbols)
add_subdirectory(ui)
add_subdirectory(simulator)
add_subdirectory(python)
add_subdirectory(footprints)
add_subdirectory(pcb)

if(BUILD_TESTING)
    add_executable(test_spice_directive_netlist schematic/tests/test_spice_directive_netlist.cpp)
    set_target_properties(test_spice_directive_netlist PROPERTIES AUTOMOC ON)
    target_include_directories(test_spice_directive_netlist PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/schematic
        ${CMAKE_CURRENT_SOURCE_DIR}/schematic/items
    )
    target_link_libraries(test_spice_directive_netlist PRIVATE
        VioSchematic
        VioSymbols
        VioSimulator
        VioCore
        VioUI
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Test
    )
    add_test(NAME schematic.spice_directive_netlist COMMAND test_spice_directive_netlist)

    # Design Rule Management System Tests
    add_executable(test_design_rule
        tests/test_design_rule.cpp
        tests/test_design_rule.h
    )
    set_target_properties(test_design_rule PROPERTIES AUTOMOC ON)
    target_link_libraries(test_design_rule PRIVATE
        VioCore
        VioSymbols
        VioSimulator
        FluxScript
        FluxScriptManager
        VioUI
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Test
    )
    add_test(NAME core.design_rule COMMAND test_design_rule)
endif()

if(BUILD_TESTING AND VIOSPICE_ENABLE_TESTS)
    add_subdirectory(tests)
endif()

option(BUILD_VIOSPICE_CLI "Build the CLI tool" ON)
if(BUILD_VIOSPICE_CLI)
    # CLI tool
    qt_add_executable(viora
        cli/main.cpp
        cli/flux_command.cpp
        resources/resources.qrc
    )

    target_link_libraries(viora PRIVATE
        VioSchematicUI
        VioSchematicCore
        VioSymbols
        VioCore
        VioSimulator
        FluxScript
        FluxScriptManager
        VioUI
        VioFootprints
        VioraPCBCore
    )
    target_compile_definitions(viora PRIVATE HAVE_FLUXSCRIPT)


    if(VIOSPICE_ENGINE_FOUND)
        target_link_libraries(viora PRIVATE ${VIOSPICE_ENGINE_LIBRARIES})
        target_include_directories(viora PRIVATE ${VIOSPICE_ENGINE_INCLUDE_DIRS})
        # Set rpath so viora finds VioMATRIXC's libngspice at runtime
        get_filename_component(_engine_lib_dir "${VIOSPICE_ENGINE_LIBRARY}" DIRECTORY)
        set_target_properties(viora PROPERTIES
            BUILD_RPATH "${_engine_lib_dir}"
            BUILD_RPATH_USE_ORIGIN TRUE
            INSTALL_RPATH "$ORIGIN/../lib"
        )
    endif()

    vioraeda_configure_app_target(viora)
    
    # Automatically link viora CLI globally in user's local bin after every build
    add_custom_command(TARGET viora POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$ENV{HOME}/.local/bin"
        COMMAND ${CMAKE_COMMAND} -E create_symlink "$<TARGET_FILE:viora>" "$ENV{HOME}/.local/bin/viora"
        COMMENT "Linking viora CLI to ~/.local/bin/viora"
    )
endif()

option(BUILD_VIOSPICE_APP "Build the desktop application" ON)
if(BUILD_VIOSPICE_APP)
    # Main executable
    qt_add_executable(viospice
        main.cpp
        core/python_embed.cpp
        resources/resources.qrc
    )

    target_link_libraries(viospice PRIVATE
        VioUI
        VioSchematicUI
        VioSchematicCore
        VioSymbols
        VioSimulator
        VioCore
        VioUI # Link twice if there are circular dependencies
        FluxScript
        FluxScriptManager
        VioFootprints
        VioraPCBCore
    )


    if(VIOSPICE_ENGINE_FOUND)
        target_link_libraries(viospice PRIVATE ${VIOSPICE_ENGINE_LIBRARIES})
        target_include_directories(viospice PRIVATE ${VIOSPICE_ENGINE_INCLUDE_DIRS})
        # Set rpath so viospice finds VioMATRIXC's libngspice at runtime
        get_filename_component(_engine_lib_dir "${VIOSPICE_ENGINE_LIBRARY}" DIRECTORY)
        set_target_properties(viospice PROPERTIES
            BUILD_RPATH "${_engine_lib_dir}"
            BUILD_RPATH_USE_ORIGIN TRUE
            INSTALL_RPATH "$ORIGIN/../lib"
        )
    endif()

    # Link embedded Python runtime (Phase 4)
    if(Python3_FOUND)
        target_link_libraries(viospice PRIVATE Python3::Python)
        target_include_directories(viospice PRIVATE ${Python3_INCLUDE_DIRS})
    endif()

    vioraeda_configure_app_target(viospice)

    # Disable AUTOUIC for files that include headers without .ui files
    set_source_files_properties(main.cpp PROPERTIES SKIP_AUTOUIC ON)

    set_target_properties(viospice PROPERTIES
        WIN32_EXECUTABLE TRUE
    )

    if(QT_VERSION_MAJOR EQUAL 6)
        qt_finalize_executable(viospice)
    endif()
endif()

# Semantic merge tool (for Git integration)
add_executable(viospice-merge
    tools/viospice_merge.cpp
)
target_link_libraries(viospice-merge PRIVATE
    VioCore
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Widgets
)
set_target_properties(viospice-merge PROPERTIES
    CXX_STANDARD 17
)
