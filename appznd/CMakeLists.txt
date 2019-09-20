cmake_minimum_required(VERSION 2.8)

include_directories("${PROJECT_SOURCE_DIR}/include")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")

set(ZND_HEADER_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/include/znd-media.h
)

set(ZND_SOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/znd-media.c
)

set(APPZND "appznd")

add_library(${APPZND} STATIC ${ZND_HEADER_FILES} ${ZND_SOURCE_FILES})
target_link_libraries ( ${APPZND} ${LXAPP} )
set_target_properties(${APPZND} PROPERTIES OUTPUT_NAME ${APPZND})
