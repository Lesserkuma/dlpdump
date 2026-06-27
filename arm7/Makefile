.SUFFIXES:
NDS_SUBDIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

export DEVKITPRO := $(subst \,/,$(DEVKITPRO))
export DEVKITARM := $(subst \,/,$(DEVKITARM))
include $(DEVKITARM)/ds_rules

TARGET ?= dlpdump
NDS_PLATFORM := arm7
BUILD    := build
SOURCES  := source ../common
INCLUDES := include ../common
DATA     :=

ARCH := -march=armv4t -mtune=arm7tdmi -mthumb
CFLAGS   := -g -Wall -Wextra -Os -ffunction-sections -fdata-sections $(ARCH) $(INCLUDE) -DARM7
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=ds_arm7.specs -g $(ARCH) -Wl,-Map,$(notdir $*).map

LIBS := -lnds7 -lcalico_ds7
LIBDIRS := $(LIBNDS) $(DEVKITPRO)/calico

include $(NDS_SUBDIR)/../mk/nds_common.mk
