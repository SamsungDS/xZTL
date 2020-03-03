cmake_minimum_required(VERSION 3.1.0)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

project(tests C)
include(FeatureSummary)
include(use_c11)
include(CheckLibraryExists)
include(CheckFunctionExists)

use_c11()
enable_c_flag("-std=c11")
enable_c_flag("-Wall")
enable_c_flag("-pedantic")
enable_c_flag("-fopenmp")
enable_c_flag("-g")

include_directories("${PROJECT_SOURCE_DIR}/../include")
include_directories("${PROJECT_SOURCE_DIR}/../zrocks/include")

link_directories("${PROJECT_SOURCE_DIR}/../build/ztl")
link_directories("${PROJECT_SOURCE_DIR}/../build/zrocks")

set(ZTL_TESTS
    ${PROJECT_SOURCE_DIR}/src/test-media-layer.c
    ${PROJECT_SOURCE_DIR}/src/test-znd-media.c
    ${PROJECT_SOURCE_DIR}/src/test-mempool.c
    ${PROJECT_SOURCE_DIR}/src/test-append-mthread.c
    ${PROJECT_SOURCE_DIR}/src/test-ztl.c
)
foreach(SRC_FN ${ZTL_TESTS})
    get_filename_component(SRC_FN_WE ${SRC_FN} NAME_WE)
    set(EXE_FN "ztl-${SRC_FN_WE}")
    add_executable(${EXE_FN} ${SRC_FN})
    target_link_libraries ( ${EXE_FN} cunit rt uuid numa pthread)
    target_link_libraries ( ${EXE_FN} ztl )
endforeach()

set(ZROCKS_TESTS
    ${PROJECT_SOURCE_DIR}/src/test-zrocks.c
)
foreach(SRC_FN ${ZROCKS_TESTS})
    get_filename_component(SRC_FN_WE ${SRC_FN} NAME_WE)
    set(EXE_FN "zrocks-${SRC_FN_WE}")
    add_executable(${EXE_FN} ${SRC_FN})
    target_link_libraries ( ${EXE_FN} cunit rt uuid numa pthread)
    target_link_libraries ( ${EXE_FN} zrocks )
endforeach()
