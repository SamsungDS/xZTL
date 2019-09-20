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
add_definitions(-DXAPP_LABEL="libxapp: Application-specific FTL Development Library")

use_c11()
enable_c_flag("-std=c11")
enable_c_flag("-Wall")
#enable_c_flag("-Wextra")
enable_c_flag("-pedantic")
enable_c_flag("-fPIC")
enable_c_flag("-fopenmp")

set(SPDK_DEPS
        spdk_nvme
	spdk_net
        spdk_util
	spdk_log
	spdk_env_dpdk
	spdk_app_rpc
	spdk_bdev
	spdk_bdev_aio
	spdk_bdev_delay
	spdk_bdev_error
	spdk_bdev_gpt
	spdk_bdev_lvol
	spdk_bdev_malloc
	spdk_bdev_null
	spdk_bdev_nvme
	spdk_bdev_passthru
	spdk_bdev_raid
	spdk_bdev_rpc
	spdk_bdev_split
	spdk_bdev_virtio
	spdk_blob
	spdk_blob_bdev
	spdk_blobfs
	spdk_conf
	spdk_copy
	spdk_copy_ioat
	spdk_env_dpdk
	spdk_event
	spdk_event_bdev
	spdk_event_copy
	spdk_event_iscsi
	spdk_event_nbd
	spdk_event_net
	spdk_event_nvmf
	spdk_event_scsi
	spdk_event_vhost
	spdk_event_vmd
	spdk_ftl
	spdk_ioat
	spdk_iscsi
	spdk_json
	spdk_jsonrpc
	spdk_log
	spdk_log_rpc
	spdk_lvol
	spdk_nbd
	spdk_net
	spdk_notify
	spdk_nvme
	spdk_nvmf
	spdk_rpc
#	spdk_rte_vhost
	spdk_scsi
	spdk_sock
	spdk_sock_posix
#	spdk_spdk_mock
	spdk_thread
	spdk_trace
#	spdk_trace_rpc
	spdk_ut_mock
	spdk_util
#	spdk_vbdev_error
#	spdk_vbdev_gpt
#	spdk_vbdev_lvol
#	spdk_vbdev_passthru
#	spdk_vbdev_split
	spdk_vhost
	spdk_virtio
	spdk_vmd
	rte_bus_pci
	rte_bus_vdev
	rte_eal
	rte_mbuf
	rte_mempool
	rte_mempool_bucket
	rte_mempool_ring
	rte_pci
	rte_net
	rte_ring
	rte_vhost
	rte_bus_pci
	rte_bus_vdev
	rte_cmdline
	rte_compressdev
	rte_cryptodev
	rte_eal
	rte_ethdev
	rte_hash
	rte_kvargs
	rte_mbuf
	rte_mempool
	rte_mempool_bucket
	rte_mempool_ring
	rte_meter
	rte_net
	rte_pci
	rte_ring
	rte_vhost
	rt
	dl
	numa
	uuid
)

set(HEADER_FILES
    ${PROJECT_SOURCE_DIR}/include/xapp.h
    ${PROJECT_SOURCE_DIR}/include/xapp-media.h
    ${PROJECT_SOURCE_DIR}/include/xapp-mempool.h
#    ${PROJECT_SOURCE_DIR}/include/xapp-mgmt.h
#    ${PROJECT_SOURCE_DIR}/include/xapp-front.h
)

### XAPP CORE ###

set(SOURCE_FILES
    ${PROJECT_SOURCE_DIR}/src/xapp-core.c
    ${PROJECT_SOURCE_DIR}/src/xapp-mempool.c
)

include_directories("${PROJECT_SOURCE_DIR}/include")

set(LXAPP "xapp")

add_library(${LXAPP} STATIC ${HEADER_FILES} ${SOURCE_FILES})
set_target_properties(${LXAPP} PROPERTIES OUTPUT_NAME ${LXAPP})

add_subdirectory(appznd)
add_subdirectory(targets)
add_subdirectory(tests)
