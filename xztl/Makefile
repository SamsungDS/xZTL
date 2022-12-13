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
default: all

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

.PHONY: ztl
ztl: info
	@echo "## meta: make ztl"
	@if [ ! -d "$(BUILD_DIR)/ztl" ]; then	\
		mkdir -p "$(BUILD_DIR)/ztl";	\
		cd $(BUILD_DIR)/ztl;		\
		cmake ../..;			\
	fi
	@if [ ! -d "third-party/xnvme/build" ]; then	\
		mkdir -p "third-party/xnvme/build";	\
		mkdir -p "third-party/xnvme/include";	\
	fi
	cd $(BUILD_DIR)/ztl && ${MAKE}

.PHONY: zrocks
zrocks: info
	@echo "## meta: make zrocks"
	@if [ ! -d "$(BUILD_DIR)/zrocks" ]; then	\
		mkdir -p "$(BUILD_DIR)/zrocks";		\
		cd $(BUILD_DIR)/zrocks;			\
		cmake ../../zrocks;			\
	fi
	cd $(BUILD_DIR)/zrocks && ${MAKE}

.PHONY: tests
tests: info
	@echo "## meta: make tests"
	@if [ ! -d "$(BUILD_DIR)/tests" ]; then	\
		mkdir -p "$(BUILD_DIR)/tests";	\
		cd $(BUILD_DIR)/tests;		\
		cmake ../../tests;		\
	fi
	cd $(BUILD_DIR)/tests && ${MAKE}

.PHONY: lib-only
lib-only: info ztl zrocks tests
	@echo "### Congrats! Your library is ready!"

.PHONY: all
all: info deps-xnvme lib-only

.PHONY: install
install:
	@echo "## meta: make install"
	cd $(BUILD_DIR)/zrocks && ${MAKE} install

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
	cd third-party/xnvme && ${MAKE} clean && ${MAKE}

.PHONY: deps-xnvme-fetch
deps-xnvme-fetch:
	@echo "## libztl: make deps-xnvme-fetch"
	@echo "# deps-fetch: fetching xnvme"
	@echo "# deps-fetch: checkout xnvme"
	@git submodule update --init
	cd third-party/xnvme && git fetch && git checkout v0.0.17 && if [ ! -d third-party/xnvme/build ]; then git submodule update --init --recursive; fi
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
	@$(MAKE) deps-xnvme-fetch
	@$(MAKE) deps-xnvme-build
