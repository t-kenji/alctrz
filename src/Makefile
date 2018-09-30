# makefile for Alcatraz.

include $(TOP_DIR)/config.mk
include $(TOP_DIR)/project.mk

TARGET := $(NAME)

INCS := -I.
ifneq ($(JANSSON_DIR),)
  INCS += -I$(JANSSON_DIR)/include
endif
#OPT_WARN := -Wall -Werror
OPT_WARN := -Wall
OPT_OPTIM := -Og
OPT_DBG := -g
OPT_DEP := -MMD -MP

OPTS := $(OPT_WARN) $(OPT_OPTIM) $(OPT_DBG) $(OPT_DEP)
CFLAGS := -std=c11 $(OPTS) $(INCS)
CFLAGS += $(EXTRA_CFLAGS)
CPPFLAGS := -DNODEBUG=$(NODEBUG) -DMODULE_VERSION=\"$(VERSION)\"
CPPFLAGS += $(EXTRA_CPPFLAGS)
LDFLAGS :=
ifneq ($(JANSSON_DIR),)
  LDFLAGS += -L$(JANSSON_DIR)/lib
endif
LDFLAGS += $(EXTRA_LDFLAGS)
LIBS := -lutil -ljansson
LIBS += $(EXTRA_LIBS)

SRCS = collections.c alctrz.c
OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)

.PHONY: all $(TARGET) clean

%.o: %.c
	$(QCC)$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ -c $<

all: $(TARGET)

$(TARGET): $(OBJS)
	$(QLINK)$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	$(QCLEAN)rm -rf $(OBJS) $(DEPS) $(TARGET)

-include $(DEPS)
