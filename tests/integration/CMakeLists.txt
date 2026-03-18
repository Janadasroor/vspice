# test_integration_schematic_simulator removed (Internal solver deleted)

add_executable(test_integration_ngspice_shared
    test_ngspice_shared_lib.cpp
    ${VIORAEDA_CLI_MODULE_OBJECTS}
)

target_link_libraries(test_integration_ngspice_shared PRIVATE Qt${QT_VERSION_MAJOR}::Test)
vioraeda_configure_cli_target(test_integration_ngspice_shared)

add_test(
    NAME integration.ngspice_shared
    COMMAND test_integration_ngspice_shared
)
set_tests_properties(integration.ngspice_shared PROPERTIES LABELS "fast;integration;simulator;ngspice")
