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

"""Check EAPOL hook preservation when the shared route header shadows app hooks."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

CHIP_ROOT = Path(__file__).resolve().parents[5]


@unittest.skipUnless(shutil.which('cc'), 'A host C compiler is required')
class TestWifiLwipHooks(unittest.TestCase):
    def test_shared_header_preserves_sdk_hook_only_for_wifi(self) -> None:
        with tempfile.TemporaryDirectory(prefix='bflb-lwip-hooks-') as directory:
            root = Path(directory)
            (root / 'lwip').mkdir()
            for name in ('arch.h', 'err.h', 'ip_addr.h', 'ip6_addr.h'):
                (root / 'lwip' / name).write_text('')
            # Model the SDK adapter contract without requiring an SDK checkout.
            (root / 'lwiphooks.h').write_text('''\
#define LWIP_HOOK_UNKNOWN_ETH_PROTOCOL net_eth_receive
int net_eth_receive(void *, void *);
''')
            source = root / 'hooks.c'
            for wifi in (False, True):
                with self.subTest(wifi=wifi):
                    source.write_text('''\
typedef struct { unsigned int addr[4]; } ip6_addr_t;
#include "lwipopts_user.h"
#include LWIP_HOOK_FILENAME
#ifndef LWIP_HOOK_IP6_ROUTE
#error IPv6 routing hook lost
#endif
#ifndef LWIP_HOOK_ND6_GET_GW
#error IPv6 gateway hook lost
#endif
''' + ('''
#ifndef LWIP_HOOK_UNKNOWN_ETH_PROTOCOL
#error EAPOL forwarding hook lost
#endif
int forward(void *packet, void *netif) {
    return LWIP_HOOK_UNKNOWN_ETH_PROTOCOL(packet, netif);
}
''' if wifi else '''
#ifdef LWIP_HOOK_UNKNOWN_ETH_PROTOCOL
#error Wi-Fi dependency leaked into non-Wi-Fi builds
#endif
'''))
                    command = ['cc', '-std=c99', '-Wall', '-Werror', '-fsyntax-only',
                               '-I', str(root),
                               '-I', str(CHIP_ROOT / 'examples/platform/bouffalolab/common/route_hook'),
                               '-I', str(CHIP_ROOT / 'examples/contact-sensor-app/bouffalolab/bflb'),
                               '-I', str(CHIP_ROOT / 'examples/contact-sensor-app/bouffalolab/bflb/wifi')]
                    if wifi:
                        command.append('-DCONFIG_MACSW_LWIP')
                    result = subprocess.run(command + [str(source)], text=True, capture_output=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
