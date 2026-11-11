#
#   Copyright (c) 2026 Project CHIP Authors
#
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.
#

# =============================================================================
# CMake Setup
# =============================================================================

cmake_minimum_required(VERSION 3.15)

# =============================================================================
# Path Setup (must precede Python3_EXECUTABLE so it resolves to an absolute path)
# =============================================================================

get_filename_component(CHIP_ROOT        "${CHIP_ROOT}"             REALPATH)
get_filename_component(EXAMPLE_DIR      "${CHIP_ROOT}/examples/chip-tool/bouffalolab" REALPATH)
get_filename_component(CHIP_TOOL_DIR    "${CHIP_ROOT}/examples/chip-tool"             REALPATH)
get_filename_component(EXAMPLES_PLAT_DIR "${CHIP_ROOT}/examples/platform/bouffalolab" REALPATH)
set(GN_ROOT_TARGET      ${CMAKE_CURRENT_LIST_DIR})

set(Python3_EXECUTABLE "${CHIP_ROOT}/.environment/pigweed-venv/bin/python3"
    CACHE FILEPATH "Python3 executable" FORCE)

get_filename_component(_bl_sdk_base "${BL_SDK_BASE}" REALPATH)
set(_matter_board_dir "${CMAKE_BINARY_DIR}/matter_board")
set(_sdk_board_config_dir "${_bl_sdk_base}/bsp/board/${BOARD}/config")
file(MAKE_DIRECTORY "${_matter_board_dir}/config")
file(COPY "${_sdk_board_config_dir}/" DESTINATION "${_matter_board_dir}/config")

# The controller is a functional validation image rather than a production
# image. Use a single large FW partition and omit the commissionee-only mfg,
# MFD, media and secondary FW partitions.
file(COPY "${CMAKE_CURRENT_LIST_DIR}/flash_config/partition_cfg_4M.toml"
     DESTINATION "${_matter_board_dir}/config")

# =============================================================================
# SDK Setup
# =============================================================================

find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE} ${BL_SDK_BASE})
include(${EXAMPLES_PLAT_DIR}/bflb/bflb_common.cmake)
bflb_defer_constructors_to_platform_init()

# GN owns only its child directory; SDK configuration remains build-local.
set(MATTER_GN_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/matter-gn")
set(_matter_sdk_config_header "${CMAKE_BINARY_DIR}/generated/autoconfig.h")

set_property(GLOBAL PROPERTY FW_TOOL_CUSTOM_BOARD_DIR "${_matter_board_dir}")

set_property(TARGET app PROPERTY CXX_STANDARD 17)
set_property(TARGET app PROPERTY CXX_STANDARD_REQUIRED ON)

# Fix SDK C++11 standard and add custom compile options
get_target_property(_sdk_iface_opts sdk_intf_lib INTERFACE_COMPILE_OPTIONS)
if(_sdk_iface_opts)
  list(FILTER _sdk_iface_opts EXCLUDE REGEX "-std=c\\+\\+11")
  set_target_properties(sdk_intf_lib PROPERTIES INTERFACE_COMPILE_OPTIONS "${_sdk_iface_opts}")
endif()
sdk_add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-use-cxa-atexit>)

# =============================================================================
# Matter CMake Setup
# =============================================================================

include(ExternalProject)
include(${CHIP_ROOT}/config/common/cmake/chip_gn.cmake)
include(${CHIP_ROOT}/config/common/cmake/chip_gn_args.cmake)
include(${CHIP_ROOT}/src/app/chip_data_model.cmake)

# =============================================================================
# SDK Properties -> MATTER_CFLAGS
# =============================================================================

get_target_property(_sdk_opts sdk_intf_lib INTERFACE_COMPILE_OPTIONS)
bflb_get_sdk_definitions(_sdk_defs)
get_target_property(_sdk_incs sdk_intf_lib INTERFACE_INCLUDE_DIRECTORIES)

if(_sdk_opts)
  foreach(_flag ${_sdk_opts})
    if(_flag MATCHES "\\$<COMPILE_LANGUAGE:C>:(.+)>")
      list(APPEND MATTER_CFLAGS_C "${CMAKE_MATCH_1}")
    elseif(_flag MATCHES "\\$<COMPILE_LANGUAGE:CXX>:(.+)>")
      list(APPEND MATTER_CFLAGS_CC "${CMAKE_MATCH_1}")
    elseif(NOT _flag MATCHES "\\$<")
      list(APPEND MATTER_CFLAGS "${_flag}")
    endif()
  endforeach()
endif()

if(_sdk_defs)
  foreach(_def ${_sdk_defs})
    if(NOT _def MATCHES "\\$<")
      list(APPEND MATTER_CFLAGS "-D${_def}")
    endif()
  endforeach()
endif()

if(_sdk_incs)
  foreach(_inc ${_sdk_incs})
    if(NOT _inc MATCHES "\\$<")
      get_filename_component(_abs "${_inc}" ABSOLUTE)
      list(APPEND MATTER_CFLAGS "-I${_abs}")
    endif()
  endforeach()
endif()

list(FILTER MATTER_CFLAGS    EXCLUDE REGEX "^-W[^n]|^-Wno-format")
list(FILTER MATTER_CFLAGS_C  EXCLUDE REGEX "^-W[^n]|^-Wno-format")
list(FILTER MATTER_CFLAGS_CC EXCLUDE REGEX "^-W[^n]|^-Wno-format")
list(FILTER MATTER_CFLAGS    EXCLUDE REGEX "^-std=")
list(FILTER MATTER_CFLAGS_C  EXCLUDE REGEX "^-std=")
list(FILTER MATTER_CFLAGS_CC EXCLUDE REGEX "^-std=")

list(FILTER MATTER_CFLAGS    EXCLUDE REGEX "^-Wno-unknown-warning-option$")
list(FILTER MATTER_CFLAGS_C  EXCLUDE REGEX "^-Wno-unknown-warning-option$")
list(FILTER MATTER_CFLAGS_CC EXCLUDE REGEX "^-Wno-unknown-warning-option$")

# GCC 14 diagnoses lwIP NULL checks after separate preprocessing with
# -save-temps. Keep the SDK's diagnostic option out of Matter compilation.
foreach(_flags MATTER_CFLAGS MATTER_CFLAGS_C MATTER_CFLAGS_CC)
  list(FILTER ${_flags} EXCLUDE REGEX "^-save-temps(=.*)?$")
endforeach()

# =============================================================================
# Matter Flags
# =============================================================================

matter_add_flags(-DCONFIG_ARCH_RISCV)
matter_add_flags(-Wno-conversion)
matter_add_flags(-Wno-maybe-uninitialized)
matter_add_flags(-Wno-shadow)
matter_add_flags(-Wno-sign-compare)

list(APPEND MATTER_CFLAGS_C
  -I${CHIP_ROOT}/src/platform/bouffalolab/BFLB)
list(APPEND MATTER_CFLAGS_CC
  -I${CHIP_ROOT}/src/platform/bouffalolab/BFLB)
list(APPEND MATTER_CFLAGS_C
  -I${CHIP_ROOT}/src/platform/bouffalolab/common)
list(APPEND MATTER_CFLAGS_CC
  -I${CHIP_ROOT}/src/platform/bouffalolab/common)

list(APPEND MATTER_CFLAGS_C  -I${EXAMPLE_DIR}/bflb)
list(APPEND MATTER_CFLAGS_CC -I${EXAMPLE_DIR}/bflb)
list(APPEND MATTER_CFLAGS_C  -I${EXAMPLE_DIR}/common)
list(APPEND MATTER_CFLAGS_CC -I${EXAMPLE_DIR}/common)

list(APPEND MATTER_CFLAGS_C  -include ${_matter_sdk_config_header})
list(APPEND MATTER_CFLAGS_CC -include ${_matter_sdk_config_header})

list(APPEND MATTER_CFLAGS_C  -UMBEDTLS_NET_C)
list(APPEND MATTER_CFLAGS_CC -UMBEDTLS_NET_C)

# =============================================================================
# Network Interface Configuration (the controller uses Wi-Fi STA)
# =============================================================================

if(NOT CONFIG_WIFI STREQUAL "y")
  message(FATAL_ERROR "The controller requires CONFIG_WIFI=y.")
endif()

matter_add_gn_arg_bool("chip_enable_wifi" TRUE)
matter_add_flags(-DCHIP_DEVICE_CONFIG_ENABLE_WIFI=1)
matter_add_flags(-DCFG_IPV6=1)
list(APPEND MATTER_CFLAGS_C  -I${EXAMPLE_DIR}/bflb/wifi)
list(APPEND MATTER_CFLAGS_CC -I${EXAMPLE_DIR}/bflb/wifi)

matter_add_gn_arg_string("chip_mdns" "minimal")
matter_add_gn_arg_bool("chip_inet_config_enable_ipv4" TRUE)

# =============================================================================
# Controller GN Arguments
# =============================================================================

# FreeRTOS defaults chip_target_style to "embedded", which drops the
# controller from libCHIP; opt back in explicitly.
matter_add_gn_arg_bool("chip_build_controller" TRUE)
matter_add_gn_arg_bool("chip_enable_icd_client" TRUE)
matter_add_gn_arg_bool("chip_enable_ota_requestor" FALSE)

# Stage A: commission over IP only. Stage B flips BLE on for `pairing ble`.
matter_add_gn_arg_bool("chip_config_network_layer_ble" FALSE)

matter_add_gn_arg_bool("chip_enable_factory_data" CONFIG_MFD STREQUAL "y")
matter_add_gn_arg_bool("chip_enable_additional_data_advertising" FALSE)
matter_add_gn_arg_bool("chip_enable_rotating_device_id" FALSE)

# The Matter library (GN) must use the same compiler family as the main
# SDK build that links it; derive the GN cross_compile from the compiler
# actually selected by the SDK toolchain file.
get_filename_component(_bflb_cc_name "${CMAKE_C_COMPILER}" NAME)
string(REGEX REPLACE "gcc(-.*)?$" "" _bflb_gn_cross_compile "${_bflb_cc_name}")
bflb_print_toolchain("${_bflb_gn_cross_compile}")
matter_add_gn_arg_string("cross_compile" "${_bflb_gn_cross_compile}")
matter_add_gn_arg_list("chip_project_config_include_dirs" "\"${EXAMPLE_DIR}/bflb\"")

# Use the SDK's libc selection for GN too.
if(BFLB_USE_PICOLIBC)
  if(BFLB_PICOLIBC_SPECS)
    matter_add_flags(${BFLB_PICOLIBC_SPECS})
  endif()
  matter_add_flags(-D_DEFAULT_SOURCE)
endif()

bflb_remove_lto()

matter_common_gn_args(
  DEBUG     FALSE
  LIB_TESTS FALSE
  LIB_SHELL TRUE
)

# =============================================================================
# GN Build
# =============================================================================

add_subdirectory(${EXAMPLES_PLAT_DIR}/bflb/matter-library ${MATTER_GN_BUILD_DIR})

# =============================================================================
# SDK Include Directories
# =============================================================================

sdk_add_include_directories(
  ${CHIP_ROOT}/src/platform/bouffalolab/BFLB
  ${CHIP_ROOT}/src/platform/bouffalolab/common
  ${EXAMPLE_DIR}/bflb
  ${EXAMPLE_DIR}/common
  ${EXAMPLES_PLAT_DIR}/bflb
  ${EXAMPLES_PLAT_DIR}/common/bouffalo_sdk
)

sdk_add_include_directories(${EXAMPLE_DIR}/bflb/wifi)

# =============================================================================
# Compile Definitions
# =============================================================================

set(_COMMON_DEFS
  APP_TASK_STACK_SIZE=16384
  CONFIG_USE_LOCAL_STORAGE
  CONFIG_USE_SEPARATE_EVENTLOOP=1
  CONFIG_BUILD_FOR_HOST_UNIT_TEST=1
  ${CHIP}_ENABLE
  START_ENTRY=main
  ${CHIP}DK
  CHIP_HAVE_CONFIG_H
  BOUFFALOLAB_CHIP_TOOL_CONTROLLER=1
  BOUFFALOLAB_APP_NAME="chip-controller"
)
list(APPEND _COMMON_DEFS CHIP_DEVICE_CONFIG_ENABLE_WIFI=1)
list(APPEND _COMMON_DEFS CFG_IPV6=1)
if(CONFIG_SHELL STREQUAL "y")
  list(APPEND _COMMON_DEFS CONFIG_ENABLE_CHIP_SHELL=1)
endif()
if(CONFIG_CHIP_HEAP_MONITOR STREQUAL "y")
  list(APPEND _COMMON_DEFS HEAP_MONITORING=1)
endif()
if(CONFIG_MFD STREQUAL "y")
  list(APPEND _COMMON_DEFS CONFIG_BOUFFALOLAB_FACTORY_DATA_ENABLE=1)
endif()

target_compile_definitions(app PRIVATE ${_COMMON_DEFS})
target_compile_options(app PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-Wno-maybe-uninitialized>
  $<$<COMPILE_LANGUAGE:CXX>:-Wno-shadow>
  $<$<COMPILE_LANGUAGE:C>:-Wno-sign-compare>
  $<$<COMPILE_LANGUAGE:CXX>:-Wno-sign-compare>
)

# JsonCpp is used only by the shared argument parser. The SDK toolchain builds
# without C++ exception support, so use JsonCpp's abort-on-error mode.
target_compile_definitions(app PRIVATE JSON_USE_EXCEPTION=0)

# =============================================================================
# Target Include Directories
# =============================================================================

target_include_directories(app PRIVATE
  ${CHIP_TOOL_DIR}
  ${CHIP_TOOL_DIR}/commands
  ${CHIP_TOOL_DIR}/include
  ${CHIP_ROOT}/src
  ${CHIP_ROOT}/src/lib
  ${CHIP_ROOT}/third_party/jsoncpp/repo/include
  ${CHIP_ROOT}/zzz_generated/chip-tool
  ${CHIP_ROOT}/zzz_generated/chip-tool/zap-generated
  ${CHIP_ROOT}/zzz_generated/chip-tool/zap-generated/cluster
  ${CHIP_ROOT}/zzz_generated/chip-tool/zap-generated/cluster/logging
  ${EXAMPLES_PLAT_DIR}/common/route_hook
  ${EXAMPLES_PLAT_DIR}/common/plat
  ${MATTER_GN_BUILD_DIR}/gen/include
  ${CHIP_ROOT}/src/lib/shell
  ${CHIP_ROOT}/examples/shell/shell_common/include
  ${CHIP_ROOT}/examples/common/tracing
)

# =============================================================================
# Target Sources
# =============================================================================

target_sources(app PRIVATE
  ${EXAMPLE_DIR}/common/ControllerTask.cpp
  ${EXAMPLE_DIR}/common/ControllerCommands.cpp
  ${EXAMPLE_DIR}/common/ControllerPlatformInit.cpp
  ${EXAMPLE_DIR}/common/ControllerShell.cpp
  ${EXAMPLE_DIR}/common/KvsPersistentStorage.cpp
  ${EXAMPLES_PLAT_DIR}/common/bouffalo_sdk/platform_port.cpp
  ${EXAMPLES_PLAT_DIR}/common/route_hook/bflb_route_hook.c
  ${EXAMPLES_PLAT_DIR}/common/route_hook/bflb_route_table.c

  # chip-tool IP commissioning command subset
  ${CHIP_TOOL_DIR}/commands/common/CHIPCommand.cpp
  ${CHIP_TOOL_DIR}/commands/common/ControllerDataModelProvider.cpp
  ${CHIP_TOOL_DIR}/commands/common/Command.cpp
  ${CHIP_TOOL_DIR}/commands/common/Commands.cpp
  ${CHIP_TOOL_DIR}/commands/pairing/OpenCommissioningWindowCommand.cpp
  ${CHIP_TOOL_DIR}/commands/pairing/PairingCommand.cpp
  ${CHIP_TOOL_DIR}/commands/pairing/ToTLVCert.cpp
  ${CHIP_TOOL_DIR}/commands/session-management/CloseSessionCommand.cpp

  # CustomArgument is part of the shared command parser. Keep its JsonCpp
  # implementation in this embedded target instead of linking desktop libs.
  ${CHIP_ROOT}/third_party/jsoncpp/repo/src/lib_json/json_reader.cpp
  ${CHIP_ROOT}/third_party/jsoncpp/repo/src/lib_json/json_value.cpp
  ${CHIP_ROOT}/third_party/jsoncpp/repo/src/lib_json/json_writer.cpp

  # Matter shell helpers reused for `wifi connect` provisioning
  ${CHIP_ROOT}/examples/shell/shell_common/cmd_misc.cpp
  ${CHIP_ROOT}/examples/shell/shell_common/globals.cpp
)

# =============================================================================
# Link & Project
# =============================================================================

sdk_set_main_file(${EXAMPLE_DIR}/common/main.cpp)
sdk_add_link_libraries(chip)

set(CHIP_PROJECT_NAME "chip-controller")
project(${CHIP_PROJECT_NAME} C CXX)

target_compile_options(${proj_name}.elf PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>
)

target_compile_definitions(${proj_name}.elf PRIVATE ${_COMMON_DEFS})
