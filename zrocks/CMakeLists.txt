cmake_minimum_required(VERSION 3.1.0)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")
set(ZROCKS_VERSION_MAJOR 0)
set(ZROCKS_VERSION_MINOR 1)
set(ZROCKS_VERSION_PATCH 0)
set(ZROCKS_VERSION "${ZROCKS_VERSION_MAJOR}.${ZROCKS_VERSION_MINOR}.${ZROCKS_VERSION_PATCH}")

project(zrocks C)
include(FeatureSummary)
include(use_c11)
include(bundle_zrocks)
include(CheckLibraryExists)
include(CheckFunctionExists)

# versioning
add_definitions(-DZROCKS_VERSION_MAJOR=${ZROCKS_VERSION_MAJOR})
add_definitions(-DZROCKS_VERSION_MINOR=${ZROCKS_VERSION_MINOR})
add_definitions(-DZROCKS_VERSION_PATCH=${ZROCKS_VERSION_PATCH})
add_definitions(-DZROCKS_VERSION=${ZROCKS_VERSION})
add_definitions(-DZROCKS_LABEL="ZRocks: Zoned RocksDB Backend")

use_c11()
enable_c_flag("-std=c11")
enable_c_flag("-Wall")
enable_c_flag("-pedantic")
enable_c_flag("-fopenmp")
enable_c_flag("-fPIC")
#enable_c_flag("-g")

set(HEADER_FILES
	${PROJECT_SOURCE_DIR}/include/libzrocks.h
)

set(SOURCE_FILES
	${PROJECT_SOURCE_DIR}/src/zrocks.c
)

include_directories("${PROJECT_SOURCE_DIR}/include")
include_directories("${PROJECT_SOURCE_DIR}/../third-party/xnvme/build")
include_directories("${PROJECT_SOURCE_DIR}/../third-party/xnvme/include")

set(LNAME "zrocks")

add_library(${LNAME} STATIC ${HEADER_FILES} ${SOURCE_FILES})
set_target_properties(${LNAME} PROPERTIES OUTPUT_NAME "${LNAME}_slim")
target_compile_options(${LNAME} PUBLIC -MMD)
target_compile_options(${LNAME} PUBLIC -MP)
target_compile_options(${LNAME} PUBLIC -MF)
target_compile_options(${LNAME} PUBLIC -fPIE)

bundle_zrocks("${LNAME}" "${LNAME}_bundle")
message(STATUS "bundle_zrocks(${BUNDLE_LIBS})")
if ("${dep_path}" STREQUAL "dep_path-NOTFOUND")
	message( FATAL_ERROR "bundling libs failed")
endif()
if ("${BUNDLE_LIBS}" STREQUAL "FAILED")
	message( FATAL_ERROR "bundling libs failed")
endif()

install(FILES "${PROJECT_SOURCE_DIR}/include/libzrocks.h"
	DESTINATION include COMPONENT dev)

install(TARGETS ${LNAME} DESTINATION lib COMPONENT lib)
