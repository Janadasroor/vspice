add_executable(test_regression_io_golden
    test_io_golden.cpp
    ${VIORAEDA_CLI_MODULE_OBJECTS}
)

vioraeda_configure_cli_target(test_regression_io_golden)

add_test(
    NAME regression.io_golden
    COMMAND test_regression_io_golden ${CMAKE_SOURCE_DIR}/tests/regression/fixtures
)
set_tests_properties(regression.io_golden PROPERTIES LABELS "fast;regression;io")

add_executable(test_regression_cli
    test_cli_regression.cpp
    ${VIORAEDA_CLI_MODULE_OBJECTS}
)

vioraeda_configure_cli_target(test_regression_cli)
target_compile_definitions(test_regression_cli PRIVATE VIO_CMD_PATH=\"$<TARGET_FILE:vio_cmd>\")

add_test(
    NAME regression.cli
    COMMAND test_regression_cli ${CMAKE_SOURCE_DIR}/tests/regression/fixtures
)
set_tests_properties(regression.cli PROPERTIES LABELS "fast;regression;cli")
