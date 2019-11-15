cmake_minimum_required(VERSION 2.8)

include_directories("${PROJECT_SOURCE_DIR}/include")
include_directories("${PROJECT_SOURCE_DIR}/lztl/include")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")

set(ZR_HEADER_FILES
    ${PROJECT_SOURCE_DIR}/include/xapp.h
    ${PROJECT_SOURCE_DIR}/include/xapp-media.h
)

set(ZR_SOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/zrocks-target.c
)

set(ZROCKS "zrocks")

add_library(${ZROCKS} STATIC ${ZR_HEADER_FILES} ${ZR_SOURCE_FILES})
set_target_properties(${ZROCKS} PROPERTIES OUTPUT_NAME ${ZROCKS})
target_link_libraries ( ${ZROCKS} PUBLIC ${LXAPP} )
target_link_libraries ( ${ZROCKS} PUBLIC ztl )
target_link_libraries ( ${ZROCKS} PUBLIC xnvme )
target_link_libraries ( ${ZROCKS} PUBLIC pthread )

install(FILES "${PROJECT_SOURCE_DIR}/targets/include/libzrocks.h"
DESTINATION include COMPONENT dev)

install(TARGETS ${ZROCKS} DESTINATION lib COMPONENT lib)
