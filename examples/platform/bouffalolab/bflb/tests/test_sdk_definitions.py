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

"""Check that GN receives directory-level toolchain definitions too."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

CHIP_ROOT = Path(__file__).resolve().parents[5]


@unittest.skipUnless(shutil.which('cmake'), 'CMake is required')
class TestSdkDefinitions(unittest.TestCase):
    def test_toolchain_and_target_definitions_are_exported(self) -> None:
        with tempfile.TemporaryDirectory(prefix='bflb-definitions-') as directory:
            source = Path(directory)
            (source / 'CMakeLists.txt').write_text(f'''\
cmake_minimum_required(VERSION 3.15)
project(sdk_definitions NONE)
# Match the SDK toolchain's directory-level definitions.
add_compile_definitions(BFLB_USE_PICOLIBC=1 __PICOLIBC_ERRNO_FUNCTION=__errno)
add_library(sdk_intf_lib INTERFACE)
target_compile_definitions(sdk_intf_lib INTERFACE CONFIG_FREERTOS BFLB_USE_PICOLIBC=1)
include("{CHIP_ROOT}/examples/platform/bouffalolab/bflb/bflb_common.cmake")
bflb_get_sdk_definitions(definitions)
file(WRITE "${{CMAKE_BINARY_DIR}}/definitions.txt" "${{definitions}}")
''')
            build = source / 'build'
            result = subprocess.run(['cmake', '-S', str(source), '-B', str(build)],
                                    text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            self.assertEqual(result.returncode, 0, result.stdout)
            definitions = (build / 'definitions.txt').read_text().split(';')
            self.assertCountEqual(definitions, ['BFLB_USE_PICOLIBC=1', '__PICOLIBC_ERRNO_FUNCTION=__errno',
                                                'CONFIG_FREERTOS'])


if __name__ == '__main__':
    unittest.main()
