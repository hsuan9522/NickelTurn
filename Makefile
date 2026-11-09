include NickelHook/NickelHook.mk
include config/firmware.mk

override LIBRARY := libnickelturn.so
override SOURCES += src/nickelturn_log.c
override PKGCONF += Qt5Widgets
override CFLAGS += -Wall -Wextra -Werror
override CXXFLAGS += -Wall -Wextra -Werror -Wno-missing-field-initializers

NT_FIRMWARE_PROFILE := src/firmware_profiles/$(NT_FIRMWARE).h
ifeq ($(filter $(NT_FIRMWARE),$(NT_SUPPORTED_FIRMWARES)),)
$(error Unsupported NT_FIRMWARE '$(NT_FIRMWARE)'; supported: $(NT_SUPPORTED_FIRMWARES))
endif
ifeq ($(wildcard $(NT_FIRMWARE_PROFILE)),)
$(error No firmware profile found at $(NT_FIRMWARE_PROFILE))
endif
override CXXFLAGS += -DNT_FIRMWARE_PROFILE_HEADER='"firmware_profiles/$(NT_FIRMWARE).h"'

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

.PHONY: print-firmware-list
override SKIPCONFIGURE += print-firmware-list

include NickelHook/NickelHook.mk

print-firmware-list:
	@printf '%s\n' $(NT_SUPPORTED_FIRMWARES)
