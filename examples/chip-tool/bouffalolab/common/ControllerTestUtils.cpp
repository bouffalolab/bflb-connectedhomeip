/*
 * Copyright (c) 2026 Project CHIP Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#include <lib/support/UnitTestUtils.h>

#include <FreeRTOS.h>
#include <task.h>

namespace chip {
namespace test_utils {

void SleepMillis(uint64_t millisecs)
{
    const TickType_t ticks = pdMS_TO_TICKS(millisecs);
    vTaskDelay(ticks == 0 ? 1 : ticks);
}

void SleepMicros(uint64_t microsecs)
{
    SleepMillis((microsecs + 500) / 1000);
}

} // namespace test_utils
} // namespace chip
