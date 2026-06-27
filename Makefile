.SUFFIXES:
export TARGET := dlpdump
DEBUG_TARGET := $(TARGET)_debug
export TOPDIR := $(CURDIR)

GAME_ICON := $(TOPDIR)/assets/banner/icon.bmp
GAME_TITLE := $(strip $(shell cat "$(TOPDIR)/assets/banner/title.txt"))
GAME_CODE := \#\#\#\#
MAKER_CODE := LK
ROM_TITLE := DLPDUMP
ROM_VERSION := 0

NDS_TARGETS := all debug _arm7 _arm7_debug _arm9 _arm9_debug $(TARGET).nds $(DEBUG_TARGET).nds arm7/$(TARGET).elf arm7/$(DEBUG_TARGET).elf arm9/$(TARGET).elf arm9/$(DEBUG_TARGET).elf
ifeq ($(strip $(MAKECMDGOALS)),)
REQUESTED_NDS_TARGETS := all
else
REQUESTED_NDS_TARGETS := $(filter $(NDS_TARGETS),$(MAKECMDGOALS))
endif

ifneq ($(strip $(REQUESTED_NDS_TARGETS)),)
ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif
export DEVKITPRO := $(subst \,/,$(DEVKITPRO))
export DEVKITARM := $(subst \,/,$(DEVKITARM))
include $(DEVKITARM)/ds_rules
endif

.PHONY: all debug _arm7 _arm7_debug _arm9 _arm9_debug clean

all: _arm7 _arm9 $(TARGET).nds _arm7_debug _arm9_debug $(DEBUG_TARGET).nds

debug: _arm7_debug _arm9_debug $(DEBUG_TARGET).nds

_arm7:
	$(MAKE) -C arm7 TARGET=$(TARGET)

_arm7_debug:
	$(MAKE) -C arm7 TARGET=$(DEBUG_TARGET)

_arm9:
	$(MAKE) -C arm9 TARGET=$(TARGET)

_arm9_debug:
	$(MAKE) -C arm9 TARGET=$(DEBUG_TARGET) DEBUG_VERSION=1

$(TARGET).nds: arm7/$(TARGET).elf arm9/$(TARGET).elf
	ndstool -c $(TARGET).nds -7 arm7/$(TARGET).elf -9 arm9/$(TARGET).elf \
		-b $(GAME_ICON) "$(GAME_TITLE)" \
		-g "$(GAME_CODE)" $(MAKER_CODE) "$(ROM_TITLE)" $(ROM_VERSION) \
		$(_ADDFILES)

$(DEBUG_TARGET).nds: arm7/$(DEBUG_TARGET).elf arm9/$(DEBUG_TARGET).elf
	ndstool -c $(DEBUG_TARGET).nds -7 arm7/$(DEBUG_TARGET).elf -9 arm9/$(DEBUG_TARGET).elf \
		-b $(GAME_ICON) "$(GAME_TITLE)" \
		-g "$(GAME_CODE)" $(MAKER_CODE) "$(ROM_TITLE)" $(ROM_VERSION) \
		$(_ADDFILES)

arm7/$(TARGET).elf:
	$(MAKE) -C arm7

arm9/$(TARGET).elf:
	$(MAKE) -C arm9

clean:
	rm -rf arm7/build arm9/build arm9/build-debug arm9/source/generated arm9/include/generated
	rm -f $(TARGET).nds $(DEBUG_TARGET).nds \
		arm7/$(TARGET).elf arm7/$(DEBUG_TARGET).elf \
		arm9/$(TARGET).elf arm9/$(DEBUG_TARGET).elf \
		arm7/$(TARGET).ntr.elf arm7/$(DEBUG_TARGET).ntr.elf \
		arm9/$(TARGET).ntr.elf arm9/$(DEBUG_TARGET).ntr.elf
