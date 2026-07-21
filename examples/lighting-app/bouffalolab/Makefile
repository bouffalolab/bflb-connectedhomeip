# =============================================================================
# SDK & Environment
# =============================================================================

SDK_DEMO_PATH ?= $(abspath .)
CHIP_ROOT      = ../../..
BL_SDK_BASE    = ${CHIP_ROOT}/third_party/bouffalolab/repo_bouffalo_sdk
export BL_SDK_BASE

# External Bouffalo Zigbee SDK (NDA, a separate tree from BL_SDK_BASE).
# Passing its path enables the dual-stack build (CONFIG_ZIGBEE_PATH != "" turns
# CONFIG_ZIGBEE on). Example: make ... CONFIG_ZIGBEE_PATH=/path/to/bouffalo_zigbee
CONFIG_ZIGBEE_PATH ?=

CROSS_COMPILE ?= riscv64-unknown-elf-

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

# Zigbee is a separate 802.15.4 stack (external NDA tree CONFIG_ZIGBEE_PATH),
# independent of the Matter network interface. It drives the same lmac154 radio
# as Thread, so it is mutually exclusive with CONFIG_THREAD. Enabled by passing
# CONFIG_ZIGBEE_PATH (the bouffalo_zigbee tree). CONFIG_ZIGBEE_APP is the internal
# enable flag -- deliberately NOT named CONFIG_ZIGBEE: the SDK reserves CONFIG_ZIGBEE
# to mean "zigbee shipped inside the SDK" and would add_subdirectory it during
# find_package; we pull the external component ourselves (like the zigbee examples).
CONFIG_ZIGBEE_APP ?= n
ifneq ($(CONFIG_ZIGBEE_PATH),)
  CONFIG_ZIGBEE_APP = y
endif

ifeq ($(words $(filter y,$(CONFIG_WIFI) $(CONFIG_THREAD) $(CONFIG_ETHERNET))),0)
  $(error At least one network interface must be enabled: CONFIG_WIFI=y, CONFIG_THREAD=y, or CONFIG_ETHERNET=y)
endif

# Zigbee validation: must not coexist with Thread (shared lmac154 802.15.4 radio),
# and the external Zigbee SDK tree (CONFIG_ZIGBEE_PATH) must be present.
ifeq ($(CONFIG_ZIGBEE_APP),y)
  ifeq ($(CONFIG_THREAD),y)
    $(error CONFIG_ZIGBEE_PATH and CONFIG_THREAD are mutually exclusive: both drive the lmac154 802.15.4 radio.)
  endif
  ifeq ($(wildcard $(CONFIG_ZIGBEE_PATH)/components/wireless/zigbee/CMakeLists.txt),)
    $(error CONFIG_ZIGBEE_PATH is invalid (no components/wireless/zigbee/CMakeLists.txt): $(CONFIG_ZIGBEE_PATH). \
      Example: make ... CONFIG_ZIGBEE_PATH=/path/to/bouffalo_zigbee)
  endif
  # zbapp (Zigbee app framework) pulls shell.h via `#if defined(CONFIG_SHELL)`,
  # falling back to a legacy cli.h the SDK does not ship. Default the shell ON for
  # the zigbee build; the user can turn it OFF (CONFIG_SHELL=n), in which case the
  # app CMakeLists adds a cli.h/utils_string.h shim and CONFIG_ZIGBEE_CLI is forced
  # to 0 (no zigbee CLI command set, since that needs the shell). With the shell ON,
  # also connect the zigbee CLI commands (set_role, nwk_permit_join, nwk_leave,
  # zb_auto_rejoin, zb_dump_*, install-code, ...).
  ifneq ($(origin CONFIG_SHELL),command line)
    CONFIG_SHELL = y
  endif
  ifeq ($(CONFIG_SHELL),y)
    CONFIG_ZIGBEE_CLI = 1
  else
    CONFIG_ZIGBEE_CLI = 0
  endif
endif

# =============================================================================
# Thread Configuration
# =============================================================================

CONFIG_OT_FTD ?= 1
CONFIG_OT_MTD ?= 0

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

# Network interface
cmake_definition += -DCONFIG_WIFI=${CONFIG_WIFI}
cmake_definition += -DCONFIG_THREAD=${CONFIG_THREAD}
cmake_definition += -DCONFIG_ETHERNET=${CONFIG_ETHERNET}

# Zigbee (external stack); pin flash size so zigbee_def.cmake matches the app layout.
cmake_definition += -DCONFIG_ZIGBEE_APP=${CONFIG_ZIGBEE_APP}
ifeq ($(CONFIG_ZIGBEE_APP),y)
  cmake_definition += -DCONFIG_ZIGBEE_PATH=$(CONFIG_ZIGBEE_PATH)
  cmake_definition += -DCONFIG_FLASH_SIZE=0x400000
  cmake_definition += -DCONFIG_ZIGBEE_CLI=${CONFIG_ZIGBEE_CLI}
  cmake_definition += -DCONFIG_SHELL=${CONFIG_SHELL}
endif

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
    cmake_definition += -DCONFIG_OT_CLI=shell
    cmake_definition += -DCONFIG_SHELL_CMD_SIZE=256
  endif
endif

# Ethernet-specific: eth_phy and lwip EMAC driver, needed by NetworkCommissioningEthernetDriver
ifeq ($(CONFIG_ETHERNET),y)
  cmake_definition += -DCONFIG_BSP_ETH_PHY=y
  cmake_definition += -DCONFIG_BSP_LWIP_NETIF_EMAC=y
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
