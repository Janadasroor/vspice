cmake_minimum_required(VERSION 3.16)

project(viospice VERSION 0.1 LANGUAGES CXX)
enable_testing()
include(CTest)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets PrintSupport Sql OpenGLWidgets Charts Svg Network Multimedia Concurrent Test Qml)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets PrintSupport Sql OpenGLWidgets Charts Svg Network Multimedia Concurrent Test Qml)
find_package(Qt${QT_VERSION_MAJOR} QUIET COMPONENTS WebSockets)
if(TARGET Qt${QT_VERSION_MAJOR}::WebSockets)
    set(VIOSPICE_HAS_QT_WEBSOCKETS ON)
else()
    set(VIOSPICE_HAS_QT_WEBSOCKETS OFF)
    message(WARNING "Qt WebSockets not found. Remote display streaming will be disabled.")
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

option(VIOSPICE_ENABLE_PYTHON "Enable Python support" ON)
if(VIOSPICE_ENABLE_PYTHON)
    include(FetchContent)
    FetchContent_Declare(
      pybind11
      GIT_REPOSITORY https://github.com/pybind/pybind11.git
      GIT_TAG        v2.12.0
    )
    FetchContent_MakeAvailable(pybind11)
endif()



include(cmake/project_sources.cmake)
include(cmake/target_settings.cmake)

# Define FluxSchematicCore and FluxSchematicUI targets
add_subdirectory(schematic)
add_subdirectory(core)
add_subdirectory(symbols)
add_subdirectory(ui)
add_subdirectory(simulator)
if(VIOSPICE_ENABLE_PYTHON)
    add_subdirectory(python)
endif()

if(BUILD_TESTING)
    add_executable(test_spice_directive_netlist schematic/tests/test_spice_directive_netlist.cpp)
    set_target_properties(test_spice_directive_netlist PROPERTIES AUTOMOC ON)
    target_include_directories(test_spice_directive_netlist PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/schematic
        ${CMAKE_CURRENT_SOURCE_DIR}/schematic/items
    )
    target_link_libraries(test_spice_directive_netlist PRIVATE
        FluxSchematic
        FluxSymbols
        FluxSimulator
        FluxCore
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Test
    )
    if(VIOSPICE_ENABLE_PYTHON)
        target_link_libraries(test_spice_directive_netlist PRIVATE FluxPython)
    endif()
    add_test(NAME schematic.spice_directive_netlist COMMAND test_spice_directive_netlist)
endif()

if(BUILD_TESTING AND VIOSPICE_ENABLE_TESTS)
    add_subdirectory(tests)
endif()

option(BUILD_VIOSPICE_CLI "Build the CLI tool" ON)
if(BUILD_VIOSPICE_CLI)
    # CLI tool
    qt_add_executable(vio_cmd
        cli/main.cpp
        resources/resources.qrc
    )

    target_link_libraries(vio_cmd PRIVATE
        FluxSchematicUI
        FluxSchematicCore
        FluxSymbols
        FluxCore
        FluxSimulator
    )

    if(VIOSPICE_ENGINE_FOUND)
        target_link_libraries(vio_cmd PRIVATE ${VIOSPICE_ENGINE_LIBRARIES})
        target_include_directories(vio_cmd PRIVATE ${VIOSPICE_ENGINE_INCLUDE_DIRS})
    endif()

    vioraeda_configure_app_target(vio_cmd)
    set_target_properties(vio_cmd PROPERTIES OUTPUT_NAME "vio-cmd")
endif()

option(BUILD_VIOSPICE_APP "Build the desktop application" ON)
if(BUILD_VIOSPICE_APP)
    # Main executable
    qt_add_executable(viospice
        main.cpp
        resources/resources.qrc
    )

    target_link_libraries(viospice PRIVATE 
        FluxUI
        FluxSchematicUI 
        FluxSchematicCore 
        FluxSymbols 
        FluxSimulator
        FluxCore 
    )

    if(VIOSPICE_ENGINE_FOUND)
        target_link_libraries(viospice PRIVATE ${VIOSPICE_ENGINE_LIBRARIES})
        target_include_directories(viospice PRIVATE ${VIOSPICE_ENGINE_INCLUDE_DIRS})
    endif()

    vioraeda_configure_app_target(viospice)

    set_target_properties(viospice PROPERTIES
        WIN32_EXECUTABLE TRUE
    )

    if(QT_VERSION_MAJOR EQUAL 6)
        qt_finalize_executable(viospice)
    endif()
endif()
