add_library(FluxSimulator STATIC
    core/sim_netlist.h
    core/sim_netlist.cpp
    core/sim_matrix.h
    core/sim_matrix.cpp
    core/sim_component.h
    core/sim_component.cpp
    core/sim_convergence_assistant.h
    core/sim_convergence_assistant.cpp
    core/sim_engine.h
    core/sim_engine.cpp
    core/sim_sparse_solver_klu.h
    core/sim_sparse_solver_klu.cpp
    bridge/sim_audio_engine.h
    bridge/sim_audio_engine.cpp
    core/sim_model_parser.h
    core/sim_model_parser.cpp
    core/sim_value_parser.h
    core/sim_value_parser.cpp
    core/flux_script_device.h
    core/flux_script_device.cpp
    bridge/sim_schematic_bridge.h
    bridge/sim_schematic_bridge.cpp
    bridge/sim_manager.h
    bridge/sim_manager.cpp
    bridge/flux_script_marshaller.h
    bridge/flux_script_marshaller.cpp
    bridge/model_library_manager.h
    bridge/model_library_manager.cpp
)

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

if(NGSPICE_FOUND)
    target_link_libraries(FluxSimulator PRIVATE ${NGSPICE_LIBRARIES})
    target_include_directories(FluxSimulator PRIVATE ${NGSPICE_INCLUDE_DIRS})
endif()

target_include_directories(FluxSimulator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(BUILD_TESTING)
    add_executable(test_simulator tests/test_basic_circuits.cpp)
    target_link_libraries(test_simulator FluxSimulator)
    add_test(NAME simulator.basic_circuits COMMAND test_simulator)
    set_tests_properties(simulator.basic_circuits PROPERTIES LABELS "fast;simulator")

    add_executable(test_sim_value_parser tests/test_value_parser.cpp)
    target_link_libraries(test_sim_value_parser FluxSimulator Qt6::Core)
    add_test(NAME simulator.value_parser COMMAND test_sim_value_parser)
    set_tests_properties(simulator.value_parser PROPERTIES LABELS "fast;simulator")

    add_executable(test_sim_solver_core tests/test_solver_core.cpp)
    target_link_libraries(test_sim_solver_core FluxSimulator)
    add_test(NAME simulator.solver_core COMMAND test_sim_solver_core)
    set_tests_properties(simulator.solver_core PROPERTIES LABELS "fast;simulator")

    add_executable(test_sim_advanced_device_models tests/test_advanced_device_models.cpp)
    target_link_libraries(test_sim_advanced_device_models FluxSimulator)
    add_test(NAME simulator.advanced_device_models COMMAND test_sim_advanced_device_models)
    set_tests_properties(simulator.advanced_device_models PROPERTIES LABELS "fast;simulator")

    add_executable(test_sim_noise_distortion tests/test_noise_distortion.cpp)
    target_link_libraries(test_sim_noise_distortion FluxSimulator)
    add_test(NAME simulator.noise_distortion COMMAND test_sim_noise_distortion)
    set_tests_properties(simulator.noise_distortion PROPERTIES LABELS "fast;simulator")

    add_executable(test_sim_mixed_signal tests/test_mixed_signal.cpp)
    target_link_libraries(test_sim_mixed_signal FluxSimulator)
    add_test(NAME simulator.mixed_signal COMMAND test_sim_mixed_signal)
    set_tests_properties(simulator.mixed_signal PROPERTIES LABELS "fast;simulator")

    add_executable(test_sim_optimization tests/test_optimization.cpp)
    target_link_libraries(test_sim_optimization FluxSimulator)
    add_test(NAME simulator.optimization COMMAND test_sim_optimization)
    set_tests_properties(simulator.optimization PROPERTIES LABELS "fast;simulator")

    add_executable(test_sim_convergence_assistant tests/test_convergence_assistant.cpp)
    target_link_libraries(test_sim_convergence_assistant FluxSimulator)
    add_test(NAME simulator.convergence_assistant COMMAND test_sim_convergence_assistant)
    set_tests_properties(simulator.convergence_assistant PROPERTIES LABELS "fast;simulator")

    add_executable(test_sim_advanced_probing tests/test_advanced_probing.cpp)
    target_link_libraries(test_sim_advanced_probing FluxSimulator)
    add_test(NAME simulator.advanced_probing COMMAND test_sim_advanced_probing)
    set_tests_properties(simulator.advanced_probing PROPERTIES LABELS "fast;simulator")

    add_executable(test_sim_model_parser tests/test_model_parser.cpp)
    target_link_libraries(test_sim_model_parser FluxSimulator Qt6::Core)
    add_test(NAME simulator.model_parser COMMAND test_sim_model_parser)
    set_tests_properties(simulator.model_parser PROPERTIES LABELS "fast;simulator")

    add_executable(test_sim_parser_fuzz tests/test_parser_fuzz.cpp)
    target_link_libraries(test_sim_parser_fuzz FluxSimulator Qt6::Core)
    add_test(NAME simulator.parser_fuzz COMMAND test_sim_parser_fuzz)
    set_tests_properties(simulator.parser_fuzz PROPERTIES LABELS "fast;simulator;fuzz")

    add_executable(test_pnp_repro tests/test_pnp_repro.cpp)
    target_link_libraries(test_pnp_repro FluxSimulator)

    add_executable(test_bjt_resolution tests/test_bjt_resolution.cpp)
    target_link_libraries(test_bjt_resolution FluxSimulator)
    add_test(NAME simulator.bjt_resolution COMMAND test_bjt_resolution)
    set_tests_properties(simulator.bjt_resolution PROPERTIES LABELS "fast;simulator")

    add_executable(benchmark_simulator_profiles tests/benchmark_simulator_profiles.cpp)
    target_link_libraries(benchmark_simulator_profiles FluxSimulator)
    add_test(
        NAME simulator.profile_harness
        COMMAND benchmark_simulator_profiles --out ${CMAKE_BINARY_DIR}/simulator_profile_harness.json --iterations 20
    )
    set_tests_properties(simulator.profile_harness PROPERTIES LABELS "slow;benchmark;performance;simulator")
endif()
