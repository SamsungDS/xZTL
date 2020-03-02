PLATFORM_ID = $$( uname -s )
PLATFORM = $$( \
	case $(PLATFORM_ID) in \
		( Linux | FreeBSD | OpenBSD | NetBSD ) echo $(PLATFORM_ID) ;; \
		( * ) echo Unrecognized ;; \
	esac)

CTAGS = $$( \
	case $(PLATFORM_ID) in \
		( Linux ) echo "ctags" ;; \
		( FreeBSD | OpenBSD | NetBSD ) echo "exctags" ;; \
		( * ) echo Unrecognized ;; \
	esac)

MAKE = $$( \
	case $(PLATFORM_ID) in \
		( Linux ) echo "make" ;; \
		( FreeBSD | OpenBSD | NetBSD ) echo "gmake" ;; \
		( * ) echo Unrecognized ;; \
	esac)

NPROC = $$( \
	case $(PLATFORM_ID) in \
		( Linux ) nproc ;; \
		( FreeBSD | OpenBSD | NetBSD ) sysctl -n hw.ncpu ;; \
		( * ) echo Unrecognized ;; \
	esac)

PTARGET = $$( \
	case $(PLATFORM_ID) in \
		( Linux ) echo "linux" ;; \
		( FreeBSD | OpenBSD | NetBSD ) echo "freebsd" ;; \
		( * ) echo Unrecognized ;; \
	esac)

BUILD_DIR?=build

.PHONY: default
default: build

.PHONY: info
info:
	@echo "## meta: make info"
	@echo "cc: $(CC)"
	@echo "platform: $(PLATFORM)"
	@echo "ptarget: $(PTARGET)"
	@echo "make: $(MAKE)"
	@echo "ctags: $(CTAGS)"
	@echo "nproc: $(NPROC)"
	@echo "build_dir: $(BUILD_DIR)"

.PHONY: build-xapp
build-xapp: info
	@echo "## meta: make build-xapp"
	@if [ ! -d "$(BUILD_DIR)/xapp" ]; then	\
		mkdir -p $(BUILD_DIR)/xapp;	\
		cd $(BUILD_DIR)/xapp;		\
		cmake ../../xapp;		\
	fi
	cd $(BUILD_DIR)/xapp && ${MAKE}

.PHONY: build-ztl
build-ztl: info
	@echo "## meta: make build-ztl"
	@if [ ! -d "$(BUILD_DIR)/ztl" ]; then	\
		mkdir -p "$(BUILD_DIR)/ztl";	\
		cd $(BUILD_DIR)/ztl;		\
		cmake ../../ztl;		\
	fi
	cd $(BUILD_DIR)/ztl && ${MAKE}

.PHONY: build-ztl-tgt-zrocks
build-ztl-tgt-zrocks: info
	@echo "## meta: make build-ztl-tgt-zrocks"
	@if [ ! -d "$(BUILD_DIR)/ztl-tgt-zrocks" ]; then	\
		mkdir -p "$(BUILD_DIR)/ztl-tgt-zrocks";		\
		cd $(BUILD_DIR)/ztl-tgt-zrocks;			\
		cmake ../../ztl-tgt-zrocks;			\
	fi
	cd $(BUILD_DIR)/ztl-tgt-zrocks && ${MAKE}

.PHONY: build-tests
build-tests: info
	@echo "## meta: make build-tests"
	@if [ ! -d "$(BUILD_DIR)/tests" ]; then	\
		mkdir -p "$(BUILD_DIR)/tests";	\
		cd $(BUILD_DIR)/tests;		\
		cmake ../../tests;		\
	fi
	cd $(BUILD_DIR)/tests && ${MAKE}

.PHONY: build
build: info deps-xnvme build-xapp build-ztl build-ztl-tgt-zrocks
	@echo "### Eureka!"

.PHONY: install
install:
	@echo "## meta: make install"
	cd $(BUILD_DIR)/ztl-tgt-zrocks && ${MAKE} install

.PHONY: clean
clean:
	@echo "## meta: make clean"
	rm -fr $(BUILD_DIR) || true

#
# THIRD-PARTY DEPENDENCIES
#

#
# xNVMe
#
.PHONY: deps-xnvme-clean
deps-xnvme-clean:
	@echo "## libztl: make deps-xnvme-clean"
	cd third-party/xnvme && ${MAKE} clean || true

.PHONY: deps-xnvme-build
deps-xnvme-build:
	@echo "## libztl: make deps-xnvme-build"
	@echo "# Installing xNVMe dependencies"
	@echo "# Building xNVMe"
	cd third-party/xnvme && ${MAKE} clean && ${MAKE} linux && ${MAKE} deps-liburing && ${MAKE}

.PHONY: deps-xnvme-fetch
deps-xnvme-fetch:
	@echo "## libztl: make deps-xnvme-fetch"
	@echo "# deps-fetch: fetching xnvme"
	@git submodule update --init --recursive
	@echo "# deps-fetch: checkout xnvme"
	cd third-party/xnvme && git checkout ocpv1

.PHONY: deps-xnvme-install
deps-xnvme-install:
	@echo "## libztl: make deps-xnvme-install"
	@echo "# Installing xNVMe"
	cd third-party/xnvme && sudo ${MAKE} install

.PHONY: deps-xnvme
deps-xnvme:
	@echo "## libztl: make deps-xnvme"
	@echo "# Preparing deps (xnvme)"
	@if [ ! -d "third-party/xnvme" ]; then  \
	    mkdir third-party/xnvme;		\
	fi
	@$(MAKE) deps-xnvme-fetch;
	@$(MAKE) deps-xnvme-build
	@$(MAKE) deps-xnvme-install
