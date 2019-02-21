# Configuration for Alcatraz (Chroot provider).
export

# Project options.
NAME := alctrz
MAJOR_VERSION := 2
MINOR_VERSION := 0
REVISION := 1
VERSION := $(MAJOR_VERSION).$(MINOR_VERSION).$(REVISION)
DESTDIR ?=

# Build type.
#  release: No debuggable.
#  debug: Debuggable.
BUILD_TYPE := debug

# Doxygen options.
DOXY_PROJECT := "Alcatraz"
DOXY_BRIEF := "Alcatraz: Chroot jail provider."
DOXY_VERSION := "1.00"
DOXY_OUTPUT := doxygen
DOXY_SOURCES := src

# Dependencies.
JANSSON_DIR ?=
CATCH2_DIR ?=

# Test options.
INTERNAL_TESTABLE ?= 1

# Debug options.
NODEBUG ?= 0
WARN_AS_ERROR ?= 0
ENABLE_SANITIZER ?= 0
DISABLE_CCACHE ?= 0

# Compile & Link options.
CSTANDARD ?= c11
CXXSTANDARD ?= c++11
EXTRA_CPPFLAGS ?=
EXTRA_CFLAGS ?=
EXTRA_CXXFLAGS ?=
EXTRA_LDFLAGS ?= $(if $(JANSSON_DIR),-L$(JANSSON_DIR)/lib)
EXTRA_INCS ?= $(if $(JANSSON_DIR),-I$(JANSSON_DIR)/include)
EXTRA_LDLIBS ?= -ljansson -lutil

# Verbose options.
V ?= 0
