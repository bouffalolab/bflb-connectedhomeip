/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include <FreeRTOS.h>
#include <task.h>

extern "C" {
#include <bflb_mtd.h>
#include <bl_sys.h>
#include <board.h>
}
#include <plat.h>

extern "C" void __libc_init_array(void);
extern "C" void shell_init_with_task(struct bflb_device_s * shell);
extern "C" void app_pre_matter_init(void) __attribute__((weak));
extern "C" void app_pre_matter_init(void) {}

void platform_port_init(void)
{
    bl_sys_rstinfo_init();

#if !defined(CONFIG_SYSINIT)
    // SYSINIT owns board services when enabled; board_init() is only the
    // compatibility entry point for legacy SDK startup.
    board_init();
#endif
#if CONFIG_ENABLE_CHIP_SHELL
    struct bflb_device_s * uart0 = bflb_device_get_by_name("uart0");
    shell_init_with_task(uart0);
#endif
#if !defined(CONFIG_SYSINIT)
    // The partition table and MTD are initialized by SYSINIT on current SDKs.
    bflb_mtd_init();
#endif

    app_pre_matter_init();

    // Run global constructors once, after the SDK allocator and console exist.
    // The SDK's pre-main constructor wrapper is disabled for these examples.
    __libc_init_array();
}

extern "C" void vAssertCalled(void)
{
    void * ra = (void *) __builtin_return_address(0);

    taskDISABLE_INTERRUPTS();
    if (xPortIsInsideInterrupt())
    {
        printf("vAssertCalled, ra = %p in ISR\r\n", (void *) ra);
    }
    else
    {
        TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
        const char * taskName    = currentTask != NULL ? pcTaskGetName(currentTask) : "<no current task>";
        printf("vAssertCalled, ra = %p in task %s\r\n", (void *) ra, taskName);
    }

    abort();

    while (true)
        ;
}

extern "C" void bflb_assert(void) __attribute__((weak, alias("vAssertCalled")));
