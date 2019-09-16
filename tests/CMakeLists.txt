cmake_minimum_required(VERSION 2.8)

#if (NOT TESTS)
#    return()
#endif()

include_directories("${PROJECT_SOURCE_DIR}/include")

set(TEST_SOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/test-media-layer.c
)

foreach(SRC_FN ${TEST_SOURCE_FILES})
    get_filename_component(SRC_FN_WE ${SRC_FN} NAME_WE)
    set(EXE_FN "xapp-${SRC_FN_WE}")
    add_executable(${EXE_FN} ${SRC_FN})
    target_link_libraries ( ${EXE_FN} ${LNAME} )
    target_link_libraries ( ${EXE_FN} cunit )
endforeach()
