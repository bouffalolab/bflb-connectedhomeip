# =============================================================================
# SDK & Environment (chip-tool controller example for Bouffalo Lab BL618DG)
# =============================================================================

SDK_DEMO_PATH ?= $(abspath .)
CHIP_ROOT      = ../../..
BL_SDK_BASE    ?= ${CHIP_ROOT}/third_party/bouffalolab/repo_bouffalo_sdk
export BL_SDK_BASE

# The controller app targets the newer bouffalo_sdk with BL618DG support
# (strict Kconfig flow); override on the make command line, e.g.
#   make ... BL_SDK_BASE=/home/weiyin/workspace/gerrit/legacy/bouffalo_sdk

# =============================================================================
# Target Chip & Board (BL618DG AP core)
# =============================================================================

CHIP    ?= bl618dg
BOARD   ?= ${CHIP}dk
CPU_ID  ?= ap

# =============================================================================
# Network Interface (WiFi STA is the controller transport)
# =============================================================================

CONFIG_WIFI    ?= y
CONFIG_CHIP_WIFI_BACKEND_WL80211 ?= y
CONFIG_CHIP_WIFI_BACKEND_FHOST   ?= n
CONFIG_THREAD  ?= n
CONFIG_ETHERNET ?= n

ifeq ($(CONFIG_CHIP_WIFI_BACKEND_FHOST),y)
override CONFIG_CHIP_WIFI_BACKEND_WL80211 := n
else
override CONFIG_CHIP_WIFI_BACKEND_FHOST := n
override CONFIG_CHIP_WIFI_BACKEND_WL80211 := y
endif

# =============================================================================
# Optional Features
# =============================================================================

# No factory data partition: a controller does not need DAC/PAI credentials.
CONFIG_MFD                    ?= n
CONFIG_PSRAM                  ?= y
CONFIG_CHIP_HEAP_MONITOR      ?= n
CONFIG_CHIP_ROTATING_DEVICE_ID ?= n
CONFIG_COREDUMP               ?= n

# =============================================================================
# cmake Definition
# =============================================================================

cmake_definition += -DCHIP_ROOT=${CHIP_ROOT}
cmake_definition += -DBL_SDK_BASE=${BL_SDK_BASE}
cmake_definition += -DCHIP=${CHIP}
cmake_definition += -DBOARD=${BOARD}
cmake_definition += -DCPU_ID=${CPU_ID}
cmake_definition += -DCMAKE_C_FLAGS=-I$(abspath bflb)

cmake_definition += -DCONFIG_WIFI=${CONFIG_WIFI}
cmake_definition += -DCONFIG_THREAD=${CONFIG_THREAD}
cmake_definition += -DCONFIG_ETHERNET=${CONFIG_ETHERNET}

cmake_definition += -DCONFIG_SHELL=y
cmake_definition += -DCONFIG_SHELL_CMD_SIZE=512
cmake_definition += -DCONFIG_MFD=${CONFIG_MFD}
cmake_definition += -DCONFIG_PSRAM=${CONFIG_PSRAM}
cmake_definition += -DCONFIG_CHIP_ROTATING_DEVICE_ID=${CONFIG_CHIP_ROTATING_DEVICE_ID}
cmake_definition += -DCONFIG_CHIP_HEAP_MONITOR=${CONFIG_CHIP_HEAP_MONITOR}
cmake_definition += -DCONFIG_COREDUMP=${CONFIG_COREDUMP}

# =============================================================================
# SDK Build
# =============================================================================

include $(BL_SDK_BASE)/project.build
