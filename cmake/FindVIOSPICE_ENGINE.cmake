# FindVIOSPICE_ENGINE.cmake
# Find the ngspice shared library and headers

set(VIOSPICE_ENGINE_ROOT "/home/jnd/cpp_projects/VioMATRIXC" CACHE PATH "Path to VioMATRIXC root")

# Check for library
set(VIOSPICE_ENGINE_LIBRARY "${VIOSPICE_ENGINE_ROOT}/releasesh/src/.libs/libngspice.so")
if(NOT EXISTS "${VIOSPICE_ENGINE_LIBRARY}")
    set(VIOSPICE_ENGINE_LIBRARY "${VIOSPICE_ENGINE_ROOT}/release/src/.libs/libngspice.so")
endif()
if(NOT EXISTS "${VIOSPICE_ENGINE_LIBRARY}")
    unset(VIOSPICE_ENGINE_LIBRARY)
endif()
if(NOT EXISTS "${VIOSPICE_ENGINE_LIBRARY}")
    find_library(VIOSPICE_ENGINE_LIBRARY NAMES ngspice libngspice.so
        PATHS /usr/local/lib /usr/lib /usr/lib64
        NO_DEFAULT_PATH)
endif()

# Check for include directory
set(VIOSPICE_ENGINE_INCLUDE_DIR "${VIOSPICE_ENGINE_ROOT}/src/include")
if(NOT EXISTS "${VIOSPICE_ENGINE_INCLUDE_DIR}/ngspice/sharedspice.h")
    unset(VIOSPICE_ENGINE_INCLUDE_DIR)
    find_path(VIOSPICE_ENGINE_INCLUDE_DIR ngspice/sharedspice.h
        PATHS /usr/local/include /usr/include
        NO_DEFAULT_PATH)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(VIOSPICE_ENGINE DEFAULT_MSG VIOSPICE_ENGINE_LIBRARY VIOSPICE_ENGINE_INCLUDE_DIR)

if(VIOSPICE_ENGINE_FOUND)
    set(VIOSPICE_ENGINE_LIBRARIES ${VIOSPICE_ENGINE_LIBRARY})
    set(VIOSPICE_ENGINE_INCLUDE_DIRS ${VIOSPICE_ENGINE_INCLUDE_DIR})
    
    message(STATUS "NGSPICE engine found!")
    message(STATUS "  Library: ${VIOSPICE_ENGINE_LIBRARIES}")
    message(STATUS "  Include: ${VIOSPICE_ENGINE_INCLUDE_DIRS}")
    
    if(NOT TARGET VIOSPICE_ENGINE::viospice)
        add_library(VIOSPICE_ENGINE::viospice UNKNOWN IMPORTED)
        set_target_properties(VIOSPICE_ENGINE::viospice PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${VIOSPICE_ENGINE_INCLUDE_DIRS}"
            IMPORTED_LOCATION "${VIOSPICE_ENGINE_LIBRARIES}"
        )
    endif()
endif()

mark_as_advanced(VIOSPICE_ENGINE_INCLUDE_DIR VIOSPICE_ENGINE_LIBRARY)
