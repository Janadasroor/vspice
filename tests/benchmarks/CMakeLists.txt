add_executable(benchmark_flows
    test_benchmark_flows.cpp
    ${VIORAEDA_CLI_MODULE_OBJECTS}
)

vioraeda_configure_cli_target(benchmark_flows)

target_link_libraries(benchmark_flows PRIVATE FluxSimulator)

add_test(
    NAME benchmark.flows
    COMMAND benchmark_flows
            ${CMAKE_SOURCE_DIR}/tests/regression/fixtures
            ${CMAKE_BINARY_DIR}/benchmark_flows.json
)
set_tests_properties(benchmark.flows PROPERTIES LABELS "slow;benchmark;performance")

add_executable(test_re_performance
    re_performance.cpp
    $<TARGET_OBJECTS:FluxReverseEngineering>
    $<TARGET_OBJECTS:FluxCore>
    $<TARGET_OBJECTS:VioraPCBCore>
    $<TARGET_OBJECTS:FluxSymbols>
    $<TARGET_OBJECTS:FluxSchematicCore>
    $<TARGET_OBJECTS:FluxSchematicUI>
    $<TARGET_OBJECTS:FluxUI>
)
vioraeda_configure_cli_target(test_re_performance)
target_link_libraries(test_re_performance PRIVATE Qt${QT_VERSION_MAJOR}::Test)
add_test(NAME benchmark_re_performance COMMAND test_re_performance)
