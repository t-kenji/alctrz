# Makefile for Alcatraz unit test.

CXXEXECUTABLE = $(NAME)_utest
OBJS = main.o \
       $(NULL)
EXTRA_CXXFLAGS += -I$(TOP_DIR)/src
EXTRA_CXXFLAGS += $(if $(CATCH2_DIR),-I$(CATCH2_DIR)/single_include)

include $(TOP_DIR)/rules.mk
