include NickelHook/NickelHook.mk

override LIBRARY := libnickelturn.so
override SOURCES += src/nickelturn_log.c
override CFLAGS += -Wall -Wextra -Werror
override CXXFLAGS += -Wall -Wextra -Werror -Wno-missing-field-initializers

# Public build profiles. The implementation's historical proof stages are no
# longer selectable: `full` is the verified Stage 21 feature set, while
# `smoke` performs only the safe load/failsafe check.
NT_BUILD ?= full

ifeq ($(NT_BUILD),smoke)
override SOURCES += src/nickelturn_smoke.c
else ifeq ($(NT_BUILD),full)
override SOURCES += src/nickelturn_plugin.c src/nickelturn.cc
else
$(error NT_BUILD must be smoke or full)
endif

include NickelHook/NickelHook.mk
