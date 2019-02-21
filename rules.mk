# Make rules for some project.

ifneq ($(BUILD_TYPE),release)
  EXTRA_CFLAGS += -fprofile-arcs -ftest-coverage
  EXTRA_LDLIBS += -lgcov
endif

INCS := -I. $(EXTRA_INCS)

OPT_WARN := -Wall -Wextra -Wshadow
OPT_WARN += $(if $(filter $(WARN_AS_ERROR),1),-Werror)
OPT_WARN += -Wno-clobbered # workarround for pthread_cleanup_push() bug
OPT_WARN += -Wno-missing-field-initializers
OPT_OPTIM := $(if $(filter $(BUILD_TYPE),release),-O2,-Og)
OPT_OPTIM += $(if $(or $(LIBRARY), $(filter-out $(BUILD_TYPE),release)),-fPIC)
OPT_DEBUG := $(if $(filter-out $(BUILD_TYPE),release),-g)
ifneq ($(and $(filter-out $(BUILD_TYPE),release), $(filter $(ENABLE_SANITIZER),1)),)
  OPT_DEBUG += -fsanitize=address -fsanitize=leak -fno-omit-frame-pointer
endif
OPT_DEP := -MMD -MP

OPTS := $(OPT_WARN) $(OPT_OPTIM) $(OPT_DEBUG) $(OPT_DEP)
DEFS := -DMODULE_VERSION=\"$(VERSION)\"
DEFS += $(if $(NODEBUG),-DNODEBUG=$(NODEBUG))
DEFS += $(if $(INTERNAL_TESTABLE),-DINTERNAL_TESTABLE=$(INTERNAL_TESTABLE))

CPPFLAGS := $(DEFS) $(EXTRA_CPPFLAGS)
CFLAGS := $(if $(CSTANDARD),-std=$(CSTANDARD)) $(OPTS) -fdiagnostics-color $(INCS) $(EXTRA_CFLAGS)
CXXFLAGS := $(if $(CXXSTANDARD),-std=$(CXXSTANDARD)) $(OPTS) -fdiagnostics-color $(INCS) $(EXTRA_CXXFLAGS)
LDFLAGS := $(if $(or $(ENABLE_STATIC),$(ENABLE_SHARED)),-L$(TOP_DIR)/src) $(EXTRA_LDFLAGS)
CLDLIBS := $(if $(or $(ENABLE_STATIC),$(ENABLE_SHARED)),-l$(NAME))
ifneq ($(and $(filter-out $(BUILD_TYPE),release), $(filter $(ENABLE_SANITIZER),1)),)
  CLDLIBS += -lasan -lstdc++
endif
CLDLIBS += $(EXTRA_LDLIBS)
CXXLDLIBS := $(if $(or $(ENABLE_STATIC),$(ENABLE_SHARED)),-l$(NAME))
ifneq ($(and $(filter-out $(BUILD_TYPE),release), $(filter $(ENABLE_SANITIZER),1)),)
  CXXLDLIBS += -lasan
endif
CXXLDLIBS += $(EXTRA_LDLIBS)
ARFLAGS := rcs

Q1     = $(V:1=)
QCC    = $(Q1:0=@echo "  CC    $@";)
QCXX   = $(Q1:0=@echo "  CXX   $@";)
QLD    = $(Q1:0=@echo "  LD    $@";)
QAR    = $(Q1:0=@echo "  AR    $@";)
QLINK  = $(Q1:0=@echo "  LINK  $@";)
QSTRIP = $(Q1:0=@echo "  STRIP $@";)
QCLEAN = $(Q1:0=@echo "  CLEAN $(CEXECUTABLE)$(LIBRARY)$(CXXEXECUTABLE)";)

ifneq ($(DISABLE_CCACHE),1)
  CCACHE := $(shell which ccache)
endif
CC := $(CCACHE) $(CROSS_COMPILE)gcc
CXX := $(CCACHE) $(CROSS_COMPILE)g++
LD := $(CROSS_COMPILE)ld
AR := $(CROSS_COMPILE)ar
STRIP := $(CROSS_COMPILE)strip

ifeq ($(ENABLE_STATIC),1)
  STATIC_LIBRARY := $(LIBRARY).a.$(VERSION)
endif
ifeq ($(ENABLE_SHARED),1)
  SHARED_LIBRARY := $(LIBRARY).so.$(VERSION)
endif
DEPS := $(OBJS:.o=.d)
GCDAS := $(OBJS:.o=.gcda)
GCNOS := $(OBJS:.o=.gcno)

%.o: %.c
	$(QCC)$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

%.o: %.cpp
	$(QCXX)$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

.PHONY: all \
        $(CEXECUTABLE) \
        $(LIBRARY) $(STATIC_LIBRARY) $(SHARED_LIBRARY) \
        $(CXXEXECUTABLE) \
        clean

all: $(CEXECUTABLE) $(LIBRARY) $(CXXEXECUTABLE)

$(CEXECUTABLE): $(OBJS)
	$(QLINK)$(CC) $(LDFLAGS) -o $@ $^ $(CLDLIBS)
ifeq ($(BUILD_TYPE),release)
	$(QSTRIP)$(STRIP) -s $@
endif

$(LIBRARY): $(STATIC_LIBRARY) $(SHARED_LIBRARY)

$(STATIC_LIBRARY): $(OBJS)
	$(QAR)$(AR) $(ARFLAGS) $@ $^
	@ln -fs $@ $(@:.$(VERSION)=)

$(SHARED_LIBRARY): $(OBJS)
	$(QLD)$(LD) -shared -fPIC -o $@ $^
	@ln -fs $@ $(@:.$(VERSION)=)

$(CXXEXECUTABLE): $(OBJS)
	$(QLINK)$(CXX) $(LDFLAGS) -o $@ $^ $(CXXLDLIBS)

cppcheck:
	@cppcheck -q --enable=all --platform=unix32 \
	          --suppress=unusedFunction \
	          $(CPPFLAGS) \
	          $(INCS) \
	          .

clean:
	$(QCLEAN)rm -rf $(if $(LIBRARY),$(LIBRARY).a* $(LIBRARY).so*) $(CEXECUTABLE) $(CXXEXECUTABLE) $(OBJS) $(DEPS) $(GCDAS) $(GCNOS)

-include $(DEPS)
