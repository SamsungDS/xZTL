cmake_minimum_required(VERSION 2.8.3)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")
set(XAPP_VERSION_MAJOR 0)
set(XAPP_VERSION_MINOR 0)
set(XAPP_VERSION_PATCH 1)
set(XAPP_VERSION "${XAPP_VERSION_MAJOR}.${XAPP_VERSION_MINOR}.${XAPP_VERSION_PATCH}")

project(libxapp C)
include(FeatureSummary)
include(use_c11)
include(CheckLibraryExists)
include(CheckFunctionExists)

# versioning
add_definitions(-DXAPP_VERSION_MAJOR=${XAPP_VERSION_MAJOR})
add_definitions(-DXAPP_VERSION_MINOR=${XAPP_VERSION_MINOR})
add_definitions(-DXAPP_VERSION_PATCH=${XAPP_VERSION_PATCH})
add_definitions(-DXAPP_VERSION=${XAPP_VERSION})
add_definitions(-DXAPP_LABEL="X-App: User-space FTL Development Library")

use_c11()
enable_c_flag("-std=c11")
enable_c_flag("-Wall")
#enable_c_flag("-Wextra")
#enable_c_flag("-pedantic")
enable_c_flag("-fPIC")

set(HEADER_FILES
    ${PROJECT_SOURCE_DIR}/include/xapp.h
    ${PROJECT_SOURCE_DIR}/include/xapp-media.h
    ${PROJECT_SOURCE_DIR}/include/xapp-ftl.h
    ${PROJECT_SOURCE_DIR}/include/xapp-front.h
)

set(SOURCE_FILES
    ${PROJECT_SOURCE_DIR}/src/xapp-core.c
)

include_directories("${PROJECT_SOURCE_DIR}/include")

set(LNAME "xapp")

add_library(${LNAME} STATIC ${HEADER_FILES} ${SOURCE_FILES})
set_target_properties(${LNAME} PROPERTIES OUTPUT_NAME ${LNAME})

set(TESTS)
add_subdirectory(tests)
