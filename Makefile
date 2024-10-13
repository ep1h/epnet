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
    CLIENT_STATIC := libepnet-client.a
    CLIENT_SHARED := epnet-client.dll
    SERVER_STATIC := libepnet-server.a
    SERVER_SHARED := epnet-server.dll
    LDFLAGS       += -static
    LDLIBS        := -lws2_32
    ifneq ($(shell uname -s),Windows_NT)
        ifeq ($(origin CC),default)
            CC := $(MINGW_PREFIX)-gcc
        endif
        ifeq ($(origin AR),default)
            AR := $(MINGW_PREFIX)-ar
        endif
    endif
else ifeq ($(OS),linux)
    CFLAGS        += -fPIC
    CLIENT_STATIC := libepnet-client.a
    CLIENT_SHARED := libepnet-client.so
    SERVER_STATIC := libepnet-server.a
    SERVER_SHARED := libepnet-server.so
    LDLIBS        :=
    ifeq ($(ARCH),x86)
        CFLAGS  += -m32
        LDFLAGS += -m32
    endif
else
    $(error Unknown OS '$(OS)'. Use OS=linux or OS=windows)
endif

# Directories
INCLUDE_DIR := include
COMMON_DIR  := src/common
CLIENT_DIR  := src/client
SERVER_DIR  := src/server
BUILD_DIR   := build/$(OS)-$(ARCH)-$(CFG)
LIB_DIR     := $(BUILD_DIR)/lib

# Internal headers live in src/common/
INTERNAL_INCLUDES := -I$(INCLUDE_DIR) -I$(COMMON_DIR)

# Sources
COMMON_SRCS := $(shell find $(COMMON_DIR) -name '*.c')
CLIENT_SRCS := $(shell find $(CLIENT_DIR) -name '*.c')
SERVER_SRCS := $(shell find $(SERVER_DIR) -name '*.c')

COMMON_OBJS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(COMMON_SRCS))
CLIENT_OBJS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(CLIENT_SRCS))
SERVER_OBJS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(SERVER_SRCS))

DEPS := $(COMMON_OBJS:.o=.d) $(CLIENT_OBJS:.o=.d) $(SERVER_OBJS:.o=.d)

# Targets
.PHONY: all static shared clean distclean format help

all: static shared

static: $(LIB_DIR)/$(CLIENT_STATIC) $(LIB_DIR)/$(SERVER_STATIC)

shared: $(LIB_DIR)/$(CLIENT_SHARED) $(LIB_DIR)/$(SERVER_SHARED)

# Static libraries
$(LIB_DIR)/$(CLIENT_STATIC): $(COMMON_OBJS) $(CLIENT_OBJS)
	@mkdir -p $(LIB_DIR)
	$(AR) rcs $@ $^

$(LIB_DIR)/$(SERVER_STATIC): $(COMMON_OBJS) $(SERVER_OBJS)
	@mkdir -p $(LIB_DIR)
	$(AR) rcs $@ $^

# Shared libraries
$(LIB_DIR)/$(CLIENT_SHARED): $(COMMON_OBJS) $(CLIENT_OBJS)
	@mkdir -p $(LIB_DIR)
	$(CC) -shared $^ -o $@ $(LDFLAGS) $(LDLIBS)

$(LIB_DIR)/$(SERVER_SHARED): $(COMMON_OBJS) $(SERVER_OBJS)
	@mkdir -p $(LIB_DIR)
	$(CC) -shared $^ -o $@ $(LDFLAGS) $(LDLIBS)

# Object rules
$(BUILD_DIR)/obj/src/common/%.o: $(COMMON_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INTERNAL_INCLUDES) -MMD -MP -c $< -o $@

$(BUILD_DIR)/obj/src/client/%.o: $(CLIENT_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INTERNAL_INCLUDES) -MMD -MP -c $< -o $@

$(BUILD_DIR)/obj/src/server/%.o: $(SERVER_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INTERNAL_INCLUDES) -MMD -MP -c $< -o $@

-include $(DEPS)

clean:
	rm -rf $(BUILD_DIR)

distclean:
	rm -rf build

format:
	find src $(INCLUDE_DIR) -name '*.[ch]' -exec clang-format -i {} +

help:
	@echo "epnet library build system"
	@echo ""
	@echo "Usage: make [TARGET] [OPTIONS]"
	@echo ""
	@echo "Targets:"
	@echo "  all        Build all static and shared libraries (default)"
	@echo "  static     Build static libraries"
	@echo "  shared     Build shared libraries"
	@echo "  clean      Remove build artifacts for current configuration"
	@echo "  distclean  Remove all build artifacts"
	@echo "  format     Run clang-format on all sources"
	@echo "  help       Show this message"
	@echo ""
	@echo "Libraries:"
	@echo "  libepnet-client  Common + client networking"
	@echo "  libepnet-server  Common + server networking"
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
