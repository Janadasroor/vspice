add_executable(test_ux_logic
    test_ux_logic.cpp
)

vioraeda_configure_cli_target(test_ux_logic)
target_link_libraries(test_ux_logic PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    FluxSchematic
    FluxUI
    FluxCore
    FluxSymbols
)

add_test(NAME unit_ux_logic COMMAND test_ux_logic)
set_tests_properties(unit_ux_logic PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
