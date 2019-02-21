# makefile for Alcatraz.

export TOP_DIR := $(PWD)

include $(TOP_DIR)/config.mk

.PHONY: all build-all \
        build build-test \
        test \
        doc \
        cppcheck \
        install install-config \
        clean

all: build-all

build-all: build build-test

build:
	@make -C src

build-test: build
	@make -C test

test: build-test
	@make -C test test
	@sudo ./src/$(NAME) -c tests/env_caps.json -u $(USER) -- $(TOP_DIR)/tests/$(NAME)_test -r compact -s

doc:
	@sed -e 's|@PROJECT@|$(DOXY_PROJECT)|' \
	     -e 's|@VERSION@|$(DOXY_VERSION)|' \
	     -e 's|@BRIEF@|$(DOXY_BRIEF)|' \
	     -e 's|@OUTPUT@|$(DOXY_OUTPUT)|' \
	     -e 's|@SOURCES@|$(DOXY_SOURCES)|' \
	     -e 's|@EXCLUDE@|$(DOXY_EXCLUDE)|' \
	     -e 's|@EXCLUDE_SYMBOLS@|$(DOXY_EXCLUDE_SYMBOLS)|' \
	     -e 's|@PREDEFINED@|$(DOXY_PREDEFINED)|' \
	     -e 's|@README@|README.md|' \
	     -e 's|@EXTRACT_STATIC@|YES|' \
	     -e 's|@SHOW_FILES@|YES|' \
	     -e 's|@SOURCE_BROWSER@|YES|' \
	     Doxyfile.in > Doxyfile
	@doxygen Doxyfile

cppcheck:
	@make -C src cppcheck

install: build
	install -m 0755 ./src/$(NAME) $(DESTDIR:/=)/root/

install-config:
	install -m 0644 conf/env.json $(DESTDIR:/=)/etc/

clean:
	@rm -rf Doxyfile $(DOXY_OUTPUT) $(COV_OUTPUT)
	@make -C src clean
	@make -C test clean
