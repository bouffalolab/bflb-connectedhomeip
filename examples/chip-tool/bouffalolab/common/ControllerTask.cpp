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
#include "ControllerCommands.h"
#include "SntpTimeSync.h"

#include <FreeRTOS.h>
#include <task.h>

#include <lib/core/CHIPError.h>
#include <lib/shell/Engine.h>
#include <lib/shell/commands/WiFi.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/bouffalolab/common/NetworkCommissioningDriver.h>
#include <system/SystemClock.h>

void cmd_misc_init(void);

namespace {

constexpr uint32_t kControllerTaskStackSize = 8192;
constexpr UBaseType_t kControllerTaskPriority = tskIDLE_PRIORITY + 2;

void ControllerTaskMain(void *)
{
    auto & wifiDriver = chip::DeviceLayer::NetworkCommissioning::BflbWiFiDriver::GetInstance();
    chip::Shell::Engine::Root().Init();
    cmd_misc_init();
    chip::Shell::SetWiFiDriver(&wifiDriver);
    ControllerCommands::Init();

    CHIP_ERROR err = chip::DeviceLayer::PlatformMgr().InitChipStack();
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "Controller stack initialization failed: %s", chip::ErrorStr(err));
        vTaskDelete(nullptr);
        return;
    }

    err = chip::DeviceLayer::PlatformMgr().StartEventLoopTask();
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "Controller event loop start failed: %s", chip::ErrorStr(err));
        vTaskDelete(nullptr);
        return;
    }

    SntpTimeSync::Start();

    ChipLogProgress(NotSpecified, "Matter IP controller ready; use: matter chip-tool <command> ...");
    vTaskDelete(nullptr);
}

} // namespace

extern "C" bool SntpTimeSyncSetMatterTime(uint32_t unixSeconds, uint32_t microseconds)
{
    const uint64_t unixTimeMicroseconds = static_cast<uint64_t>(unixSeconds) * chip::kMicrosecondsPerSecond + microseconds;

    const CHIP_ERROR err =
        chip::System::SystemClock().SetClock_RealTime(chip::System::Clock::Microseconds64(unixTimeMicroseconds));
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "Failed to set Matter system time: %s", chip::ErrorStr(err));
        return false;
    }

    return true;
}

void ControllerTask::Start()
{
    BaseType_t result = xTaskCreate(ControllerTaskMain, "matter_ctrl", kControllerTaskStackSize / sizeof(StackType_t), nullptr,
                                    kControllerTaskPriority, nullptr);
    if (result != pdPASS)
    {
        ChipLogError(NotSpecified, "Failed to create Matter controller initialization task");
    }
}
