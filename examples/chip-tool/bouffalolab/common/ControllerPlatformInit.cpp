/*
 * Copyright (c) 2026 Project CHIP Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#include <platform/CHIPDeviceLayer.h>

namespace chip {
namespace DeviceLayer {

CHIP_ERROR PlatformManagerImpl::PlatformInit(void)
{
    // The controller initializes CHIP before starting the event loop. Unlike
    // the lighting application, it has no application-specific startup hook.
    return CHIP_NO_ERROR;
}

} // namespace DeviceLayer
} // namespace chip
