add_executable(test_eda_headless headless_eda_test.cpp)

# Link only to headless logic and required Qt core types
target_link_libraries(test_eda_headless PRIVATE
    FluxHeadless
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Test
)

# Since DRCEngine uses DRCViolation/DRCRules from pcb_drc.h, 
# we might need to compile those if they have logic.
# For now, let's see if we can get away with just the headers.
