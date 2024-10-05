CFLAGS  := -std=c11 -Wall -Wextra -Wpedantic -Wconversion
LDFLAGS :=

# Build configuration
CFG ?= debug
ifeq ($(CFG),debug)
    CFLAGS += -g -O0 -DDEBUG
else ifeq ($(CFG),release)
    CFLAGS += -O2 -DNDEBUG
else
    $(error Unknown CFG '$(CFG)'. Use CFG=debug or CFG=release)
endif

# Architecture
ARCH ?= x64

ifeq ($(ARCH),x64)
    MINGW_PREFIX := x86_64-w64-mingw32
else ifeq ($(ARCH),x86)
    MINGW_PREFIX := i686-w64-mingw32
else
    $(error Unknown ARCH '$(ARCH)'. Use ARCH=x64 or ARCH=x86)
endif

# Platform
OS ?= linux

ifeq ($(OS),windows)
    STATIC_LIB := libnet.a
    SHARED_LIB := net.dll
    LDFLAGS    += -static
    LDLIBS     := -lws2_32
    ifneq ($(shell uname -s),Windows_NT)
        ifeq ($(origin CC),default)
            CC := $(MINGW_PREFIX)-gcc
        endif
        ifeq ($(origin AR),default)
            AR := $(MINGW_PREFIX)-ar
        endif
    endif
else ifeq ($(OS),linux)
    CFLAGS     += -fPIC
    STATIC_LIB := libnet.a
    SHARED_LIB := libnet.so
    LDLIBS     :=
    ifeq ($(ARCH),x86)
        CFLAGS  += -m32
        LDFLAGS += -m32
    endif
else
    $(error Unknown OS '$(OS)'. Use OS=linux or OS=windows)
endif

# Directories
SRC_DIR   := src/common
BUILD_DIR := build/$(OS)-$(ARCH)-$(CFG)
LIB_DIR   := $(BUILD_DIR)/lib

# Sources
SRCS := $(shell find $(SRC_DIR) -name '*.c')
OBJS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

# Targets
.PHONY: all static shared clean distclean format help

all: static shared

static: $(LIB_DIR)/$(STATIC_LIB)

shared: $(LIB_DIR)/$(SHARED_LIB)

# Static library
$(LIB_DIR)/$(STATIC_LIB): $(OBJS)
	@mkdir -p $(LIB_DIR)
	$(AR) rcs $@ $^

# Shared library
$(LIB_DIR)/$(SHARED_LIB): $(OBJS)
	@mkdir -p $(LIB_DIR)
	$(CC) -shared $^ -o $@ $(LDFLAGS) $(LDLIBS)

# Object rules
$(BUILD_DIR)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(SRC_DIR) -MMD -MP -c $< -o $@

-include $(DEPS)

clean:
	rm -rf $(BUILD_DIR)

distclean:
	rm -rf build

format:
	find src -name '*.[ch]' -exec clang-format -i {} +

help:
	@echo "net library build system"
	@echo ""
	@echo "Usage: make [TARGET] [OPTIONS]"
	@echo ""
	@echo "Targets:"
	@echo "  all        Build static and shared libraries (default)"
	@echo "  static     Build static library"
	@echo "  shared     Build shared library"
	@echo "  clean      Remove build artifacts for current configuration"
	@echo "  distclean  Remove all build artifacts"
	@echo "  format     Run clang-format on all sources"
	@echo "  help       Show this message"
	@echo ""
	@echo "Options:"
	@echo "  CFG=debug|release  Build configuration (default: debug)"
	@echo "  OS=linux|windows   Target platform (default: linux)"
	@echo "  ARCH=x64|x86       Target architecture (default: x64)"
	@echo "  CC=<compiler>      Override C compiler"
	@echo "  AR=<archiver>      Override archiver"
	@echo ""
	@echo "Output: build/<os>-<arch>-<cfg>/lib/"
	@echo "  Current: $(BUILD_DIR)/"
