cmake_minimum_required(VERSION 2.8)

include_directories("${PROJECT_SOURCE_DIR}/include")
include_directories("${PROJECT_SOURCE_DIR}/lztl/include")
include_directories("${PROJECT_SOURCE_DIR}/targets/include")

### XAPP Tests ###

set(TEST_HEADER_FILES
    ${PROJECT_SOURCE_DIR}/include/xapp.h
)

set(TEST_SOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/test-media-layer.c
)

foreach(SRC_FN ${TEST_SOURCE_FILES})
    get_filename_component(SRC_FN_WE ${SRC_FN} NAME_WE)
    set(EXE_FN "xapp-${SRC_FN_WE}")
    add_executable(${EXE_FN} ${TEST_HEADER_FILES} ${SRC_FN})
    target_link_libraries ( ${EXE_FN} ${LXAPP} )
    target_link_libraries ( ${EXE_FN} xnvme )
    target_link_libraries ( ${EXE_FN} cunit )
endforeach()

### libztl Tests ###

set(TEST_ZND_SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/test-znd-media.c
    ${CMAKE_CURRENT_SOURCE_DIR}/test-mempool.c
    ${CMAKE_CURRENT_SOURCE_DIR}/test-append-mthread.c
    ${CMAKE_CURRENT_SOURCE_DIR}/test-ztl.c
)

foreach(SRC_FN ${TEST_ZND_SRC_FILES})
    get_filename_component(SRC_FN_WE ${SRC_FN} NAME_WE)
    set(EXE_FN "lztl-${SRC_FN_WE}")
    add_executable(${EXE_FN} ${TEST_HEADER_FILES} ${SRC_FN})
    target_link_libraries ( ${EXE_FN} ${LXAPP} )
    target_link_libraries ( ${EXE_FN} ztl )
    target_link_libraries ( ${EXE_FN} cunit )
    target_link_libraries ( ${EXE_FN} xnvme )
    target_link_libraries ( ${EXE_FN} pthread )
endforeach()
