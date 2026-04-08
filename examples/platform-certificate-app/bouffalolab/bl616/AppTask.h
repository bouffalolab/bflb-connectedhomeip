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

#pragma once

#include <platform/CHIPDeviceLayer.h>
#include <platform/PlatformManager.h>
#include "FreeRTOS.h"
#include "task.h"

using namespace ::chip;
using namespace ::chip::DeviceLayer;

#define APP_BUTTON_PRESS_JITTER 100
#define APP_BUTTON_PRESS_SHORT 1000
#define APP_BUTTON_PRESS_LONG 3000

class AppTask
{
public:
    static void AppTaskMain(void * pvParameter);
    static void AppEventLoop();

    enum app_event_t
    {
        APP_EVENT_NONE                      = 0,
        APP_EVENT_FACTORY_RESET             = 0x00000001,
        APP_EVENT_COMMISSION_COMPLETE       = 0x00000002,
    };

    static void ButtonInit(void);
    static bool ButtonPressed(void);
    static void ButtonEventHandler(void * arg);

    uint64_t mButtonPressedTime;

    TaskHandle_t sAppTaskHandle = nullptr;
    static AppTask sAppTask;

    void PostEvent(app_event_t event);
};

inline AppTask & GetAppTask(void)
{
    return AppTask::sAppTask;
}

void StartAppTask(void);
