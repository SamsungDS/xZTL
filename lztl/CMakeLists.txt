cmake_minimum_required(VERSION 2.8)

include_directories("${PROJECT_SOURCE_DIR}/include")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")

set(ZND_HEADER_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/include/lztl.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ztl-media.h
)

set(ZND_SOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ztl-media.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ztl-zmd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ztl-pro.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ztl-pro-grp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ztl-mpe.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ztl-map.c
)

set(LIBZTL "ztl")

add_library(${LIBZTL} STATIC ${ZND_HEADER_FILES} ${ZND_SOURCE_FILES})
target_link_libraries ( ${LIBZTL} ${LXAPP} )
set_target_properties(${LIBZTL} PROPERTIES OUTPUT_NAME ${LIBZTL})
