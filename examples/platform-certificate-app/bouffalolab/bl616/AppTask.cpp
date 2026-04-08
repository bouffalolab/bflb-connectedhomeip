/*
 *
 *    Copyright (c) 2025 Project CHIP Authors
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

#include "AppTask.h"

#include <platform/CHIPDeviceLayer.h>
#include <platform/PlatformManager.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/logging/CHIPLogging.h>

#if CHIP_DEVICE_LAYER_TARGET_BL616
#ifdef BOOT_PIN_RESET
#include <bflb_gpio.h>
#endif
#else
extern "C" {
#include <bl_gpio.h>
#include <hal_gpio.h>
#include <hosal_gpio.h>
}
#endif
#include <plat.h>

using namespace ::chip;
using namespace ::chip::DeviceLayer;

AppTask AppTask::sAppTask;

void StartAppTask(void)
{
    BaseType_t ret = xTaskCreate(
        AppTask::AppTaskMain,
        APP_TASK_NAME,
        APP_TASK_STACK_SIZE / sizeof(StackType_t),
        NULL,
        APP_TASK_PRIORITY,
        &AppTask::sAppTask.sAppTaskHandle
    );

    if (ret != pdPASS)
    {
        ChipLogError(NotSpecified, "Failed to create app task");
    }
}

void AppTask::AppTaskMain(void * pvParameter)
{
    ChipLogProgress(NotSpecified, "Platform certificate App Task Main started");

    ButtonInit();

    CHIP_ERROR ret = PlatformMgr().StartEventLoopTask();
    if (ret != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "PlatformMgr().StartEventLoopTask() failed");
        appError(ret);
    }
    vTaskSuspend(NULL);

    while (true)
    {
        app_event_t     appEvent        = APP_EVENT_NONE;
        BaseType_t      eventReceived   = xTaskNotifyWait(0, APP_EVENT_FACTORY_RESET, (uint32_t *) &appEvent, portMAX_DELAY);

        if (eventReceived)
        {
            PlatformMgr().LockChipStack();
            if (APP_EVENT_FACTORY_RESET & appEvent)
            {
                DeviceLayer::ConfigurationMgr().InitiateFactoryReset();
            }
            PlatformMgr().UnlockChipStack();
        }
    }
}

void AppTask::PostEvent(app_event_t event)
{
    if (xPortIsInsideInterrupt())
    {
        BaseType_t higherPrioTaskWoken = pdFALSE;
        xTaskNotifyFromISR(sAppTaskHandle, event, eSetBits, &higherPrioTaskWoken);
    }
    else
    {
        xTaskNotify(sAppTaskHandle, event, eSetBits);
    }
}

#if CHIP_DEVICE_LAYER_TARGET_BL616
static struct bflb_device_s * app_task_gpio_var = NULL;
static void app_task_gpio_isr(int irq, void * arg)
{
    bool intstatus = bflb_gpio_get_intstatus(app_task_gpio_var, BOOT_PIN_RESET);
    if (intstatus)
    {
        bflb_gpio_int_clear(app_task_gpio_var, BOOT_PIN_RESET);
    }

    GetAppTask().ButtonEventHandler(arg);
}
#else
static hosal_gpio_dev_t gpio_key = { .port = BOOT_PIN_RESET, .config = INPUT_HIGH_IMPEDANCE, .priv = NULL };
#endif

void AppTask::ButtonInit(void)
{
    GetAppTask().mButtonPressedTime = 0;

#if CHIP_DEVICE_LAYER_TARGET_BL616
    app_task_gpio_var = bflb_device_get_by_name("gpio");

    bflb_gpio_init(app_task_gpio_var, BOOT_PIN_RESET, GPIO_INPUT);
    bflb_gpio_int_init(app_task_gpio_var, BOOT_PIN_RESET, GPIO_INT_TRIG_MODE_SYNC_FALLING_RISING_EDGE);
    bflb_gpio_int_mask(app_task_gpio_var, BOOT_PIN_RESET, false);

    bflb_irq_attach(app_task_gpio_var->irq_num, app_task_gpio_isr, app_task_gpio_var);
    bflb_irq_enable(app_task_gpio_var->irq_num);
#else
    hosal_gpio_init(&gpio_key);
    hosal_gpio_irq_set(&gpio_key, HOSAL_IRQ_TRIG_POS_PULSE, GetAppTask().ButtonEventHandler, NULL);
#endif
}

bool AppTask::ButtonPressed(void)
{
#if CHIP_DEVICE_LAYER_TARGET_BL616
    return bflb_gpio_read(app_task_gpio_var, BOOT_PIN_RESET);
#else
    uint8_t val = 1;

    hosal_gpio_input_get(&gpio_key, &val);

    return val == 1;
#endif
}

void AppTask::ButtonEventHandler(void * arg)
{
    if (ButtonPressed())
    {
        GetAppTask().mButtonPressedTime = System::SystemClock().GetMonotonicMilliseconds64().count();
    }
    else {
        if (APP_BUTTON_PRESS_LONG < System::SystemClock().GetMonotonicMilliseconds64().count() - GetAppTask().mButtonPressedTime) {
            GetAppTask().PostEvent(APP_EVENT_FACTORY_RESET);
        }
        GetAppTask().mButtonPressedTime = 0;
    }
}
