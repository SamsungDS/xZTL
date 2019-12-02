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

include_directories("${PROJECT_SOURCE_DIR}/../xapp/include")
include_directories("${PROJECT_SOURCE_DIR}/../ztl/include")
include_directories("${PROJECT_SOURCE_DIR}/../ztl-tgt-zrocks/include")

link_directories("${PROJECT_SOURCE_DIR}/../build/xapp")
link_directories("${PROJECT_SOURCE_DIR}/../build/ztl")
link_directories("${PROJECT_SOURCE_DIR}/../build/ztl-tgt-zrocks")

set(XAPP_TESTS
    ${PROJECT_SOURCE_DIR}/src/test-media-layer.c
)
foreach(SRC_FN ${XAPP_TESTS})
    get_filename_component(SRC_FN_WE ${SRC_FN} NAME_WE)
    set(EXE_FN "xapp-${SRC_FN_WE}")
    add_executable(${EXE_FN} ${SRC_FN})
    target_link_libraries ( ${EXE_FN} cunit )
    target_link_libraries ( ${EXE_FN} xapp )
endforeach()

set(ZTL_TESTS
    ${PROJECT_SOURCE_DIR}/src/test-znd-media.c
    ${PROJECT_SOURCE_DIR}/src/test-mempool.c
    ${PROJECT_SOURCE_DIR}/src/test-append-mthread.c
    ${PROJECT_SOURCE_DIR}/src/test-ztl.c
)
foreach(SRC_FN ${ZTL_TESTS})
    get_filename_component(SRC_FN_WE ${SRC_FN} NAME_WE)
    set(EXE_FN "lztl-${SRC_FN_WE}")
    add_executable(${EXE_FN} ${SRC_FN})
    target_link_libraries ( ${EXE_FN} cunit )
    target_link_libraries ( ${EXE_FN} pthread )
    target_link_libraries ( ${EXE_FN} ztl )
endforeach()

set(ZROCKS_TESTS
    ${PROJECT_SOURCE_DIR}/src/test-zrocks.c
)
foreach(SRC_FN ${ZTL_TESTS})
    get_filename_component(SRC_FN_WE ${SRC_FN} NAME_WE)
    set(EXE_FN "zrocks-${SRC_FN_WE}")
    add_executable(${EXE_FN} ${SRC_FN})
    target_link_libraries ( ${EXE_FN} cunit )
    target_link_libraries ( ${EXE_FN} pthread )
    target_link_libraries ( ${EXE_FN} zrocks )
endforeach()
