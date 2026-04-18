add_library(VioSimulator STATIC
    core/sim_netlist.h
    core/sim_netlist.cpp
    core/sim_meas_evaluator.h
    core/sim_meas_evaluator.cpp
    core/sim_net_evaluator.h
    core/sim_net_evaluator.cpp
    core/sim_report_generator.h
    core/sim_report_generator.cpp
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
    core/sim_rf_math.h
    core/sim_rf_math.cpp
    bridge/sim_schematic_bridge.h
    bridge/sim_schematic_bridge.cpp
    bridge/sim_manager.h
    bridge/sim_manager.cpp
    bridge/model_library_manager.h
    bridge/model_library_manager.cpp
    bridge/spice_model_search.h
    bridge/spice_model_search.cpp
    bridge/flux_sim_bridge.h
    bridge/flux_sim_bridge.cpp
    bridge/flux_script_marshaller.h
)

if(NOT VIOSPICE_ENABLE_PYTHON)
    set_source_files_properties(
        bridge/flux_script_marshaller.h
        PROPERTIES HEADER_FILE_ONLY TRUE
    )
endif()

vioraeda_configure_module_target(VioSimulator)

# Optional KLU Sparse Solver
find_path(KLU_INCLUDE_DIR klu.h)
find_library(KLU_LIBRARY klu)
if(KLU_INCLUDE_DIR AND KLU_LIBRARY)
    message(STATUS "Vio Simulator: KLU detected, enabling high-performance sparse path.")
    target_compile_definitions(VioSimulator PRIVATE VIO_HAS_KLU)
    target_include_directories(VioSimulator PRIVATE ${KLU_INCLUDE_DIR})
    target_link_libraries(VioSimulator PRIVATE ${KLU_LIBRARY})
else()
    message(STATUS "Vio Simulator: KLU not found, using professional dense fallback.")
endif()

target_link_libraries(VioSimulator PUBLIC VioCore VioSchematicCore Qt${QT_VERSION_MAJOR}::Multimedia Qt${QT_VERSION_MAJOR}::Concurrent)

if(VIOSPICE_ENGINE_FOUND)
    target_link_libraries(VioSimulator PRIVATE ${VIOSPICE_ENGINE_LIBRARIES})
    target_include_directories(VioSimulator PRIVATE ${VIOSPICE_ENGINE_INCLUDE_DIRS})
endif()

target_include_directories(VioSimulator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(BUILD_TESTING)
    add_executable(test_sim_value_parser tests/test_value_parser.cpp)
    target_link_libraries(test_sim_value_parser VioSimulator Qt6::Core)
    add_test(NAME simulator.value_parser COMMAND test_sim_value_parser)
    set_tests_properties(simulator.value_parser PROPERTIES LABELS "fast;simulator")

    add_executable(test_sim_model_parser tests/test_model_parser.cpp)
    target_link_libraries(test_sim_model_parser VioSimulator Qt6::Core)
    add_test(NAME simulator.model_parser COMMAND test_sim_model_parser)
    set_tests_properties(simulator.model_parser PROPERTIES LABELS "fast;simulator")

    add_executable(test_sim_parser_fuzz tests/test_parser_fuzz.cpp)
    target_link_libraries(test_sim_parser_fuzz VioSimulator Qt6::Core)
    add_test(NAME simulator.parser_fuzz COMMAND test_sim_parser_fuzz)
    set_tests_properties(simulator.parser_fuzz PROPERTIES LABELS "fast;simulator;fuzz")

    add_executable(test_sim_rf_math tests/test_rf_math.cpp)
    target_link_libraries(test_sim_rf_math VioSimulator Qt6::Core Qt6::Test)
    add_test(NAME simulator.rf_math COMMAND test_sim_rf_math)
    set_tests_properties(simulator.rf_math PROPERTIES LABELS "fast;simulator;rf")
endif()
