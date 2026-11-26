#!/usr/bin/env python3
# Copyright (c) 2026 Project CHIP Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Exercise constructor ordering with the real Matter platform initialization."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

CHIP_ROOT = Path(__file__).resolve().parents[5]


@unittest.skipUnless(shutil.which('cmake') and shutil.which('cc') and shutil.which('c++'),
                     'Host C/C++ compilers and CMake are required')
class TestConstructorStartup(unittest.TestCase):
    def test_board_precedes_single_constructor_pass(self) -> None:
        with tempfile.TemporaryDirectory(prefix='bflb-startup-') as directory:
            source = Path(directory)
            headers = {
                'FreeRTOS.h': '#include <stdio.h>\n#include <stdlib.h>\n#define taskDISABLE_INTERRUPTS() ((void)0)\n',
                'task.h': '''\
typedef void * TaskHandle_t;
inline int xPortIsInsideInterrupt() { return 0; }
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return nullptr; }
inline const char * pcTaskGetName(TaskHandle_t) { return "test"; }
''',
                'bl_sys.h': 'void bl_sys_rstinfo_init(void);\n',
                'board.h': 'void board_init(void);\n',
                'bflb_mtd.h': 'void bflb_mtd_init(void);\n',
                'plat.h': 'extern "C" void platform_port_init(void);\n',
            }
            for name, text in headers.items():
                (source / name).write_text(text)
            (source / 'runtime.c').write_text('''\
extern void __libc_init_array(void);
extern int __real_main(void);
void __cxx_runtime_dummy(void) {}
int __wrap_main(void) {
    __libc_init_array();
    return __real_main();
}
''')
            (source / 'main.cpp').write_text('''\
#include <cstdlib>
#include <plat.h>
static int stage;
static int constructors;
extern "C" void bl_sys_rstinfo_init(void) {
    if (stage != 0) { std::exit(40); }
    stage = 1;
}
extern "C" void board_init(void) {
    if (stage != 1) { std::exit(40); }
    stage = 2;
}
extern "C" void bflb_mtd_init(void) {
    if (stage != 2) { std::exit(40); }
    stage = 3;
}
extern "C" void app_pre_matter_init(void) {
    if (stage != 3) { std::exit(40); }
    stage = 4;
}
extern "C" void __libc_init_array(void) {
    // Constructors may allocate or create RTOS objects: the board must be ready.
    if (stage != 4) { std::exit(41); }
    if (++constructors != 1) { std::exit(42); }
}
int main(void) {
    platform_port_init();
    return constructors == 1 ? 0 : 43;
}
''')
            platform = CHIP_ROOT / 'examples/platform/bouffalolab'
            (source / 'CMakeLists.txt').write_text(f'''\
cmake_minimum_required(VERSION 3.15)
project(constructor_startup C CXX)
add_library(sdk_intf_lib INTERFACE)
target_link_options(sdk_intf_lib INTERFACE -Wl,-wrap,main -Wl,-wrap,snprintf -u __cxx_runtime_dummy)
include("{platform}/bflb/bflb_common.cmake")
if(DEFER_CONSTRUCTORS)
  bflb_defer_constructors_to_platform_init()
endif()
get_target_property(opts sdk_intf_lib INTERFACE_LINK_OPTIONS)
if(NOT "-Wl,-wrap,snprintf" IN_LIST opts OR NOT "__cxx_runtime_dummy" IN_LIST opts)
  message(FATAL_ERROR "Unrelated SDK runtime link options were removed")
endif()
add_library(runtime STATIC runtime.c)
target_compile_options(runtime PRIVATE -ffunction-sections)
add_executable(startup main.cpp "{platform}/common/bouffalo_sdk/platform_port.cpp")
target_include_directories(startup PRIVATE "{source}")
target_link_options(startup PRIVATE -Wl,--gc-sections)
target_link_libraries(startup PRIVATE sdk_intf_lib runtime)
''')
            # First reproduce the pre-board constructor call, then verify the fix.
            for enabled, expected_exit in (('OFF', 41), ('ON', 0)):
                build = source / enabled
                for command in (
                    ['cmake', '-S', str(source), '-B', str(build), f'-DDEFER_CONSTRUCTORS={enabled}'],
                    ['cmake', '--build', str(build)],
                ):
                    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                    self.assertEqual(result.returncode, 0, result.stdout)
                result = subprocess.run([str(build / 'startup')], capture_output=True, text=True)
                self.assertEqual(result.returncode, expected_exit, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
