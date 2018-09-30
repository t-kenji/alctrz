# makefile for Alcatraz.

export TOP_DIR := $(shell pwd)

include $(TOP_DIR)/config.mk

.PHONY: all test test-build doc cppcheck install clean

all:
	@make -C src

test: test-build
	@sudo ./src/$(NAME) -c tests/env_caps.json -u $(USER) -- $(TOP_DIR)/tests/$(NAME)_test -r compact -s

test-build: all
	@make -C tests

doc:
	@sed -e 's/@PROJECT@/$(DOXY_PROJECT)/' \
	     -e 's/@VERSION@/$(VERSION)/' \
	     -e 's/@BRIEF@/$(DOXY_BRIEF)/' \
	     -e 's/@OUTPUT@/$(DOXY_OUTPUT)/' \
	     -e 's/@SOURCES@/$(DOXY_SOURCES)/' \
	     Doxygen.conf.in > Doxygen.conf
	@doxygen Doxygen.conf

cppcheck:
	@cppcheck --enable=all --suppress=unusedFunction ./src

install: all
	install -m 755 ./src/$(NAME) $(DEST:/=)/root

clean:
	@rm -rf Doxygen.conf $(DOXY_OUTPUT)
	@make -C src clean
	@make -C tests clean
