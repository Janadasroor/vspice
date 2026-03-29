add_library(FluxSimulator STATIC
    core/sim_netlist.h
    core/sim_netlist.cpp
    core/sim_meas_evaluator.h
    core/sim_meas_evaluator.cpp
    mixedmode/NetlistManager.h
    mixedmode/NetlistManager.cpp
    mixedmode/LogicComponent.h
    mixedmode/LogicComponent.cpp
    mixedmode/SpiceWorkerThread.h
    mixedmode/SpiceWorkerThread.cpp
    bridge/sim_audio_engine.h
    bridge/sim_audio_engine.cpp
    core/sim_model_parser.h
    core/sim_model_parser.cpp
    core/sim_value_parser.h
    core/sim_value_parser.cpp
    core/raw_data_parser.h
    core/raw_data_parser.cpp
    bridge/sim_schematic_bridge.h
    bridge/sim_schematic_bridge.cpp
    bridge/sim_manager.h
    bridge/sim_manager.cpp
    bridge/model_library_manager.h
    bridge/model_library_manager.cpp
    bridge/flux_script_marshaller.h
    bridge/flux_script_marshaller.cpp
)

if(NOT VIOSPICE_ENABLE_PYTHON)
    set_source_files_properties(
        bridge/flux_script_marshaller.h
        bridge/flux_script_marshaller.cpp
        PROPERTIES HEADER_FILE_ONLY TRUE
    )
endif()

vioraeda_configure_module_target(FluxSimulator)

# Optional KLU Sparse Solver
find_path(KLU_INCLUDE_DIR klu.h)
find_library(KLU_LIBRARY klu)
if(KLU_INCLUDE_DIR AND KLU_LIBRARY)
    message(STATUS "Flux Simulator: KLU detected, enabling high-performance sparse path.")
    target_compile_definitions(FluxSimulator PRIVATE FLUX_HAS_KLU)
    target_include_directories(FluxSimulator PRIVATE ${KLU_INCLUDE_DIR})
    target_link_libraries(FluxSimulator PRIVATE ${KLU_LIBRARY})
else()
    message(STATUS "Flux Simulator: KLU not found, using professional dense fallback.")
endif()

target_link_libraries(FluxSimulator PUBLIC FluxCore FluxSchematicCore Qt${QT_VERSION_MAJOR}::Multimedia Qt${QT_VERSION_MAJOR}::Concurrent)

if(VIOSPICE_ENGINE_FOUND)
    target_link_libraries(FluxSimulator PRIVATE ${VIOSPICE_ENGINE_LIBRARIES})
    target_include_directories(FluxSimulator PRIVATE ${VIOSPICE_ENGINE_INCLUDE_DIRS})
endif()

target_include_directories(FluxSimulator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(BUILD_TESTING)
    add_executable(test_sim_value_parser tests/test_value_parser.cpp)
    target_link_libraries(test_sim_value_parser FluxSimulator Qt6::Core)
    add_test(NAME simulator.value_parser COMMAND test_sim_value_parser)
    set_tests_properties(simulator.value_parser PROPERTIES LABELS "fast;simulator")

    add_executable(test_sim_model_parser tests/test_model_parser.cpp)
    target_link_libraries(test_sim_model_parser FluxSimulator Qt6::Core)
    add_test(NAME simulator.model_parser COMMAND test_sim_model_parser)
    set_tests_properties(simulator.model_parser PROPERTIES LABELS "fast;simulator")

    add_executable(test_sim_parser_fuzz tests/test_parser_fuzz.cpp)
    target_link_libraries(test_sim_parser_fuzz FluxSimulator Qt6::Core)
    add_test(NAME simulator.parser_fuzz COMMAND test_sim_parser_fuzz)
    set_tests_properties(simulator.parser_fuzz PROPERTIES LABELS "fast;simulator;fuzz")
endif()
