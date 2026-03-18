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

# --- NGSPICE Support ---
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
set(NGSPICE_ROOT "/home/jnd/cpp_projects/ngspice/release-shared/install-prefix/usr/local" CACHE PATH "Path to NGSPICE installation")
find_package(NGSPICE)
if(NGSPICE_FOUND)
    message(STATUS "NGSPICE found: ${NGSPICE_LIBRARIES}")
    add_compile_definitions(HAVE_NGSPICE)
else()
    message(WARNING "NGSPICE not found. Simulation features using libngspice will be disabled.")
endif()
# -----------------------

include(FetchContent)
FetchContent_Declare(
  pybind11
  GIT_REPOSITORY https://github.com/pybind/pybind11.git
  GIT_TAG        v2.12.0 # Standard stable version
)
FetchContent_MakeAvailable(pybind11)

include(cmake/project_sources.cmake)
include(cmake/target_settings.cmake)

# Define FluxSchematicCore and FluxSchematicUI targets
add_subdirectory(schematic)
add_subdirectory(core)
add_subdirectory(symbols)
add_subdirectory(ui)
add_subdirectory(simulator)
add_subdirectory(python)
add_subdirectory(tests)

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

if(NGSPICE_FOUND)
    target_link_libraries(vio_cmd PRIVATE ${NGSPICE_LIBRARIES})
    target_include_directories(vio_cmd PRIVATE ${NGSPICE_INCLUDE_DIRS})
endif()

vioraeda_configure_app_target(vio_cmd)
set_target_properties(vio_cmd PROPERTIES OUTPUT_NAME "vio-cmd")

# Main executable
qt_add_executable(viospice
    main.cpp
    resources/resources.qrc
)

target_link_libraries(viospice PRIVATE 
    FluxSchematicUI 
    FluxSchematicCore 
    FluxSymbols 
    FluxCore 
    FluxSimulator
)

if(NGSPICE_FOUND)
    target_link_libraries(viospice PRIVATE ${NGSPICE_LIBRARIES})
    target_include_directories(viospice PRIVATE ${NGSPICE_INCLUDE_DIRS})
endif()

vioraeda_configure_app_target(viospice)

set_target_properties(viospice PROPERTIES
    WIN32_EXECUTABLE TRUE
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(viospice)
endif()
