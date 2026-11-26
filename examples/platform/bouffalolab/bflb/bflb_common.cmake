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

option(BFLB_LTO_OFF
       "Disable Bouffalo SDK LTO paths that fail with the current T-Head GCC toolchain on macOS Apple Silicon"
       ON)

# Matter initializes the board, allocator, and console in platform_port_init()
# before invoking __libc_init_array(). The SDK's generic main wrapper would
# invoke allocating constructors before that initialization, and then again
# from platform_port_init(). Keep the SDK startup/TLS and C++ runtime objects,
# but use Matter's existing post-board constructor initialization for these apps.
function(bflb_defer_constructors_to_platform_init)
  get_target_property(_opts sdk_intf_lib INTERFACE_LINK_OPTIONS)
  if(_opts)
    list(FILTER _opts EXCLUDE REGEX "^-Wl,-wrap,main$")
    set_target_properties(sdk_intf_lib PROPERTIES INTERFACE_LINK_OPTIONS "${_opts}")
  endif()
endfunction()

# Toolchain definitions installed with add_compile_definitions() are directory
# properties, not sdk_intf_lib usage requirements. GN must also see these (in
# particular Picolibc TLS/errno) to use the same FreeRTOS types and libc ABI.
function(bflb_get_sdk_definitions output)
  get_directory_property(_definitions COMPILE_DEFINITIONS)
  get_target_property(_interface_definitions sdk_intf_lib INTERFACE_COMPILE_DEFINITIONS)
  if(_interface_definitions)
    list(APPEND _definitions ${_interface_definitions})
  endif()
  list(REMOVE_DUPLICATES _definitions)
  set(${output} "${_definitions}" PARENT_SCOPE)
endfunction()

# Keep tool selection under CROSS_COMPILE/PATH control, but show the resolved
# tools for both build systems so installation differences are visible.
function(bflb_print_toolchain gn_prefix)
  message(STATUS "Bouffalo SDK C compiler: ${CMAKE_C_COMPILER}")
  message(STATUS "Bouffalo SDK C++ compiler: ${CMAKE_CXX_COMPILER}")
  message(STATUS "Bouffalo SDK archiver: ${CMAKE_AR}")
  message(STATUS "Bouffalo SDK ISA/ABI: ${BFLB_RISCV_ARCH_FLAGS}")
  foreach(_tool gcc g++ ar)
    unset(_bflb_tool CACHE)
    find_program(_bflb_tool NAMES "${gn_prefix}${_tool}")
    if(NOT _bflb_tool)
      message(FATAL_ERROR "Matter GN tool not found: ${gn_prefix}${_tool}")
    endif()
    execute_process(COMMAND "${_bflb_tool}" --version
      OUTPUT_VARIABLE _version OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
      message(FATAL_ERROR "Cannot query Matter GN tool: ${_bflb_tool}")
    endif()
    string(REGEX REPLACE "\n.*" "" _version "${_version}")
    message(STATUS "Matter GN ${_tool}: ${_bflb_tool} (${_version})")
  endforeach()
  unset(_bflb_tool CACHE)
endfunction()

function(bflb_remove_lto_option target property)
  get_target_property(_opts ${target} ${property})
  if(_opts)
    list(FILTER _opts EXCLUDE REGEX "^-flto(=.*)?$")
    set_target_properties(${target} PROPERTIES ${property} "${_opts}")
  endif()
endfunction()

macro(bflb_remove_lto)
  if(BFLB_LTO_OFF)
    # The current T-Head GCC 10.2 LTO flow fails on macOS Apple Silicon:
    # - Matter LTO objects fail during static library archiving.
    # - BL616 prebuilt Wi-Fi archives can fail lto-wrapper with
    #   "original not compressed with zstd" at final link.
    # Linux hosts have not reproduced these failures. When the toolchain is
    # updated, disable BFLB_LTO_OFF to restore LTO quickly.
    bflb_remove_lto_option(sdk_intf_lib INTERFACE_COMPILE_OPTIONS)
    bflb_remove_lto_option(sdk_intf_lib INTERFACE_LINK_OPTIONS)

    foreach(_flags_var MATTER_CFLAGS MATTER_CFLAGS_C MATTER_CFLAGS_CC)
      if(DEFINED ${_flags_var})
        list(FILTER ${_flags_var} EXCLUDE REGEX "^-flto(=.*)?$")
      endif()
    endforeach()

    sdk_add_link_options(-fno-use-linker-plugin)
  endif()
endmacro()
