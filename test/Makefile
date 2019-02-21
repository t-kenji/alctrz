# Makefile for Alcatraz tests.

TESTS := utest

.PHONY: all test clean

all:
	@$(foreach t,$(TESTS),make -f $(t).mk;)

test:
	@sudo bash -c "./$(NAME)_utest -r compact -s $(TAGS)"

clean:
	@$(foreach t,$(TESTS),make -f $(t).mk clean;)
