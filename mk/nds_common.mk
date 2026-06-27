# Common devkitPro Nintendo DS sub-build rules.
#
# The including Makefile must define:
#   NDS_PLATFORM    arm7 or arm9
#   BUILD           build directory
#   SOURCES         source directories
#   INCLUDES        project include directories
#   DATA            data directories, optional
#   LIBDIRS         library roots
#   LIBS            libraries

ifeq ($(strip $(NDS_PLATFORM)),)
$(error NDS_PLATFORM must be arm7 or arm9)
endif

GRAPHICS ?=
DATA ?=

nds_path = $(subst \,/,$(1))

ifneq ($(BUILD),$(notdir $(CURDIR)))
ifeq ($(NDS_PLATFORM),arm9)
export ARM9ELF := $(CURDIR)/$(TARGET).elf
else ifeq ($(NDS_PLATFORM),arm7)
export ARM7ELF := $(CURDIR)/$(TARGET).elf
else
$(error Unsupported NDS_PLATFORM $(NDS_PLATFORM))
endif

export DEPSDIR := $(CURDIR)/$(BUILD)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) $(foreach dir,$(DATA),$(CURDIR)/$(dir)) $(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir))

CFILES   := $(sort $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c))) $(EXTRA_CFILES))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
  export LD := $(CC)
else
  export LD := $(CXX)
endif

export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)
export HFILES := $(addsuffix .h,$(subst .,_,$(BINFILES)))
export INCLUDE := $(foreach dir,$(INCLUDES),-iquote $(CURDIR)/$(dir)) $(foreach dir,$(LIBDIRS),-I$(call nds_path,$(dir))/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(call nds_path,$(dir))/lib)

.PHONY: $(BUILD) clean
$(BUILD): $(GENERATED_FILES)
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).elf source/generated include/generated
else
ifeq ($(NDS_PLATFORM),arm9)
$(ARM9ELF): $(OFILES)
else
$(ARM7ELF): $(OFILES)
endif

%.bin.o %_bin.h: %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPSDIR)/*.d
endif
