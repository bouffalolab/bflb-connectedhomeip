# Copyright (c) 2026 Project CHIP Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os
import runpy
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

from builders.bouffalolab import BouffalolabBflbBuilder


class TestBouffalolabBflbBuilder(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        environment = patch.dict(os.environ, {'PATH': '/usr/bin'}, clear=True)
        environment.start()
        self.addCleanup(environment.stop)
        self.builder = BouffalolabBflbBuilder(str(self.root), Mock(), enable_wifi=True)
        self.builder.identifier = 'bouffalolab-bl616dk-light-wifi-littlefs'
        self.builder.output_dir = str(self.root / 'out' / self.builder.identifier)

    def test_explicit_sdk_and_toolchain_reach_make(self):
        sdk = self.root / 'custom sdk'
        prefix = str(self.root / 'custom toolchain' / 'riscv64-unknown-elf-')
        with patch.dict(os.environ, {'BL_SDK_BASE': str(sdk), 'CROSS_COMPILE': prefix}), \
                patch.object(self.builder, '_Execute') as execute, \
                patch.object(self.builder, '_post_build'):
            self.builder._build()
        command = execute.call_args.args[0]
        self.assertIn(f'BL_SDK_BASE={sdk}', command)
        self.assertIn(f'CROSS_COMPILE={prefix}', command)
        self.assertIn(f'{os.path.dirname(prefix)}{os.pathsep}/usr/bin', command[1])

    def test_path_toolchain_precedes_legacy_install(self):
        compiler = '/selected/toolchain/bin/riscv64-unknown-elf-gcc'
        with patch.dict(os.environ, {'BOUFFALOLAB_SDK_ROOT': '/legacy/sdk'}), \
                patch('builders.bouffalolab.shutil.which', return_value=compiler), \
                patch('builders.bouffalolab.os.path.isfile', return_value=True):
            self.assertEqual(self.builder._cross_compile_prefix(), compiler[:-3])

    def test_default_sdk_is_repo_checkout(self):
        self.assertEqual(self.builder._sdk_base(), str(self.root / 'third_party/bouffalolab/repo_bouffalo_sdk'))

    def test_thread_mtd_uses_kconfig_role(self):
        builder = BouffalolabBflbBuilder(str(self.root), Mock(), enable_thread=True, enable_thread_ftd=False)
        self.assertIn('CONFIG_OT_FTD=n', builder._make_args())
        self.assertIn('CONFIG_OT_MTD=y', builder._make_args())

    def test_flash_wrapper_uses_selected_sdk_and_packaged_output(self):
        sdk_out = Path(self.builder.root) / self.builder._sdk_build_dir() / 'build_out'
        sdk_out.mkdir(parents=True)
        for extension in ('bin', 'elf'):
            (sdk_out / f'chip-bflb-lighting-example_bl616.{extension}').touch()
        ota = sdk_out / 'ota_images' / 'firmware.matter'
        ota.parent.mkdir()
        ota.touch()
        sdk = str(self.root / 'selected sdk')
        with patch.dict(os.environ, {'BL_SDK_BASE': sdk}):
            self.builder._post_build()
        output = Path(self.builder.output_dir)
        self.assertEqual((output / 'chip-bl616-lighting-example.bin').resolve(),
                         sdk_out / 'chip-bflb-lighting-example_bl616.bin')
        self.assertEqual((output / 'ota_images/firmware.matter').resolve(), ota)
        script = output / 'chip-bl616-lighting-example.flash.py'
        with patch('sys.argv', [str(script), '--port', '/dev/ttyUSB9']), patch('subprocess.run') as run:
            runpy.run_path(str(script), run_name='__main__')
        command = run.call_args.args[0]
        self.assertIn(f'BL_SDK_BASE={sdk}', command)
        self.assertIn(f'BUILD_DIR={sdk_out.parent}', command)
        self.assertIn('flash', command)
        self.assertIn('COMX=/dev/ttyUSB9', command)


if __name__ == '__main__':
    unittest.main()
