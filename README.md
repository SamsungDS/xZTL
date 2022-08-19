xZTL: Zone Translation Layer User-space Library
===============================================

This library provides an easier access to zoned namespace drives via [xNVMe](https://github.com/OpenMPDK/xNVMe) (>= v0.0.3)

## Block-Based Access:
  ### Write:
    Applications provide a buffer and a buffer size. xZTL decides where to write in the ZNS drive and returns a list of physical addresses as a list (mapping pieces).
  ### Read:
    Applications read by providing the physical block address as standard block devices.

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
     ztl.c             (Zone translation layer development core)
     ztl-map.c         (In-memory mapping table)
     ztl-media.c       (access to xnvme functions and ZNS devices)
     ztl_metadata.c    (Zone metadata management)
     ztl-mpe.c         (Persistent mapping table TODO)
     ztl-pro-grp.c     (Per group zone provisioning only 1 group for now)
     ztl-pro.c         (Zone provisioning)
     ztl-io.c          (Write-cache aligned media I/Os from user I/Os)
     ztl-zmd.c         (Zone metadata management NOT IN USE)

# libzrocks: RocksDB Target

     zrocks/src/zrocks.c         (target implementation)
     zrocks/include/libzrocks.h  (interface to be used by RocksDB)

# unit tests
     test-media-layer.c     (Test xapp media layer)
     test-mempool.c         (Test xapp memory pool)
     test-znd-media.c       (Test libztl media implementation)
     test-ztl.c             (Test libztl I/O and translation layer)
     test-append-mthread.c  (Test multi-threaded append command)
     test-zrocks.c          (NOT IN USE)
     test-zrocks-rw.c       (Test ZRocks Write/Read)
     test-zrocks-metadata.c (Test ZRocks metadata)
```

Dependencies
============

Debian: apt install libnuma-dev libcunit1-dev libuuid1

Installation
============

If you don't have xNVMe, please refer to [xNVMe user guide](https://xnvme.io/docs/latest/getting_started/index.html)

If you have xNVMe already installed in your system, type:

```bash
$ make lib-only
$ sudo make install
```

If you want xZTL to compile xNVMe for you, type:

```base
$ make
$ sudo make install
```

The library and tests are built in::

```bash
build/ztl     (Core Library)
build/zrocks  (RocksDB ZRocks target)
build/tests   (Library unit tests)
```

