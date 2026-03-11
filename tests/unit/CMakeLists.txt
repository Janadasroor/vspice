# Auto-discover unit test subdirectories.
file(GLOB unit_entries RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)

foreach(entry IN LISTS unit_entries)
    if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${entry} AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${entry}/CMakeLists.txt)
        add_subdirectory(${entry})
    endif()
endforeach()
