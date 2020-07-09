xZTL: Zone Translation Layer User-space Library
======

Library for fast intergration Application ⬌ ZNS devices

Implemented targets:

  ZRocks: Zoned RocksDB (libzrocks.h)


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
