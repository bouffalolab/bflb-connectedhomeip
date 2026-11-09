/*
 * Copyright (c) 2026 Project CHIP Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#include "ControllerTask.h"

#include <FreeRTOS.h>
#include <task.h>

#include <lib/support/logging/CHIPLogging.h>
#include <platform/bouffalolab/common/BflbConfig.h>

extern "C" void platform_port_init(void);

extern "C" int START_ENTRY(void)
{
    platform_port_init();
    chip::DeviceLayer::Internal::BflbConfig::Init();

    ChipLogProgress(NotSpecified, "bouffalolab Matter IP controller, built at " __DATE__ " " __TIME__);
    ControllerTask::Start();

    vTaskStartScheduler();
    ChipLogError(NotSpecified, "FreeRTOS scheduler failed to start");
    for (;;)
    {
    }
}
