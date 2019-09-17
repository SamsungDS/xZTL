cmake_minimum_required(VERSION 2.8)

include_directories("${PROJECT_SOURCE_DIR}/include")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")

set(ZN_HEADER_FILES
    ${PROJECT_SOURCE_DIR}/include/xapp.h
    ${PROJECT_SOURCE_DIR}/include/xapp-media.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/zn-media.h
)

set(ZN_SOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/zn-media.c
)

set(ZNMEDIA "znd-media")

add_library(${ZNMEDIA} STATIC ${ZN_HEADER_FILES} ${ZN_SOURCE_FILES})
target_link_libraries ( ${ZNMEDIA} ${LXAPP} )
set_target_properties(${ZNMEDIA} PROPERTIES OUTPUT_NAME ${ZNMEDIA})
