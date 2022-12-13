$(info $(MAKEFLAGS))
plugin_root := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

$(shell cd $(plugin_root)/xztl && rm -rf log)
$(shell cd $(plugin_root)/xztl && make clean)
$(shell cd $(plugin_root)/xztl && make ztl > log)
$(shell cd $(plugin_root)/xztl && make zrocks > log)
$(shell cd $(plugin_root)/xztl && make install)

xztl_SOURCES = env/env_zns.cc env/env_zns_io.cc
xztl_HEADERS = env/env_zns.h
xztl_CXXFLAGS = -I xztl/zrocks/include/libzrocks.h 
xztl_LDFLAGS = -MMD -MP -MF -fPIE -Wl,--whole-archive -Wl,--no-as-needed -lzrocks -Wl,--no-whole-archive -Wl,--as-needed -luuid -fopenmp -laio -u xztl_env_reg
