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
#enable_c_flag("-g")
enable_c_flag("-ldl")
enable_c_flag("-lm")

include_directories("${PROJECT_SOURCE_DIR}/../include")
include_directories("${PROJECT_SOURCE_DIR}/../zrocks/include")

link_directories("${PROJECT_SOURCE_DIR}/../build/ztl")
link_directories("${PROJECT_SOURCE_DIR}/../build/zrocks")

include_directories("${PROJECT_SOURCE_DIR}/../third-party/xnvme/build")
include_directories("${PROJECT_SOURCE_DIR}/../third-party/xnvme/include")

set(ZTL_TESTS
    ${PROJECT_SOURCE_DIR}/src/test-media-layer.c
    ${PROJECT_SOURCE_DIR}/src/test-znd-media.c
    ${PROJECT_SOURCE_DIR}/src/test-mempool.c
    ${PROJECT_SOURCE_DIR}/src/test-append-mthread.c
    ${PROJECT_SOURCE_DIR}/src/test-ztl.c
)
foreach(SRC_FN ${ZTL_TESTS})
    get_filename_component(SRC_FN_WE ${SRC_FN} NAME_WE)

    set(EXE_FN "${SRC_FN_WE}")
    add_executable(${EXE_FN} ${SRC_FN})
    set_target_properties(${EXE_FN} PROPERTIES
		INTERPROCEDURAL_OPTIMIZATION HAS_IPO)
    target_compile_options(${EXE_FN} PUBLIC -MMD)
    target_compile_options(${EXE_FN} PUBLIC -MP)
    target_compile_options(${EXE_FN} PUBLIC -MF)
    target_compile_options(${EXE_FN} PUBLIC -fPIE)

    target_link_libraries(${EXE_FN} -Wl,--whole-archive)
    target_link_libraries(${EXE_FN} -Wl,--no-as-needed)
    target_link_libraries ( ${EXE_FN} cunit rt uuid numa pthread aio)
    target_link_libraries ( ${EXE_FN} ztl )
    target_link_libraries(${EXE_FN} -Wl,--no-whole-archive)
    target_link_libraries(${EXE_FN} -Wl,--as-needed)
endforeach()

set(ZROCKS_TESTS
    ${PROJECT_SOURCE_DIR}/src/test-zrocks.c
    ${PROJECT_SOURCE_DIR}/src/test-zrocks-rw.c
    ${PROJECT_SOURCE_DIR}/src/test-zrocks-metadata.c
)
foreach(SRC_FN ${ZROCKS_TESTS})
    get_filename_component(SRC_FN_WE ${SRC_FN} NAME_WE)

    set(EXE_FN "${SRC_FN_WE}")
    add_executable(${EXE_FN} ${SRC_FN})
    set_target_properties(${EXE_FN} PROPERTIES
		INTERPROCEDURAL_OPTIMIZATION HAS_IPO)
    target_compile_options(${EXE_FN} PUBLIC -MMD)
    target_compile_options(${EXE_FN} PUBLIC -MP)
    target_compile_options(${EXE_FN} PUBLIC -MF)
    target_compile_options(${EXE_FN} PUBLIC -fPIE)

    target_link_libraries(${EXE_FN} -Wl,--whole-archive)
    target_link_libraries(${EXE_FN} -Wl,--no-as-needed)
    target_link_libraries ( ${EXE_FN} cunit rt uuid numa pthread aio)
    target_link_libraries ( ${EXE_FN} zrocks )
    target_link_libraries(${EXE_FN} -Wl,--no-whole-archive)
    target_link_libraries(${EXE_FN} -Wl,--as-needed)
endforeach()
