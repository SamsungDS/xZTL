xZTL: Zone Translation Layer User-space Library
===============================================

This library provides an easier access to zoned namespace drives via [xNVMe](https://github.com/OpenMPDK/xNVMe) (>= v0.3)

## Block-Based Access:
  ### Write:
    Applications provide a buffer and a buffer size. xZTL decides where to write in the ZNS drive and returns a list of physical addresses as a list (mapping pieces).
  ### Read:
    Applications read by providing the physical block address as standard block devices.
  ### Info:
    This design requires the recovery of mapping by the application side.

We strongly believe that by centralizing the zone management into a single library, multiple applications will benefit from this design due to a simpler and thinner application backend design.

### Implemented application backend(s):

```
ZRocks: Zoned RocksDB (libzrocks.h)
```


Content
=======

This repository contains:

```bash
# libxztl: Zone Translation Layer User-space Library

     xztl-core.c       (Initialization)
     xztl-ctx.c        (xnvme asynchronous contexts support)
     xztl-groups.c     (grouped zones support)
     xztl-mempool.c    (lock-free memory pool support)
     xztl-prometheus.c (Prometheus support)
     xztl-stats.c      (Statistics support)
     ztl.c	           (Zone translation layer development core)
     ztl-map.c         (In-memory mapping table)
     ztl-media.c       (access to xnvme functions and ZNS devices)
     ztl_metadata.c    (Zone metadata management)
     ztl-mpe.c         (Persistent mapping table TODO)
     ztl-pro-grp.c     (Per group zone provisioning only 1 group for now)
     ztl-pro.c         (Zone provisioning)
     ztl-wca.c         (Write-cache aligned media I/Os from user I/Os)
     ztl-zmd.c         (Zone metadata management NOT IN USE)

# libzrocks: RocksDB Target

     zrocks.c            (target implementation)
     include/libzrocks.h (interface to be used by RocksDB)

# unit tests
     test-media-layer.c     (Test xapp media layer)
     test-mempool.c         (Test xapp memory pool)
     test-znd-media.c       (Test libztl media implementation)
     test-ztl.c             (Test libztl I/O and translation layer)
     test-append-mthread.c  (Test multi-threaded append command)
     test-zrocks.c          (Test ZRocks target)
     test-zrocks-rw.c       (Test ZRocks Write/Read Bandwidth)
     test-zrocks-metadata.c (Test ZRocks metadata)
```

Dependencies
============

Debian: apt install libnuma-dev libcunit1-dev libuuid1

Installation
============

Upgrade your kernel to above 5.14.14 to support character device for ZNS

## Build


- Build and install [xNVMe](https://github.com/OpenMPDK/xNVMe) v0.4

- Build RocksDB db_bench tool with xZTL plugin

  ```shell
  $ git clone https://github.com/facebook/rocksdb.git
  $ cd rocksdb
  $ mkdir -p plugin/xztl
  $ git clone git@github.com:OpenMPDK/xZTL.git plugin/xztl
  $ DEBUG_LEVEL=0 ROCKSDB_PLUGINS=xztl make -j16 db_bench
  ```

## Run

- ### IO back-end option

  In v0.5, the option ***zns*** is enhanced to specify the IO back-end if the kernel supports. The supported IO back-ends are threadpool, io_uring and SPDK.

  ```shell
  Character abstraction
  --zns <dev_path>                    # The default backend is thrpool
  --zns <dev_path>?be=thrpool         # thrpool backend
  --zns <dev_path>?be=io_uring_cmd    # io_uring backend (io_uring_cmd for character device)
  
  SPDK abstraction
  --zns <dev_path>?nsid=N             # e.g. pci:0000:01:00.0?nsid=1
  ```
  
  
  
- ### Run db_bench

  ```shell
  ./db_bench --env_uri=xztl:/dev/ng0n1  --zns /dev/nvme2n1?be=thrpool
  ```
