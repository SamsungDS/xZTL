Content
=======

This repository contain three projects::

  # libxapp: ???
  xapp

  # libztl: User-space Zone Translation Layer Library
  ztl

  # libzrocks: ???
  ztl-tgt-zrocks

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
