cmake_minimum_required(VERSION 3.1.0)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")
set(XAPP_VERSION_MAJOR 0)
set(XAPP_VERSION_MINOR 0)
set(XAPP_VERSION_PATCH 6)
set(XAPP_VERSION "${XAPP_VERSION_MAJOR}.${XAPP_VERSION_MINOR}.${XAPP_VERSION_PATCH}")

project(xapp C)
include(FeatureSummary)
include(use_c11)
include(bundle_xapp)
include(CheckLibraryExists)
include(CheckFunctionExists)

# versioning
add_definitions(-DXAPP_VERSION_MAJOR=${XAPP_VERSION_MAJOR})
add_definitions(-DXAPP_VERSION_MINOR=${XAPP_VERSION_MINOR})
add_definitions(-DXAPP_VERSION_PATCH=${XAPP_VERSION_PATCH})
add_definitions(-DXAPP_VERSION=${XAPP_VERSION})
add_definitions(-DXAPP_LABEL="xapp: ztl library core")

use_c11()
enable_c_flag("-std=c11")
enable_c_flag("-Wall")
enable_c_flag("-pedantic")
enable_c_flag("-fopenmp")
#enable_c_flag("-g")

set(HEADER_FILES
    ${PROJECT_SOURCE_DIR}/include/xapp.h
    ${PROJECT_SOURCE_DIR}/include/xapp-media.h
    ${PROJECT_SOURCE_DIR}/include/xapp-mempool.h
    ${PROJECT_SOURCE_DIR}/include/xapp-ztl.h
)

set(SOURCE_FILES
    ${PROJECT_SOURCE_DIR}/src/xapp-core.c
    ${PROJECT_SOURCE_DIR}/src/xapp-mempool.c
    ${PROJECT_SOURCE_DIR}/src/xapp-ctx.c
    ${PROJECT_SOURCE_DIR}/src/xapp-ztl.c
    ${PROJECT_SOURCE_DIR}/src/xapp-groups.c
    ${PROJECT_SOURCE_DIR}/src/xapp-stats.c
    ${PROJECT_SOURCE_DIR}/src/xapp-prometheus.c
)

include_directories("${PROJECT_SOURCE_DIR}/include")

set(LNAME "xapp")

add_library(${LNAME} STATIC ${HEADER_FILES} ${SOURCE_FILES})
set_target_properties(${LNAME} PROPERTIES OUTPUT_NAME "${LNAME}_slim")

bundle_xapp("${LNAME}" "${LNAME}_bundle")
message(STATUS "bundle_xapp(${BUNDLE_LIBS})")
if ("${dep_path}" STREQUAL "dep_path-NOTFOUND")
	message( FATAL_ERROR "bundling libs failed")
endif()
if ("${BUNDLE_LIBS}" STREQUAL "FAILED")
	message( FATAL_ERROR "bundling libs failed")
endif()

install(FILES "${PROJECT_SOURCE_DIR}/include/xapp.h"
	DESTINATION include COMPONENT dev)
install(FILES "${PROJECT_SOURCE_DIR}/include/xapp-ztl.h"
	DESTINATION include COMPONENT dev)
install(FILES "${PROJECT_SOURCE_DIR}/include/xapp-media.h"
	DESTINATION include COMPONENT dev)
install(TARGETS ${LNAME} DESTINATION lib COMPONENT lib)
