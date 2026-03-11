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
