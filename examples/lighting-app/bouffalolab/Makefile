# =============================================================================
# SDK & Environment
# =============================================================================

SDK_DEMO_PATH ?= $(abspath .)
CHIP_ROOT      = ../../..
BL_SDK_BASE   ?= ${CHIP_ROOT}/third_party/bouffalolab/repo_bouffalo_sdk
export BL_SDK_BASE

# The SDK board.build selects the default riscv64-unknown-elf- Zephyr GCC
# toolchain. Pass CROSS_COMPILE explicitly only for another compatible toolchain.

# =============================================================================
# Target Chip & Board
# =============================================================================

CHIP  ?= bl616
BOARD ?= ${CHIP}dk

# =============================================================================
# Network Interface (at least one must be y)
# =============================================================================

ifeq ($(origin CONFIG_WIFI)$(origin CONFIG_THREAD)$(origin CONFIG_ETHERNET),undefinedundefinedundefined)
  CONFIG_WIFI ?= y
else
  CONFIG_WIFI ?= n
endif
CONFIG_THREAD   ?= n
CONFIG_ETHERNET ?= n

ifeq ($(words $(filter y,$(CONFIG_WIFI) $(CONFIG_THREAD) $(CONFIG_ETHERNET))),0)
  $(error At least one network interface must be enabled: CONFIG_WIFI=y, CONFIG_THREAD=y, or CONFIG_ETHERNET=y)
endif

# =============================================================================
# Thread Configuration
# =============================================================================

# Values must be valid Kconfig bool assignments (y/n)
CONFIG_OT_FTD ?= y
CONFIG_OT_MTD ?= n

# =============================================================================
# Optional Features
# =============================================================================

CONFIG_SHELL                  ?= n
CONFIG_MFD                    ?= y
CONFIG_CHIP_ROTATING_DEVICE_ID ?= n
CONFIG_CHIP_HEAP_MONITOR       ?= n
CONFIG_COREDUMP                ?= n

# =============================================================================
# cmake Definition
# =============================================================================

# Basic
cmake_definition += -DCHIP_ROOT=${CHIP_ROOT}
cmake_definition += -DBL_SDK_BASE=${BL_SDK_BASE}
# CHIP/BOARD must resolve before find_package(bouffalo_sdk) loads the
# generated configuration (the bouffalo_sdk >= 2.3.3x build does not pass
# them on the cmake command line itself).
cmake_definition += -DCHIP=${CHIP}
cmake_definition += -DBOARD=${BOARD}
# The bouffalo_sdk >= 2.3.3x wireless sub-builds compile the BLE controller
# from source with the FreeRTOS port glue; their only application include
# directory is the example root (SDK_DEMO_PATH). The FreeRTOS configuration
# lives in bflb/, so propagate it to the sub-builds.
cmake_definition += -DCMAKE_C_FLAGS=-I$(abspath bflb)

# Network interface
cmake_definition += -DCONFIG_WIFI=${CONFIG_WIFI}
cmake_definition += -DCONFIG_THREAD=${CONFIG_THREAD}
cmake_definition += -DCONFIG_ETHERNET=${CONFIG_ETHERNET}

# Optional features
cmake_definition += -DCONFIG_SHELL=${CONFIG_SHELL}
cmake_definition += -DCONFIG_MFD=${CONFIG_MFD}
cmake_definition += -DCONFIG_CHIP_ROTATING_DEVICE_ID=${CONFIG_CHIP_ROTATING_DEVICE_ID}
cmake_definition += -DCONFIG_CHIP_HEAP_MONITOR=${CONFIG_CHIP_HEAP_MONITOR}
cmake_definition += -DCONFIG_COREDUMP=${CONFIG_COREDUMP}

# Thread-specific
ifeq ($(CONFIG_THREAD),y)
  cmake_definition += -DCONFIG_OT_FTD=${CONFIG_OT_FTD}
  cmake_definition += -DCONFIG_OT_MTD=${CONFIG_OT_MTD}
  cmake_definition += -DOPENTHREAD_PROJECT_CORE_CONFIG_FILE=\"openthread-core-proj-config.h\"
  cmake_definition += -DOPENTHREAD_CORE_CONFIG_PLATFORM_CHECK_FILE=\"openthread-core-platform-config-check.h\"
  ifeq ($(CONFIG_SHELL),y)
    # SDK configuration is generated from Make CONFIG_* variables. CMake-only
    # values are overwritten when generated/autoconfig.cmake is loaded.
    CONFIG_OT_CLI = shell
    CONFIG_SHELL_CMD_SIZE = 256
  endif
endif

# Ethernet-specific: eth_phy and lwip EMAC driver, needed by NetworkCommissioningEthernetDriver
ifeq ($(CONFIG_ETHERNET),y)
  CONFIG_BSP_ETH_PHY = y
  CONFIG_BSP_LWIP_NETIF_EMAC = y
  CONFIG_BSP_LWIP_EMAC_START = y
  # SDK EMAC startup reads the factory MAC through the RF manufacturing API.
  CONFIG_RF = y
endif

# =============================================================================
# SDK Build
# =============================================================================

include $(BL_SDK_BASE)/project.build

# =============================================================================
# Flash Target (with optional MFD)
# =============================================================================

ifneq ($(MFD_FILE),)
MFD_FILE_ABS := $(if $(wildcard $(MFD_FILE)),$(abspath $(MFD_FILE)),$(abspath $(CHIP_ROOT)/$(MFD_FILE)))
command_flash_without_mfd := $(command_flash)
override command_flash = test -f "$(MFD_FILE_ABS)" || { echo "[flash] MFD file not found: $(MFD_FILE)"; exit 1; }; \
	test -d "$(BUILD_DIR)/build_out" || { echo "[flash] BUILD_DIR not found: $(BUILD_DIR)"; exit 1; }; \
	echo "[flash] adding MFD bin: $(MFD_FILE_ABS)"; \
	echo "[flash] using build output: $(abspath $(BUILD_DIR)/build_out)"; \
	sed 's|\./build/build_out|./$(BUILD_DIR)/build_out|g' flash_prog_cfg.ini > flash_prog_cfg.mfd.ini; \
	trap 'rm -f flash_prog_cfg.mfd.ini' EXIT; \
	printf '\n[MFD]\nfiledir = $(MFD_FILE_ABS)\naddress = @partition\n' >> flash_prog_cfg.mfd.ini; \
	$(subst --config=flash_prog_cfg.ini,--config=flash_prog_cfg.mfd.ini,$(command_flash_without_mfd))
endif
