add_executable(test_integration_schematic_simulator
    test_schematic_simulator.cpp
    ${VIORAEDA_CLI_MODULE_OBJECTS}
)

add_executable(test_integration_ngspice_shared
    test_ngspice_shared_lib.cpp
    ${VIORAEDA_CLI_MODULE_OBJECTS}
)

target_link_libraries(test_integration_ngspice_shared PRIVATE Qt${QT_VERSION_MAJOR}::Test)

vioraeda_configure_cli_target(test_integration_schematic_simulator)
vioraeda_configure_cli_target(test_integration_ngspice_shared)

add_test(
    NAME integration.schematic_simulator
    COMMAND test_integration_schematic_simulator
)
set_tests_properties(integration.schematic_simulator PROPERTIES LABELS "fast;integration;simulator")

add_test(
    NAME integration.ngspice_shared
    COMMAND test_integration_ngspice_shared
)
set_tests_properties(integration.ngspice_shared PROPERTIES LABELS "fast;integration;simulator;ngspice")
