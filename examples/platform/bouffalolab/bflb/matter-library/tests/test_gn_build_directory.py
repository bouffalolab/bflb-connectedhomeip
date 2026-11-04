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

"""Exercise the Bouffalo CMake/GN boundary without compiling Matter."""

import concurrent.futures
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

CHIP_ROOT = Path(__file__).resolve().parents[6]


@unittest.skipUnless(shutil.which('cmake') and shutil.which('ninja'), 'CMake and Ninja are required')
class TestGnBuildDirectory(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(prefix='matter-gn-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.tools = self.root / 'tools'
        self.tools.mkdir()
        # Substitute only GN: CMake, argument conversion and Ninja are real.
        gn = self.tools / 'gn'
        gn.write_text(f'#!{sys.executable}\n' + '''\
import pathlib
import sys
if '--version' in sys.argv:
    print('2000')
else:
    output = pathlib.Path(sys.argv[-1])
    (output / 'lib').mkdir(exist_ok=True)
    (output / 'lib/libCHIP.a').write_bytes(b'!<arch>\\n')
    (output / 'build.ninja').write_text('build lib/libCHIP.a: phony\\ndefault lib/libCHIP.a\\n')
''')
        gn.chmod(0o755)
        self.env = dict(os.environ, PATH=str(self.tools) + os.pathsep + os.environ['PATH'])

    def configure_and_build(self, name: str) -> tuple[Path, Path]:
        source = self.root / ('source-' + name)
        build = self.root / name
        source.mkdir()
        gn_dir = build / 'matter-gn'
        (source / 'CMakeLists.txt').write_text(f'''\
cmake_minimum_required(VERSION 3.20)
project(gn_directory_test NONE)
include(ExternalProject)
set(CHIP_ROOT "{CHIP_ROOT}")
set(GN_ROOT_TARGET "{source}")
set(Python3_EXECUTABLE "{sys.executable}")
include("{CHIP_ROOT}/config/common/cmake/chip_gn.cmake")
include("{CHIP_ROOT}/config/common/cmake/chip_gn_args.cmake")
matter_add_gn_arg_string(configuration "{name}")
add_subdirectory("{CHIP_ROOT}/examples/platform/bouffalolab/bflb/matter-library"
                 "${{CMAKE_CURRENT_BINARY_DIR}}/matter-gn")
file(GENERATE OUTPUT "${{CMAKE_CURRENT_BINARY_DIR}}/chip-includes.txt"
     CONTENT "$<TARGET_PROPERTY:chip,INTERFACE_INCLUDE_DIRECTORIES>")
add_custom_target(consume ALL
    COMMAND "${{CMAKE_COMMAND}}" -E compare_files
            "{gn_dir}/lib/libCHIP.a" "{source}/expected.a")
add_dependencies(consume chip)
''')
        (source / 'expected.a').write_bytes(b'!<arch>\n')
        generator = 'Ninja'
        result = subprocess.run(['cmake', '-S', str(source), '-B', str(build), '-G', generator],
                                env=self.env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode, 0, result.stdout)
        for _ in range(2):
            # An incremental build must retain both CMake and GN build graphs.
            result = subprocess.run(['cmake', '--build', str(build)], env=self.env, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn(str(gn_dir / 'gen/include'), (build / 'chip-includes.txt').read_text())
        self.assertIn(f'configuration = "{name}"', (gn_dir / 'args.gn').read_text())
        return build, gn_dir

    def test_separate_directory_preserves_cmake_ninja_graph(self) -> None:
        build, gn_dir = self.configure_and_build('isolated')
        self.assertIn('CMake', (build / 'build.ninja').read_text())
        self.assertTrue((gn_dir / 'build.ninja').is_file())
        self.assertFalse((build / 'args.tmp').exists())

    def test_sibling_builds_do_not_share_arguments(self) -> None:
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
            results = list(executor.map(self.configure_and_build, ['wifi', 'thread']))
        self.assertNotEqual(results[0][1], results[1][1])


if __name__ == '__main__':
    unittest.main()
