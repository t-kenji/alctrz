# Configuration for Alcatraz.

NAME = alctrz
MAJOR_VERSION = 2
MINOR_VERSION = 0
REVISION = 0
VERSION = $(MAJOR_VERSION).$(MINOR_VERSION).$(REVISION)

## Options.
DEST ?=
NODEBUG ?= 0
EXTRA_CPPFLAGS ?=
EXTRA_CFLAGS ?=
EXTRA_CXXFLAGS ?=
EXTRA_LDFLAGS ?=
EXTRA_LIBS ?=

## Dependencies directory.
JANSSON_DIR ?=

## Header direcotory of Catch2 test framework.
CATCH2_DIR ?=

## Doxygen options.
DOXY_PROJECT = "Alcatraz"
DOXY_BRIEF = "Alcatraz: Chroot jail provider."
DOXY_OUTPUT = doxygen
DOXY_SOURCES = src
