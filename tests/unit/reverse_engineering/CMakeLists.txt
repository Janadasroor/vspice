set(RE_TEST_SOURCES
	$<TARGET_OBJECTS:FluxReverseEngineering>
	$<TARGET_OBJECTS:FluxCore>
    $<TARGET_OBJECTS:VioraPCBCore>
    $<TARGET_OBJECTS:FluxSymbols>
    $<TARGET_OBJECTS:FluxSchematicCore>
    $<TARGET_OBJECTS:FluxSchematicUI>
    $<TARGET_OBJECTS:FluxUI>
)

add_executable(test_re_parsers
    test_re_parsers.cpp
    ${RE_TEST_SOURCES}
)
vioraeda_configure_cli_target(test_re_parsers)
target_link_libraries(test_re_parsers PRIVATE Qt${QT_VERSION_MAJOR}::Test)
add_test(NAME unit_re_parsers COMMAND test_re_parsers)

add_executable(test_re_reconstruction
    test_re_reconstruction.cpp
    ${RE_TEST_SOURCES}
)
vioraeda_configure_cli_target(test_re_reconstruction)
target_link_libraries(test_re_reconstruction PRIVATE Qt${QT_VERSION_MAJOR}::Test)
add_test(NAME unit_re_reconstruction COMMAND test_re_reconstruction)
