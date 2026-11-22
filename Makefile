# SPDX-License-Identifier: MPL-2.0
SHELL := /bin/bash

BUILD_DIR ?= build
OBJ_DIR := $(BUILD_DIR)/obj

# Releases: the vX.Y.Z tag gives the version and the sequence (release.yml).
# A local package names both; the sequence must rise with every package
# installed on a car (the manager refuses one that does not).
APP_VERSION ?=
APP_SEQ ?=
APP_KEY ?=
# Sibling NBTFoundry checkouts; override as needed. The SDK is built into
# $(BUILD_DIR)/sdk; the manager is only needed for packaging and its checks.
NBT_SDK_DIR ?= ../sdk
NBT_MGR_DIR ?= ../manager
CXX ?= g++
PYTHON ?= python3

NBT_SDK_BUILD_DIR := $(abspath $(BUILD_DIR))/sdk
include $(NBT_SDK_DIR)/nbt-sdk.mk

# --- NBT-AAD, unmodified (third_party/nbt-aad) ---
AAD_ROOT := third_party/nbt-aad
AAD_DIR := $(AAD_ROOT)/milestone-7

# --- The adapter ---
LIB_SRC := src/daemon.cpp src/daemon_bridge.cpp src/daemon_log.cpp src/daemon_settings.cpp src/daemon_status.cpp
APP_SRC := $(LIB_SRC) src/main.cpp
# NBT-AAD's own car-data reader (rhmiapp/cds_parse.hpp, PolyForm NC like the package).
APP_INC := $(NBT_SDK_INC) -Isrc -I$(AAD_DIR)/rhmiapp
APP_CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti $(CXXFLAGS)
objs = $(patsubst %.cpp,$(OBJ_DIR)/$(1)/%.o,$(2))
# Each kind of object rebuilds when its compile command changes (the SDK's
# nbt-stamp.mk; the stamps are written at the end of this file).
stamp = $(OBJ_DIR)/$(1).stamp
ifeq ($(value nbt_stamp),)
$(error $(NBT_SDK_ROOT) has no nbt-stamp.mk: update that SDK checkout)
endif
HOST_COMPILE = $(CXX) $(APP_INC) $(APP_CXXFLAGS)
ASAN_COMPILE = $(HOST_COMPILE) $(ASAN_FLAGS)

HOST_ADAPTER := $(BUILD_DIR)/adapter-host
HOST_APP := $(BUILD_DIR)/nbt-android-auto-host
ASAN_ADAPTER := $(BUILD_DIR)/adapter-asan
ASAN_APP := $(BUILD_DIR)/nbt-android-auto-asan
UNIT_TEST := $(BUILD_DIR)/adapter-test
QNX_ADAPTER := $(BUILD_DIR)/adapter-qnx
QNX_DAEMON := $(BUILD_DIR)/nbt-aad-qnx
QNX_APP := $(BUILD_DIR)/nbt-android-auto-qnx
FAKE_DAEMON := tests/fake_nbt_aad.py
ASAN_FLAGS := -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer

.PHONY: all host test test-asan test-upstream check-protocol check-static-init format qnx \
	qnx-check package package-args notice clean

all: host test

# --- Host (Linux): the adapter with a fake daemon appended ---
$(OBJ_DIR)/host/%.o: %.cpp $(call stamp,host)
	@mkdir -p $(dir $@)
	$(HOST_COMPILE) -MMD -MP -c $< -o $@

$(OBJ_DIR)/asan/%.o: %.cpp $(call stamp,asan)
	@mkdir -p $(dir $@)
	$(ASAN_COMPILE) -MMD -MP -c $< -o $@

$(HOST_ADAPTER): $(call objs,host,$(APP_SRC)) $(NBT_SDK_HOST_LIBS)
	$(CXX) $^ $(LDFLAGS) $(NBT_SDK_HOST_LDLIBS) -o $@

$(ASAN_ADAPTER): $(call objs,asan,$(APP_SRC)) $(NBT_SDK_ASAN_LIBS)
	$(CXX) $(ASAN_FLAGS) $^ $(LDFLAGS) $(NBT_SDK_HOST_LDLIBS) -o $@

$(HOST_APP): $(HOST_ADAPTER) $(FAKE_DAEMON)
	$(NBT_SDK_APPEND) $(HOST_ADAPTER) $(FAKE_DAEMON) $@

$(ASAN_APP): $(ASAN_ADAPTER) $(FAKE_DAEMON)
	$(NBT_SDK_APPEND) $(ASAN_ADAPTER) $(FAKE_DAEMON) $@

$(UNIT_TEST): $(call objs,host,tests/adapter_test.cpp $(LIB_SRC)) $(NBT_SDK_HOST_LIBS)
	$(CXX) $^ $(LDFLAGS) $(NBT_SDK_HOST_LDLIBS) -o $@

host: $(HOST_APP) $(UNIT_TEST)

test: host check-protocol
	$(UNIT_TEST)
	NF_AA_APP=$(abspath $(HOST_APP)) NBT_MGR_DIR=$(abspath $(NBT_MGR_DIR)) \
		$(PYTHON) -m unittest discover -s tests -v

test-asan: $(ASAN_APP)
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		NF_AA_APP=$(abspath $(ASAN_APP)) $(PYTHON) -m unittest discover -s tests -v -k Adapter

# NBT-AAD's own host tests, at the pin.
test-upstream:
	$(MAKE) -C $(AAD_DIR)/tests check

# Every fact src/protocol.hpp and src/settings.def hold about the daemon is
# still in the pinned sources.
check-protocol:
	$(PYTHON) tools/check_protocol.py --upstream $(AAD_DIR)
	$(PYTHON) tools/package_args.py --check release/package.base src/settings.def $(PACKAGE_ARGS)

check-static-init: $(call objs,host,$(APP_SRC))
	@set -e; \
	for obj in $^; do \
		ctors="$$(readelf -S "$$obj" | awk '/\.init_array|\.ctors/ {print $$2}')"; \
		if test -n "$$ctors"; then echo "ERROR: static constructor section in $$obj: $$ctors"; exit 1; fi; \
	done; \
	echo "OK: no .init_array/.ctors in the adapter's objects"

format:
	clang-format -i src/*.cpp src/*.hpp tests/*.cpp

# --- QNX (head unit, ARMv7): the adapter with the real daemon appended, both
# ordinary QNX programs built with the NBT toolchain's QNX target: no QNX SDP
# (module/flags.mk in the SDK; "make -C ../sdk toolchain" once). ---
QNX_APP_CXXFLAGS = $(NBT_QNX_CXXFLAGS) $(NBT_QNX_SDK_INC) -O2 -std=c++11 -Wall -Wextra -Werror \
	-fno-exceptions -fno-rtti $(APP_INC)
QNX_COMPILE = $(NBT_QNX_CXX) $(QNX_APP_CXXFLAGS)

$(OBJ_DIR)/qnx/%.o: %.cpp $(call stamp,qnx)
	@mkdir -p $(dir $@)
	$(QNX_COMPILE) -MMD -MP -c $< -o $@

$(QNX_ADAPTER)-unstripped: $(call objs,qnx,$(APP_SRC)) $(NBT_SDK_QNX_LIBS)
	$(call nbt_qnx_link,$@,$(filter %.o %.a,$^),$(NBT_SDK_QNX_LDLIBS))

$(QNX_ADAPTER): $(QNX_ADAPTER)-unstripped
	$(NBT_LLVM_BIN)/llvm-strip -o $@ $<

# The daemon: NBT-AAD's sources as its Makefile lists them, with its include
# directories and libraries (its own flags are QNX compiler options). Its
# objects stay in this build directory.
aad_var = $(shell $(MAKE) -s --no-print-directory -C $(AAD_DIR) -f Makefile -f $(abspath tools/print_var.mk) print-$(1))
AAD_SRCS := $(call aad_var,SRCS)
AAD_CSRCS := $(call aad_var,CSRCS)
AAD_OBJS := $(patsubst %.cpp,$(OBJ_DIR)/qnx-aad/%.o,$(AAD_SRCS)) $(patsubst %.c,$(OBJ_DIR)/qnx-aad/%.o,$(AAD_CSRCS))
AAD_INC := -I$(AAD_DIR)/include -I$(AAD_DIR)/src
AAD_LDLIBS := -rpath /opt/mm/lib:/lib:/usr/lib -la2i_client -lsocket -lssl -lcrypto -lscreen -lusbdi -lm
AAD_CXX_COMPILE = $(NBT_QNX_CXX) $(NBT_QNX_CXXFLAGS) -O2 -std=gnu++11 -w $(AAD_INC)
AAD_C_COMPILE = $(NBT_QNX_CC) $(NBT_QNX_CFLAGS) -O2 -std=gnu99 -w $(AAD_INC)

$(OBJ_DIR)/qnx-aad/%.o: $(AAD_DIR)/%.cpp $(call stamp,qnx-aad)
	@mkdir -p $(dir $@)
	$(AAD_CXX_COMPILE) -MMD -MP -c $< -o $@

$(OBJ_DIR)/qnx-aad/%.o: $(AAD_DIR)/%.c $(call stamp,qnx-aad)
	@mkdir -p $(dir $@)
	$(AAD_C_COMPILE) -MMD -MP -c $< -o $@

$(QNX_DAEMON)-unstripped: $(AAD_OBJS)
	$(call nbt_qnx_link,$@,$(AAD_OBJS),$(AAD_LDLIBS))

$(QNX_DAEMON): $(QNX_DAEMON)-unstripped
	$(NBT_LLVM_BIN)/llvm-strip -o $@ $<

$(QNX_APP): $(QNX_ADAPTER) $(QNX_DAEMON)
	$(NBT_SDK_APPEND) $(QNX_ADAPTER) $(QNX_DAEMON) $@

qnx: $(QNX_APP)

# The car runs QNX 6.5.0 SP1: the adapter's imports must exist there. The
# daemon also links the car's own liba2i_client and its newer OpenSSL (1.0.1).
QNX65_SP1_SYSROOT ?= $(HOME)/src/tools/qnx-6.5sp1-tools/sdk/target/qnx6
qnx-check: qnx
	@if test -d "$(QNX65_SP1_SYSROOT)"; then \
		$(PYTHON) $(NBT_SDK_ROOT)/tools/check_qnx_imports.py --sysroot "$(QNX65_SP1_SYSROOT)" \
			--readelf $(NBT_MOD_READELF) $(QNX_ADAPTER)-unstripped; \
	else echo "skipped: no QNX 6.5.0 SP1 sysroot at $(QNX65_SP1_SYSROOT)"; fi

# --- QNX 6.5 SP1 VM (the SDK's tools/qnx65-vm): the real daemon under the
# adapter, with the car's libraries NBT-AAD links and the SP1 ones it needs.
# The VM has no Bluetooth, Wi-Fi, display or network stack (io-pkt-v4 does not
# come up under its QEMU board), so it checks that the health check loads the
# daemon on SP1 and that a user start runs the daemon's own start-up (its own
# log, /dev/shmem/nbt-aad.log), which then ends without sockets. The control port is the host tests' and the car's. ---
VM_DIR := $(BUILD_DIR)/vm
VM_SP1_LIB := $(QNX65_SP1_SYSROOT)/armle-v7/lib
VM_RUN := $(NBT_SDK_ROOT)/tools/qnx65-vm/run.sh
# mkifs rewrites executables, which would drop the appended daemon; run.sh
# keeps "*-bundle" files byte for byte, as the manager installs them.
VM_APP := nbt-android-auto-bundle
VM_ENV := NBT_DATA_DIR=/dev/shmem
# vm_background(command, seconds, stop wait): run it, then stop it as the manager would.
vm_background = -c "$(1) &" -c "waitfor /none $(2)" -c "slay -s TERM $(VM_APP)" -c "waitfor /none $(3)"

.PHONY: vm-test
vm-test: $(QNX_APP)
	@mkdir -p $(VM_DIR)
	cp $(QNX_APP) $(VM_DIR)/$(VM_APP)
	cp $(AAD_DIR)/lib/liba2i_client.so $(VM_DIR)/liba2i_client.so
	cp $(AAD_DIR)/lib/libssl.so $(VM_DIR)/libssl.so.2
	cp $(AAD_DIR)/lib/libcrypto.so $(VM_DIR)/libcrypto.so.2
	QEMU_RUN_SECS=60 $(VM_RUN) \
		$(call vm_background,NBT_LAUNCH=check $(VM_ENV) $(VM_APP),8,2) \
		$(call vm_background,NBT_LAUNCH=user $(VM_ENV) $(VM_APP),15,2) \
		-c "cat /dev/shmem/nbt-aad.log" \
		-x 'check: /dev/shmem/nbt-aad loads on this head unit' \
		-x 'daemon: started /dev/shmem/nbt-aad' -x '\[Ctl\] socket failed' \
		-x 'daemon ended while starting' \
		$(VM_DIR)/$(VM_APP) $(VM_DIR)/liba2i_client.so $(VM_DIR)/libssl.so.2 $(VM_DIR)/libcrypto.so.2 \
		$(VM_SP1_LIB)/libusbdi.so.2 $(QNX65_SP1_SYSROOT)/armle-v7/bin/cat

# --- The signed package ---
PACKAGE_INPUT ?= $(QNX_APP)
PACKAGE := $(BUILD_DIR)/android-auto-$(APP_VERSION).nbt
PACKAGE_ARGS := release/package.args

package-args:
	$(PYTHON) tools/package_args.py release/package.base src/settings.def $(PACKAGE_ARGS)

package: $(PACKAGE_INPUT) check-protocol
	@test -f "$(APP_KEY)" || { echo "APP_KEY must name the RSA signing key"; exit 1; }
	@test -f "$(NBT_MGR_DIR)/tools/package.py" || { echo "NBT_MGR_DIR=$(NBT_MGR_DIR) has no tools/package.py"; exit 1; }
	@test -n "$(APP_VERSION)" -a -n "$(APP_SEQ)" || { echo "APP_VERSION and APP_SEQ must be given"; exit 1; }
	rm -f $(PACKAGE)
	$(PYTHON) $(NBT_MGR_DIR)/tools/package.py @$(PACKAGE_ARGS) \
		--version "$(APP_VERSION)" --sequence "$(APP_SEQ)" --key "$(APP_KEY)" \
		--output $(PACKAGE) $(PACKAGE_INPUT)

notice:
	NBT_SDK_DIR=$(NBT_SDK_ROOT) $(PYTHON) tools/make_notice.py

clean:
	rm -rf $(BUILD_DIR)

DEPS := $(shell find $(OBJ_DIR) -name '*.d' 2>/dev/null)
-include $(DEPS)

$(call nbt_stamp,$(call stamp,host),$(HOST_COMPILE),$(CXX))
$(call nbt_stamp,$(call stamp,asan),$(ASAN_COMPILE),$(CXX))
$(call nbt_stamp,$(call stamp,qnx),$(QNX_COMPILE),,$(NBT_TOOLCHAIN)/VERSION)
$(call nbt_stamp,$(call stamp,qnx-aad),$(AAD_CXX_COMPILE) | $(AAD_C_COMPILE),,$(NBT_TOOLCHAIN)/VERSION)
