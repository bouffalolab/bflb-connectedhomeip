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

"""Exercise the real Bouffalo logger's recursive mutex lifetime on the host."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

CHIP_ROOT = Path(__file__).resolve().parents[5]


@unittest.skipUnless(shutil.which('c++'), 'Host C++ compiler is required')
class TestLoggingMutex(unittest.TestCase):
    def test_boot_and_multiple_tasks_balance_lock_operations(self) -> None:
        with tempfile.TemporaryDirectory(prefix='bflb-logging-') as directory:
            source = Path(directory)
            headers = {
                'platform/CHIPDeviceConfig.h': '',
                'platform/logging/LogV.h': '''\
#include <cstdarg>
#include <cstdint>
namespace chip { namespace Logging { namespace Platform {
void LogV(const char *, uint8_t, const char *, va_list);
}}}
''',
                'CHIPDevicePlatformConfig.h': '',
                'lib/core/CHIPConfig.h': '#define CHIP_CONFIG_LOG_MESSAGE_MAX_SIZE 128\n',
                'lib/support/CodeUtils.h': '''\
#define VerifyOrReturn(x) do { if (!(x)) { return; } } while (0)
#define VerifyOrDo(x, action) do { if (!(x)) { action; } } while (0)
''',
                'lib/support/logging/Constants.h': '''\
namespace chip { namespace Logging {
enum { kLogCategory_Error, kLogCategory_Progress, kLogCategory_Detail };
}}
''',
                'FreeRTOS.h': '''\
#pragma once
#define pdTRUE 1
#define portMAX_DELAY 0xffffffffU
struct StaticSemaphore_t { int owner; int depth; };
using SemaphoreHandle_t = StaticSemaphore_t *;
''',
                'semphr.h': '''\
#include <FreeRTOS.h>
SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic(StaticSemaphore_t *);
int xSemaphoreTakeRecursive(SemaphoreHandle_t, unsigned);
int xSemaphoreGiveRecursive(SemaphoreHandle_t);
''',
                'task.h': '''\
#define taskSCHEDULER_RUNNING 2
int xTaskGetSchedulerState();
unsigned long xTaskGetTickCount();
int xPortIsInsideInterrupt();
''',
            }
            for name, contents in headers.items():
                path = source / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(contents)
            (source / 'main.cpp').write_text('''\
#include <cassert>
#include <platform/logging/LogV.h>
#include <semphr.h>
static SemaphoreHandle_t mutex;
static int scheduler, task = 1, takes, gives, blocked, in_isr;
SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic(StaticSemaphore_t * storage) {
    mutex = storage;
    return storage;
}
int xSemaphoreTakeRecursive(SemaphoreHandle_t m, unsigned) {
    ++takes;
    if (m->depth && m->owner != task) { ++blocked; return 0; }
    m->owner = task;
    ++m->depth;
    return 1;
}
int xSemaphoreGiveRecursive(SemaphoreHandle_t m) {
    ++gives;
    assert(m->owner == task && m->depth > 0);
    if (--m->depth == 0) { m->owner = 0; }
    return 1;
}
int xTaskGetSchedulerState() { return scheduler; }
unsigned long xTaskGetTickCount() { return 0; }
int xPortIsInsideInterrupt() { return in_isr; }
static void log(const char * format, ...) {
    va_list args;
    va_start(args, format);
    chip::Logging::Platform::LogV("test", 1, format, args);
    va_end(args);
}
int main() {
    log("before scheduler");
    assert(takes == 0 && gives == 0);
    scheduler = 2;
    log("APP task");
    assert(takes == 1 && gives == 1 && mutex->depth == 0);
    task = 2;
    log("CHIP task");
    assert(blocked == 0 && takes == 2 && gives == 2 && mutex->depth == 0);
    xSemaphoreTakeRecursive(mutex, 0);
    log("recursive owner");
    assert(mutex->depth == 1);
    xSemaphoreGiveRecursive(mutex);
    in_isr = 1;
    log("ISR must not take the log lock");
    assert(takes == 4 && gives == 4 && mutex->depth == 0);
}
''')
            logger = CHIP_ROOT / 'src/platform/bouffalolab/common/Logging.cpp'
            binary = source / 'test'
            result = subprocess.run(['c++', '-std=c++17', '-I', str(source), str(logger), str(source / 'main.cpp'),
                                     '-o', str(binary)], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            self.assertEqual(result.returncode, 0, result.stdout)
            result = subprocess.run([str(binary)], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            self.assertEqual(result.returncode, 0, result.stdout)


if __name__ == '__main__':
    unittest.main()
