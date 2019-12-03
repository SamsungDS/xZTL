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

Usage
=====

Type::

  make

The different libraries and their tests are build in::

  build/tests
  build/xapp
  build/ztl
  build/ztl-tgt-zrocks

You can enter the build-directories and install each individual project.
For example::

  cd build/tests
  make install

Will install the tests.
