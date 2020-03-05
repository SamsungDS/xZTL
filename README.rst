libztl: Zone Translation Layer Library
======

Library for fast intergration Application ⬌ ZNS devices

Implemented targets:

  ZRocks: Zoned RocksDB (libzrocks.h)


Content
=======

This repository contain three projects::

  # libxapp: Library Core
     xapp-core.c    (Initialization)
     xapp-ctx.c     (xnvme asynchronous contexts support)
     xapp-groups.c  (grouped zones support)
     xapp-mempool.c (lock-free memory pool support)
     xapp-ztl.c     (zone translation layer development core)

  # libztl: User-space Zone Translation Layer Library
     ztl-map.c     (In-memory mapping table)
     ztl-media.c   (access to xnvme functions and ZNS devices)
     ztl-mpe.c     (Persistent mapping table TODO)
     ztl-pro-grp.c (Per group zone provisioning only 1 group for now)
     ztl-pro.c     (Zone provisioning)
     ztl-wca.c     (Write-cache *not caching now, but it generates media aligned I/Os from user I/Os)
     ztl-zmd.c     (Zone metadata management)

  # libzrocks: RocksDB Target (object interface) 
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

If you want libztl to compile xNVMe for you, type:

  $ make
  $ make install

The library and tests are built in::

  build/ztl     (Core Library)
  build/zrocks  (RocksDB ZRocks target)
  build/tests   (Library Tests)
