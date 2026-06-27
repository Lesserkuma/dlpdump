.SUFFIXES:
NDS_SUBDIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

export DEVKITPRO := $(subst \,/,$(DEVKITPRO))
export DEVKITARM := $(subst \,/,$(DEVKITARM))
include $(DEVKITARM)/ds_rules

TARGET ?= dlpdump
NDS_PLATFORM := arm9
ifeq ($(strip $(DEBUG_VERSION)),1)
BUILD    := build-debug
else
BUILD    := build
endif
SOURCES  := source source/generated ../common
INCLUDES := include include/generated ../common
DATA     :=
GRAPHICS :=

ARCH := -march=armv5te -mtune=arm946e-s
CFLAGS   := -g -Wall -Wextra -O2 -ffunction-sections -fdata-sections $(ARCH) $(INCLUDE) -DARM9
ifeq ($(strip $(DEBUG_VERSION)),1)
CFLAGS   += -DDEBUG_VERSION=1
endif
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=ds_arm9.specs -g $(ARCH) -Wl,-Map,$(notdir $*).map

ifeq ($(BUILD),$(notdir $(CURDIR)))
.PHONY: force_variant_relink
$(ARM9ELF): force_variant_relink
endif

LIBS := -lfat -lnds9 -lcalico_ds9
LIBDIRS := $(LIBNDS) $(DEVKITPRO)/calico $(PORTLIBS)

ifneq ($(BUILD),$(notdir $(CURDIR)))
.PHONY: all
all: $(BUILD)

PYTHON ?= python
FONT_DIR := ../assets/font
FONT_GEN_C := source/generated/font_8x8.c
FONT_GEN_H := include/generated/font_8x8.h
SIGNAL_ICON_DIR := ../assets/wireless-strength-icons
SIGNAL_ICON_GEN_C := source/generated/signal_icons.c
SIGNAL_ICON_GEN_H := include/generated/signal_icons.h
GENERATED_FILES := $(FONT_GEN_C) $(FONT_GEN_H) $(SIGNAL_ICON_GEN_C) $(SIGNAL_ICON_GEN_H)
EXTRA_CFILES := font_8x8.c signal_icons.c

$(FONT_GEN_C) $(FONT_GEN_H): ../buildtools/build_font.py $(FONT_DIR)/font.txt $(FONT_DIR)/glyphs.tsv $(FONT_DIR)/glyphs.bmp
	@mkdir -p source/generated include/generated
	@$(PYTHON) ../buildtools/build_font.py \
		--font $(FONT_DIR)/font.txt \
		--glyphs $(FONT_DIR)/glyphs.tsv \
		--bitmap $(FONT_DIR)/glyphs.bmp \
		--out-c $(FONT_GEN_C) \
		--out-h $(FONT_GEN_H)

$(SIGNAL_ICON_GEN_C) $(SIGNAL_ICON_GEN_H): ../buildtools/build_signal_icons.py $(SIGNAL_ICON_DIR)/1.bmp $(SIGNAL_ICON_DIR)/2.bmp $(SIGNAL_ICON_DIR)/3.bmp $(SIGNAL_ICON_DIR)/4.bmp
	@mkdir -p source/generated include/generated
	@$(PYTHON) ../buildtools/build_signal_icons.py \
		--input-dir $(SIGNAL_ICON_DIR) \
		--out-c $(SIGNAL_ICON_GEN_C) \
		--out-h $(SIGNAL_ICON_GEN_H)
endif

include $(NDS_SUBDIR)/../mk/nds_common.mk
