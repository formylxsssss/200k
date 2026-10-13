##########################################################################################################################
# File generated in the same Makefile style as channel1_device.
# MSYS2-oriented STM32U575 Makefile with J-Link flash/erase/run targets.
##########################################################################################################################

######################################
# target
######################################
TARGET = single_channel_device

ALLCSRC =
ALLINC =
include device/device.mk
include ARM_SEGGER_RTT/segger_rtt.mk
include Middlewares/Third_Party/FatFs/fatfs.mk

######################################
# building variables
######################################
DEBUG ?= 1
OPT ?= -O2

#######################################
# paths
#######################################
BUILD_DIR ?= build
STM32CUBE_U5_PATH ?= .
TOOLCHAIN_LIB_DIR ?= ToolchainLib/gcc-arm-11.2.1/thumb/v8-m.main+fp/hard
NANO_SPECS ?= $(TOOLCHAIN_LIB_DIR)/nano.specs

######################################
# source
######################################
C_SOURCES =  \
Core/Src/main.c \
Core/Src/gpio.c \
Core/Src/sdmmc.c \
Core/Src/spi.c \
Core/Src/tim.c \
Core/Src/stm32u5xx_it.c \
Core/Src/stm32u5xx_hal_msp.c \
Core/Src/syscalls.c \
$(STM32CUBE_U5_PATH)/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal.c \
$(STM32CUBE_U5_PATH)/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_cortex.c \
$(STM32CUBE_U5_PATH)/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_dma.c \
$(STM32CUBE_U5_PATH)/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_exti.c \
$(STM32CUBE_U5_PATH)/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_flash.c \
$(STM32CUBE_U5_PATH)/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_flash_ex.c \
$(STM32CUBE_U5_PATH)/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_gpio.c \
$(STM32CUBE_U5_PATH)/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_pwr.c \
$(STM32CUBE_U5_PATH)/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_pwr_ex.c \
$(STM32CUBE_U5_PATH)/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_rcc.c \
$(STM32CUBE_U5_PATH)/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_rcc_ex.c \
$(STM32CUBE_U5_PATH)/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_sd.c \
$(STM32CUBE_U5_PATH)/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_ll_sdmmc.c \
$(STM32CUBE_U5_PATH)/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_spi.c \
$(STM32CUBE_U5_PATH)/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_tim.c \
$(STM32CUBE_U5_PATH)/Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_tim_ex.c \
$(STM32CUBE_U5_PATH)/Drivers/CMSIS/Device/ST/STM32U5xx/Source/Templates/system_stm32u5xx.c

C_SOURCES += $(ALLCSRC)

# ASM sources
ASM_SOURCES =  \
$(STM32CUBE_U5_PATH)/Drivers/CMSIS/Device/ST/STM32U5xx/Source/Templates/gcc/startup_stm32u575xx.s

# ASMM sources
ASMM_SOURCES =

#######################################
# host and external tools
#######################################
# Compatible with the same environment variables used by channel1_device:
#
#   GCC       = D:\gcc\<toolchain-root>
#   FLASH_RTT = D:\J-LINK\JLink_V794
#
# Command-line overrides:
#
#   make GCC_PATH=/d/gcc/toolchain/bin
#   make JLINK_ROOT=/d/J-LINK/JLink_V794 flash

.DEFAULT_GOAL := all

AUTO_PARALLEL ?= 1
JOBS ?= $(if $(NUMBER_OF_PROCESSORS),$(NUMBER_OF_PROCESSORS),$(shell nproc 2>/dev/null || echo 4))
GENERATE_LST ?= 0
USE_PIPE ?= 1
GENERATE_CREF ?= 0

MAKEFLAGS += --no-builtin-rules --no-builtin-variables
.SUFFIXES:

normalize_path = $(subst \,/,$(strip $(1)))

ifeq ($(OS),Windows_NT)
HOST_OS := windows
EXEEXT := .exe
else
HOST_OS := linux
EXEEXT :=
endif

GCC_PATH ?=
GCC_ROOT ?= $(GCC)
JLINK_ROOT ?= $(FLASH_RTT)

GCC_PATH := $(call normalize_path,$(GCC_PATH))
GCC_ROOT := $(call normalize_path,$(GCC_ROOT))
JLINK_ROOT := $(call normalize_path,$(JLINK_ROOT))
STM32CUBE_U5_PATH := $(call normalize_path,$(STM32CUBE_U5_PATH))
TOOLCHAIN_LIB_DIR := $(call normalize_path,$(TOOLCHAIN_LIB_DIR))
NANO_SPECS := $(call normalize_path,$(NANO_SPECS))

ifeq ($(HOST_OS),windows)

ifeq ($(strip $(GCC_PATH)),)
ifneq ($(strip $(GCC_ROOT)),)
ifneq ($(wildcard $(GCC_ROOT)/arm-none-eabi-gcc.exe),)
GCC_PATH := $(GCC_ROOT)
else
GCC_PATH := $(GCC_ROOT)/bin
endif
else
AUTO_GCC_EXE := $(firstword $(wildcard D:/gcc/*/bin/arm-none-eabi-gcc.exe) $(wildcard C:/Tools/*/bin/arm-none-eabi-gcc.exe))
ifneq ($(strip $(AUTO_GCC_EXE)),)
GCC_PATH := $(patsubst %/arm-none-eabi-gcc.exe,%,$(AUTO_GCC_EXE))
endif
endif
endif

ifeq ($(strip $(JLINK_ROOT)),)
AUTO_JLINK_EXE := $(firstword $(wildcard D:/J-LINK/JLink_V*/JLink.exe) $(wildcard C:/Program\ Files/SEGGER/JLink*/JLink.exe))
ifneq ($(strip $(AUTO_JLINK_EXE)),)
JLINK_ROOT := $(patsubst %/JLink.exe,%,$(AUTO_JLINK_EXE))
endif
endif

else

ifeq ($(strip $(GCC_PATH)),)
ifneq ($(wildcard /opt/gcc-arm-none-eabi-9-2020-q2-update/bin/arm-none-eabi-gcc),)
GCC_PATH := /opt/gcc-arm-none-eabi-9-2020-q2-update/bin
endif
endif

endif

#######################################
# binaries
#######################################
PREFIX := arm-none-eabi-

ifeq ($(strip $(GCC_PATH)),)
CC := $(PREFIX)gcc$(EXEEXT)
AS := $(PREFIX)gcc$(EXEEXT)
CP := $(PREFIX)objcopy$(EXEEXT)
SZ := $(PREFIX)size$(EXEEXT)
else
CC := $(GCC_PATH)/$(PREFIX)gcc$(EXEEXT)
AS := $(GCC_PATH)/$(PREFIX)gcc$(EXEEXT)
CP := $(GCC_PATH)/$(PREFIX)objcopy$(EXEEXT)
SZ := $(GCC_PATH)/$(PREFIX)size$(EXEEXT)
endif

ifeq ($(strip $(JLINK_ROOT)),)
ifeq ($(HOST_OS),windows)
JLINK_EXE := JLink.exe
RTT_CLIENT_EXE := JLinkRTTClient.exe
RTT_VIEWER_EXE := JLinkRTTViewer.exe
else
JLINK_EXE := JLinkExe
RTT_CLIENT_EXE := JLinkRTTClient
RTT_VIEWER_EXE := JLinkRTTViewerExe
endif
else
ifeq ($(HOST_OS),windows)
JLINK_EXE := $(JLINK_ROOT)/JLink.exe
RTT_CLIENT_EXE := $(JLINK_ROOT)/JLinkRTTClient.exe
RTT_VIEWER_EXE := $(JLINK_ROOT)/JLinkRTTViewer.exe
else
JLINK_EXE := $(JLINK_ROOT)/JLinkExe
RTT_CLIENT_EXE := $(JLINK_ROOT)/JLinkRTTClient
RTT_VIEWER_EXE := $(JLINK_ROOT)/JLinkRTTViewerExe
endif
endif

HEX = "$(CP)" -O ihex
BIN = "$(CP)" -O binary -S

#######################################
# CFLAGS
#######################################
CPU = -mcpu=cortex-m33
FPU = -mfpu=fpv5-sp-d16
FLOAT-ABI = -mfloat-abi=hard
MCU = $(CPU) -mthumb $(FPU) $(FLOAT-ABI)

AS_DEFS =
C_DEFS =  \
-DUSE_HAL_DRIVER \
-DSTM32U575xx

AS_INCLUDES =
C_INCLUDES =  \
-ICore/Inc \
-I$(STM32CUBE_U5_PATH)/Drivers/STM32U5xx_HAL_Driver/Inc \
-I$(STM32CUBE_U5_PATH)/Drivers/STM32U5xx_HAL_Driver/Inc/Legacy \
-I$(STM32CUBE_U5_PATH)/Drivers/CMSIS/Device/ST/STM32U5xx/Include \
-I$(STM32CUBE_U5_PATH)/Drivers/CMSIS/Include

C_INCLUDES += $(addprefix -I,$(ALLINC))

ASFLAGS = $(MCU) $(AS_DEFS) $(AS_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections
CFLAGS += $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -Wall -Wextra -fdata-sections -ffunction-sections

ifeq ($(USE_PIPE),1)
CFLAGS += -pipe
ASFLAGS += -pipe
endif

ifeq ($(DEBUG), 1)
CFLAGS += -g3
endif

CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"

#######################################
# LDFLAGS
#######################################
LDSCRIPT = STM32U575RGTX_FLASH.ld
LIBS = -lc -lm -lnosys
LIBDIR = -L$(TOOLCHAIN_LIB_DIR)

ifeq ($(GENERATE_CREF),1)
MAP_FLAGS = -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref
else
MAP_FLAGS = -Wl,-Map=$(BUILD_DIR)/$(TARGET).map
endif

LDFLAGS = $(MCU) -nostartfiles -specs=$(NANO_SPECS) -T$(LDSCRIPT) $(LIBDIR) $(LIBS) $(MAP_FLAGS) -Wl,--gc-sections

.PHONY: all firmware clean flash erase run telnet rtt viewer info help \
        check-tools check-gcc check-binutils check-jlink check-rtt-client

firmware: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

ifeq ($(AUTO_PARALLEL),1)
all:
	+@$(MAKE) --no-print-directory -j$(JOBS) AUTO_PARALLEL=0 firmware
else
all: firmware
endif

check-gcc:
	@if [ -x "$(CC)" ] || command -v "$(CC)" >/dev/null 2>&1; then \
		:; \
	else \
		echo "ERROR: ARM GCC was not found."; \
		echo "Resolved compiler: $(CC)"; \
		echo "Set GCC to the toolchain root or run: make GCC_PATH=/d/path/to/toolchain/bin"; \
		exit 1; \
	fi

check-binutils:
	@if { [ -x "$(CP)" ] || command -v "$(CP)" >/dev/null 2>&1; } && \
	    { [ -x "$(SZ)" ] || command -v "$(SZ)" >/dev/null 2>&1; }; then \
		:; \
	else \
		echo "ERROR: arm-none-eabi-objcopy or arm-none-eabi-size was not found."; \
		echo "Resolved objcopy: $(CP)"; \
		echo "Resolved size:    $(SZ)"; \
		exit 1; \
	fi

check-tools: check-gcc check-binutils

check-jlink:
	@if [ -x "$(JLINK_EXE)" ] || command -v "$(JLINK_EXE)" >/dev/null 2>&1; then \
		:; \
	else \
		echo "ERROR: SEGGER J-Link Commander was not found."; \
		echo "Resolved J-Link: $(JLINK_EXE)"; \
		echo "Set FLASH_RTT or run: make JLINK_ROOT=/d/J-LINK/JLink_Vxxx"; \
		exit 1; \
	fi

check-rtt-client:
	@if [ -x "$(RTT_CLIENT_EXE)" ] || command -v "$(RTT_CLIENT_EXE)" >/dev/null 2>&1; then \
		:; \
	else \
		echo "ERROR: SEGGER J-Link RTT Client was not found."; \
		echo "Resolved RTT client: $(RTT_CLIENT_EXE)"; \
		exit 1; \
	fi

info:
	@echo "HOST_OS          = $(HOST_OS)"
	@echo "GCC environment  = $(GCC)"
	@echo "GCC_ROOT         = $(GCC_ROOT)"
	@echo "GCC_PATH         = $(GCC_PATH)"
	@echo "CC               = $(CC)"
	@echo "CP               = $(CP)"
	@echo "SZ               = $(SZ)"
	@echo "FLASH_RTT env    = $(FLASH_RTT)"
	@echo "JLINK_ROOT       = $(JLINK_ROOT)"
	@echo "JLINK_EXE        = $(JLINK_EXE)"
	@echo "RTT_CLIENT_EXE   = $(RTT_CLIENT_EXE)"
	@echo "BUILD_DIR        = $(BUILD_DIR)"
	@echo "TARGET           = $(TARGET)"
	@echo "STM32CUBE_U5_PATH= $(STM32CUBE_U5_PATH)"
	@echo "TOOLCHAIN_LIB_DIR= $(TOOLCHAIN_LIB_DIR)"
	@echo "NANO_SPECS       = $(NANO_SPECS)"
	@echo "AUTO_PARALLEL    = $(AUTO_PARALLEL)"
	@echo "MAKEFLAGS        = $(MAKEFLAGS)"
	@echo "JOBS             = $(JOBS)"
	@echo "GENERATE_LST     = $(GENERATE_LST)"
	@echo "USE_PIPE         = $(USE_PIPE)"
	@echo "GENERATE_CREF    = $(GENERATE_CREF)"

help:
	@echo "Available targets:"
	@echo "  make info        - show all resolved tool paths"
	@echo "  make check-tools - verify ARM GCC and binutils"
	@echo "  make             - safe parallel build using all logical CPU cores"
	@echo "  make JOBS=8      - safe parallel build with a fixed job count"
	@echo "  make GENERATE_LST=1 - generate assembly listing files"
	@echo "  make GENERATE_CREF=1 - generate map cross-reference table"
	@echo "  make clean       - remove the build directory"
	@echo "  make flash       - build and program with J-Link"
	@echo "  make erase       - erase the MCU with J-Link"
	@echo "  make run         - start J-Link Commander and RTT Telnet server"
	@echo "  make telnet      - connect with JLinkRTTClient"
	@echo "  make viewer      - open JLinkRTTViewer"

#######################################
# build the application
#######################################
OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))

OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASMM_SOURCES:.S=.o)))
vpath %.S $(sort $(dir $(ASMM_SOURCES)))

$(OBJECTS): | check-tools

ifeq ($(GENERATE_LST),1)
C_LISTING_FLAG = -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst))
else
C_LISTING_FLAG =
endif

$(BUILD_DIR)/ff.o: CFLAGS += -Wno-implicit-fallthrough

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	@"$(CC)" -c $(CFLAGS) $(C_LISTING_FLAG) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	@"$(AS)" -x assembler-with-cpp -c $(ASFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.S Makefile | $(BUILD_DIR)
	@"$(AS)" -x assembler-with-cpp -c $(ASFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile | check-tools
	@"$(CC)" $(OBJECTS) $(LDFLAGS) -o $@
	@"$(SZ)" $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf | $(BUILD_DIR) check-binutils
	$(HEX) $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf | $(BUILD_DIR) check-binutils
	$(BIN) $< $@

$(BUILD_DIR):
	@mkdir -p $@

#######################################
# clean, programming and RTT
#######################################
JLINK_SN ?= 20722677
JLINK_DEVICE ?= STM32U575RG
JLINK_IF ?= SWD
JLINK_SPEED ?= 4000
JLINK_RUN_SPEED ?= 2400
JLINK_RTT_PORT ?= 6666

clean:
	@rm -rf "$(BUILD_DIR)"

flash: all check-jlink
	@echo "Uploading firmware with J-Link..."
	@"$(JLINK_EXE)" \
		-SelectEmuBySN $(JLINK_SN) \
		-Device $(JLINK_DEVICE) \
		-if $(JLINK_IF) \
		-Speed $(JLINK_SPEED) \
		-CommandFile ./flash.jlink

erase: check-jlink
	@echo "Erasing chip with J-Link..."
	@"$(JLINK_EXE)" \
		-SelectEmuBySN $(JLINK_SN) \
		-Device $(JLINK_DEVICE) \
		-if $(JLINK_IF) \
		-Speed $(JLINK_SPEED) \
		-CommandFile ./erase.jlink

run: check-jlink
	@echo "Starting J-Link Commander and RTT server on port $(JLINK_RTT_PORT)..."
	@"$(JLINK_EXE)" \
		-SelectEmuBySN $(JLINK_SN) \
		-Device $(JLINK_DEVICE) \
		-if $(JLINK_IF) \
		-Speed $(JLINK_RUN_SPEED) \
		-RTTTelnetPort $(JLINK_RTT_PORT) \
		-autoconnect 1

telnet: check-rtt-client
	@echo "Connecting to RTT server on localhost:$(JLINK_RTT_PORT)..."
	@while true; do \
		"$(RTT_CLIENT_EXE)" -RTTTelnetPort $(JLINK_RTT_PORT); \
		echo "RTT connection closed; retrying in 1 second..."; \
		sleep 1; \
	done

rtt: telnet

viewer:
	@if [ -x "$(RTT_VIEWER_EXE)" ] || command -v "$(RTT_VIEWER_EXE)" >/dev/null 2>&1; then \
		"$(RTT_VIEWER_EXE)"; \
	else \
		echo "ERROR: JLinkRTTViewer was not found: $(RTT_VIEWER_EXE)"; \
		exit 1; \
	fi

#######################################
# dependencies
#######################################
-include $(wildcard $(BUILD_DIR)/*.d)

# *** EOF ***
