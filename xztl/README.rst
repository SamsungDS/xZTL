xZTL: Zone Translation Layer User-space Library
===============================================

This library provides an easier access to zoned namespace drives via xNVMe.

## Block-Based Access:
  ### Write:
    Applications provide a buffer and a buffer size. xZTL decides where to write in the ZNS drive and returns
    a list of physical addresses as a list (mapping pieces).
  ### Read:
    Application read by providing the physical block address as standard block devices.
  ### Info:
    This design requires recovery of mapping by the application side.

## Object-Based Access (under development):
  ### Write:
    Applications provide a unique object ID, a buffer, and a buffer size. xZTL decides where to write in the
    ZNS drive and maintains the mapping, returning only a ACK to the application.
  ### Read:
    Applications provide a unique object ID and the offset within the object.
  ### Info:
    This design does not require recovery of mapping by the application because xZTL maintains the mapping and
    recovery of metadata.

We strongly believe that by centralizing the zone management into a single library, multiple applications will benefit from this design due to a simpler and thinner application backend design.

### Implemented application backend(s):

  **ZRocks: Zoned RocksDB (libzrocks.h)**


Content
=======

This repository contains::

  # libxztl: Zone Translation Layer User-space Library
     xztl-core.c       (Initialization)
     xztl-ctx.c        (xnvme asynchronous contexts support)
     xztl-groups.c     (grouped zones support)
     xztl-mempool.c    (lock-free memory pool support)
     xztl-prometheus.c (Prometheus support)
     xztl-stats.c      (Statistics support)
     ztl.c	       (Zone translation layer development core)
     ztl-map.c         (In-memory mapping table)
     ztl-media.c       (access to xnvme functions and ZNS devices)
     ztl-mpe.c         (Persistent mapping table TODO)
     ztl-pro-grp.c     (Per group zone provisioning only 1 group for now)
     ztl-pro.c         (Zone provisioning)
     ztl-wca.c         (Write-cache *not caching now, but it generates media aligned I/Os from user I/Os)
     ztl-zmd.c         (Zone metadata management)

  # libzrocks: RocksDB Target
     zrocks-target.c     (target implementation)
     include/libzrocks.h (interface to be used by RocksDB)

And ``tests``, exercising the projects above.

Dependencies
============

Debian: apt install libnuma-dev libcunit1-dev libuuid1

Installation
============

If you have xNVMe already installed in your system, type:

  $ make lib-only
  $ make install

If you want xZTL to compile xNVMe for you, type:

  $ make
  $ make install

The library and tests are built in::

  build/ztl     (Core Library)
  build/zrocks  (RocksDB ZRocks target)
  build/tests   (Library Tests)
