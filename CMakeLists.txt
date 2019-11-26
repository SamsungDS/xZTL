cmake_minimum_required(VERSION 2.8.3)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")
set(XAPP_VERSION_MAJOR 0)
set(XAPP_VERSION_MINOR 0)
set(XAPP_VERSION_PATCH 1)
set(XAPP_VERSION "${XAPP_VERSION_MAJOR}.${XAPP_VERSION_MINOR}.${XAPP_VERSION_PATCH}")

project(libztl LANGUAGES C CXX)
include(FeatureSummary)
include(use_c11)
include(CheckLibraryExists)
include(CheckFunctionExists)

# versioning
add_definitions(-DXAPP_VERSION_MAJOR=${XAPP_VERSION_MAJOR})
add_definitions(-DXAPP_VERSION_MINOR=${XAPP_VERSION_MINOR})
add_definitions(-DXAPP_VERSION_PATCH=${XAPP_VERSION_PATCH})
add_definitions(-DXAPP_VERSION=${XAPP_VERSION})
add_definitions(-DXAPP_LABEL="libztl: User-space Zone Translation Layer Library")

use_c11()
enable_c_flag("-std=c11")
enable_c_flag("-Wall")
#enable_c_flag("-Wextra")
enable_c_flag("-pedantic")
#enable_c_flag("-fPIC")
enable_c_flag("-fopenmp")
enable_c_flag("-g")

set(HEADER_FILES
    ${PROJECT_SOURCE_DIR}/include/xapp.h
    ${PROJECT_SOURCE_DIR}/include/xapp-media.h
    ${PROJECT_SOURCE_DIR}/include/xapp-mempool.h
    ${PROJECT_SOURCE_DIR}/include/xapp-ztl.h
)

### XAPP CORE ###

set(SOURCE_FILES
    ${PROJECT_SOURCE_DIR}/src/xapp-core.c
    ${PROJECT_SOURCE_DIR}/src/xapp-mempool.c
    ${PROJECT_SOURCE_DIR}/src/xapp-ctx.c
    ${PROJECT_SOURCE_DIR}/src/xapp-ztl.c
    ${PROJECT_SOURCE_DIR}/src/xapp-groups.c
)

include_directories("${PROJECT_SOURCE_DIR}/include")

set(LXAPP "xapp")

add_library(${LXAPP} STATIC ${HEADER_FILES} ${SOURCE_FILES})
set_target_properties(${LXAPP} PROPERTIES OUTPUT_NAME ${LXAPP})

install(FILES "${PROJECT_SOURCE_DIR}/include/xapp.h"
	      "${PROJECT_SOURCE_DIR}/include/xapp-ztl.h"
	      "${PROJECT_SOURCE_DIR}/include/xapp-media.h"
DESTINATION include COMPONENT dev)
install(TARGETS ${LXAPP} DESTINATION lib COMPONENT lib)

add_subdirectory(lztl)
add_subdirectory(targets)
add_subdirectory(tests)
add_subdirectory(scripts)
