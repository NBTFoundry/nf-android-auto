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
# QNX toolchain: the environment script to source, and the folder holding
# the head unit's libscreen.so.
QNX_ENV ?=
QNX_SCREEN_LIBDIR ?=

CXX ?= g++
QCC ?= qcc
PYTHON ?= python3
QNX_STRIP ?= ntoarmv7-strip
QNX_READELF ?= ntoarmv7-readelf
QNX_NM ?= ntoarmv7-nm

NBT_SDK_BUILD_DIR := $(abspath $(BUILD_DIR))/sdk
include $(NBT_SDK_DIR)/nbt-sdk.mk

# --- NBT-AAD, unmodified (third_party/nbt-aad) ---
AAD_ROOT := third_party/nbt-aad
AAD_DIR := $(AAD_ROOT)/milestone-7

# --- The adapter ---
LIB_SRC := src/daemon.cpp src/daemon_settings.cpp src/daemon_status.cpp
APP_SRC := $(LIB_SRC) src/main.cpp
APP_INC := $(NBT_SDK_INC) -Isrc
APP_CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti $(CXXFLAGS)
objs = $(patsubst %.cpp,$(OBJ_DIR)/$(1)/%.o,$(2))

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

.PHONY: all host test test-asan test-upstream check-protocol check-static-init format qnx qnx-env \
	qnx-check daemon-qnx package package-args notice clean

all: host test

# --- Host (Linux): the adapter with a fake daemon appended ---
$(OBJ_DIR)/host/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(APP_INC) $(APP_CXXFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/asan/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(APP_INC) $(APP_CXXFLAGS) $(ASAN_FLAGS) -MMD -MP -c $< -o $@

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

# --- QNX (head unit, ARMv7): the adapter with the real daemon appended ---
qnx-env:
	@test -n "$(QNX_ENV)" || { echo "QNX_ENV is not set: make QNX_ENV=/path/to/qnx-env.sh ..."; exit 1; }
	@test -r "$(QNX_ENV)" || { echo "QNX_ENV=$(QNX_ENV) is not readable"; exit 1; }

QNX_CPP_VARIANT := -Vgcc_ntoarmv7le_gpp
QNX_APP_CXXFLAGS := $(QNX_CPP_VARIANT) -D_QNX_SOURCE -O2 -Wc,-std=c++11 -Wc,-fno-exceptions \
	-Wc,-fno-rtti -Wp,-U__EXCEPTIONS -Wc,-Wall -Wc,-Wextra -Wc,-Werror $(APP_INC)
QNX_DEPFLAGS = -Wp,-MMD,$(@:.o=.d) -Wp,-MP -Wp,-MT,$@

$(OBJ_DIR)/qnx/%.o: %.cpp | qnx-env
	@mkdir -p $(dir $@)
	. "$(QNX_ENV)" && $(QCC) $(QNX_APP_CXXFLAGS) $(QNX_DEPFLAGS) -c $< -o $@

$(QNX_ADAPTER)-unstripped: $(call objs,qnx,$(APP_SRC)) $(NBT_SDK_QNX_LIBS) | qnx-env
	@test -n "$(QNX_SCREEN_LIBDIR)" || { echo "QNX_SCREEN_LIBDIR is not set (directory with libscreen.so)"; exit 1; }
	. "$(QNX_ENV)" && $(QCC) $(QNX_CPP_VARIANT) -lang-c++ $(filter %.o %.a,$^) \
		-L$(QNX_SCREEN_LIBDIR) $(NBT_SDK_QNX_LDFLAGS) $(NBT_SDK_QNX_LDLIBS) -o $@

$(QNX_ADAPTER): $(QNX_ADAPTER)-unstripped
	. "$(QNX_ENV)" && $(QNX_STRIP) -o $@ $<

# The daemon with NBT-AAD's own Makefile, flags and libraries (its objects
# stay in the submodule, which ignores them).
daemon-qnx: | qnx-env
	. "$(QNX_ENV)" && $(MAKE) -C $(AAD_DIR) QCC=$(QCC) nbt-aad
	@mkdir -p $(BUILD_DIR)
	. "$(QNX_ENV)" && $(QNX_STRIP) -o $(QNX_DAEMON) $(AAD_DIR)/nbt-aad

$(QNX_APP): $(QNX_ADAPTER) daemon-qnx
	$(NBT_SDK_APPEND) $(QNX_ADAPTER) $(QNX_DAEMON) $@

qnx: $(QNX_APP)

# The car runs QNX 6.5.0 SP1: the adapter's imports must exist there. The
# daemon's are NBT-AAD's business (it links the car's own libraries).
QNX65_SP1_SYSROOT ?= $(HOME)/src/tools/qnx-6.5sp1-tools/sdk/target/qnx6
qnx-check: qnx
	. "$(QNX_ENV)" && set -e; \
		bad="$$($(QNX_NM) -u $(QNX_ADAPTER)-unstripped | grep -E 'GLIBCXX_3\.4\.(1[4-9]|[2-9][0-9])|__cxa_throw' || true)"; \
		if test -n "$$bad"; then echo "Disallowed QNX ABI symbols:"; echo "$$bad"; exit 1; fi; \
		echo "OK: adapter ABI symbols"
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
		$(VM_SP1_LIB)/libusbdi.so.2 $(VM_SP1_LIB)/libcpp.so.4 $(QNX65_SP1_SYSROOT)/armle-v7/bin/cat

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
