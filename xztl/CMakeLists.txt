cmake_minimum_required(VERSION 3.1.0)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

set(ZTL_VERSION_MAJOR 0)
set(ZTL_VERSION_MINOR 1)
set(ZTL_VERSION_PATCH 0)
set(ZNS_OBJ_STORE 0)
set(ZTL_VERSION "${ZTL_VERSION_MAJOR}.${ZTL_VERSION_MINOR}.${ZTL_VERSION_PATCH}")

project(ztl C)
include(FeatureSummary)
include(use_c11)
include(bundle_ztl)
include(CheckLibraryExists)
include(CheckFunctionExists)

# versioning
add_definitions(-DZTL_VERSION_MAJOR=${ZTL_VERSION_MAJOR})
add_definitions(-DZTL_VERSION_MINOR=${ZTL_VERSION_MINOR})
add_definitions(-DZTL_VERSION_PATCH=${ZTL_VERSION_PATCH})
add_definitions(-DZTL_VERSION=${ZTL_VERSION})
add_definitions(-DZTL_LABEL="xZTL: Zone Translation Layer User-space Library")
add_definitions(-DZNS_OBJ_STORE=${ZNS_OBJ_STORE})

use_c11()
enable_c_flag("-std=c11")
enable_c_flag("-Wall")
enable_c_flag("-pedantic")
enable_c_flag("-fopenmp")
enable_c_flag("-fPIC")
#enable_c_flag("-g")

set(HEADER_FILES
    ${PROJECT_SOURCE_DIR}/include/xztl.h
    ${PROJECT_SOURCE_DIR}/include/xztl-media.h
    ${PROJECT_SOURCE_DIR}/include/xztl-mempool.h
    ${PROJECT_SOURCE_DIR}/include/xztl-mods.h
    ${PROJECT_SOURCE_DIR}/include/xztl-pro.h
    ${PROJECT_SOURCE_DIR}/include/xztl-stats.h
    ${PROJECT_SOURCE_DIR}/include/xztl-metadata.h
)

set(SOURCE_FILES
    ${PROJECT_SOURCE_DIR}/src/xztl-core.c
    ${PROJECT_SOURCE_DIR}/src/xztl-mempool.c
    ${PROJECT_SOURCE_DIR}/src/xztl-ctx.c
    ${PROJECT_SOURCE_DIR}/src/xztl-groups.c
    ${PROJECT_SOURCE_DIR}/src/xztl-stats.c
    ${PROJECT_SOURCE_DIR}/src/xztl-prometheus.c
    ${PROJECT_SOURCE_DIR}/src/ztl.c
    ${PROJECT_SOURCE_DIR}/src/ztl-media.c
    ${PROJECT_SOURCE_DIR}/src/ztl-zmd.c
    ${PROJECT_SOURCE_DIR}/src/ztl-pro.c
    ${PROJECT_SOURCE_DIR}/src/ztl-pro-grp.c
    ${PROJECT_SOURCE_DIR}/src/ztl-mpe.c
    ${PROJECT_SOURCE_DIR}/src/ztl-map.c
    ${PROJECT_SOURCE_DIR}/src/ztl-io.c
    ${PROJECT_SOURCE_DIR}/src/ztl-mgmt-cmd.c
    ${PROJECT_SOURCE_DIR}/src/ztl-metadata.c
)

include_directories("${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/zrocks/include/"
)

set(LNAME "ztl")

add_library(${LNAME} STATIC ${HEADER_FILES} ${SOURCE_FILES})
set_target_properties(${LNAME} PROPERTIES OUTPUT_NAME "${LNAME}_slim")
target_compile_options(${LNAME} PUBLIC -MMD)
target_compile_options(${LNAME} PUBLIC -MP)
target_compile_options(${LNAME} PUBLIC -MF)
target_compile_options(${LNAME} PUBLIC -fPIE)

bundle_ztl("${LNAME}" "${LNAME}_bundle")
message(STATUS "bundle_ztl(${BUNDLE_LIBS})")
if ("${dep_path}" STREQUAL "dep_path-NOTFOUND")
	message( FATAL_ERROR "bundling libs failed")
endif()
if ("${BUNDLE_LIBS}" STREQUAL "FAILED")
	message( FATAL_ERROR "bundling libs failed")
endif()

install(FILES ${PROJECT_SOURCE_DIR}/include/xztl.h
	      ${PROJECT_SOURCE_DIR}/include/xztl-media.h
	      ${PROJECT_SOURCE_DIR}/include/xztl.h
	      ${PROJECT_SOURCE_DIR}/include/xztl-mempool.h
	DESTINATION include COMPONENT dev)

install(TARGETS ${LNAME} DESTINATION lib COMPONENT lib)
