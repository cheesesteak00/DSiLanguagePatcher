#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------
BLOCKSDS            ?= /opt/wonderful/thirdparty/blocksds/core
WONDERFUL_TOOLCHAIN ?= /opt/wonderful
ARM_NONE_EABI_PATH  ?= $(WONDERFUL_TOOLCHAIN)/toolchain/gcc-arm-none-eabi/bin/

ifeq ($(strip $(BLOCKSDS)),)
$(error "Please set BLOCKSDS in your environment. export BLOCKSDS=<path to>blocksds/core")
endif

export PATH     :=  $(ARM_NONE_EABI_PATH):$(PATH)
PREFIX          :=  arm-none-eabi-
CC              :=  $(PREFIX)gcc
CXX             :=  $(PREFIX)g++
AR              :=  $(PREFIX)ar
OBJCOPY         :=  $(PREFIX)objcopy
STRIP           :=  $(PREFIX)strip
NM              :=  $(PREFIX)nm
RANLIB          :=  $(PREFIX)ranlib

#---------------------------------------------------------------------------------
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# INCLUDES is a list of directories containing extra header files
# DATA is a list of directories containing binary files
# all directories are relative to this makefile
#---------------------------------------------------------------------------------
BUILD		:=	build
SOURCES		:=	source  
INCLUDES	:=	include build 
DATA		:=
 
#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-mthumb-interwork

CFLAGS	:=	-g -Wall -O2\
		-mcpu=arm7tdmi -mtune=arm7tdmi -fomit-frame-pointer\
		-ffast-math \
		$(ARCH)

CFLAGS	+=	$(INCLUDE) -DARM7
CXXFLAGS	:=	$(CFLAGS) -fno-rtti -fno-exceptions -fno-rtti


ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-g $(ARCH) -nostdlib \
		-T$(BLOCKSDS)/sys/crts/ds_arm7.ld \
		-Wl,--gc-sections \
		-Wl,--no-warn-rwx-segments \
		-Wl,-Map,$(notdir $*).map

LIBS	:=	-Wl,--start-group -ldswifi7 -lmm7 -lnds7 -lc -Wl,--end-group -lgcc

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:=	$(BLOCKSDS)/libs/dswifi \
			$(BLOCKSDS)/libs/maxmod \
			$(BLOCKSDS)/libs/libnds
 
  
#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------
 
export ARM7ELF	:=	$(CURDIR)/$(TARGET).elf
export DEPSDIR	:=	$(CURDIR)/$(BUILD)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
 
CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))
 
export OFILES	:=	$(addsuffix .o,$(BINFILES)) \
			$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
 
export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)
 
export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

.PHONY: $(BUILD) clean
 
#---------------------------------------------------------------------------------
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
 
#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) *.elf
 
 
#---------------------------------------------------------------------------------
else
 
DEPENDS	:=	$(OFILES:.o=.d)
 
#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(ARM7ELF)	:	$(OFILES)
	@echo linking $(notdir $@)
	@$(LD) -o $@ $(BLOCKSDS)/sys/crts/ds_arm7_crt0.o $(OFILES) $(LIBPATHS) $(LIBS) $(LDFLAGS)


#---------------------------------------------------------------------------------
# compile rules (previously provided by ds_rules)
#---------------------------------------------------------------------------------
%.o	:	%.c
	@echo $(notdir $<)
	@$(CC) -MMD -MP -MF $(DEPSDIR)/$*.d $(CFLAGS) $(INCLUDE) -c $< -o $@

%.o	:	%.cpp
	@echo $(notdir $<)
	@$(CXX) -MMD -MP -MF $(DEPSDIR)/$*.d $(CXXFLAGS) $(INCLUDE) -c $< -o $@

%.o	:	%.s
	@echo $(notdir $<)
	@$(CC) -MMD -MP -MF $(DEPSDIR)/$*.d -x assembler-with-cpp $(ASFLAGS) $(INCLUDE) -c $< -o $@

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	:	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(BLOCKSDS)/tools/bin2c/bin2c $< .
	@$(CC) $(CFLAGS) -MMD -MP -c -o $@ $(notdir $*).bin.c

-include $(DEPENDS)
 
#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
