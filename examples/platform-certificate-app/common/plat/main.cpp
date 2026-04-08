/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
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

#include <platform/CHIPDeviceLayer.h>
#include <app/server/Server.h>
#include <app/DeviceProxy.h>
#include <app/InteractionModelEngine.h>
#include <app/util/attribute-storage.h>
#include <app/util/config.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <lib/core/CHIPError.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/ErrorStr.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/PlatformManager.h>

#include <app/CommandHandler.h>
#include <app/CommandSender.h>
#include <app/EventLogging.h>
#include <app/InteractionModelEngine.h>
#include <app/clusters/identify-server/identify-server.h>
#include <app/server/Server.h>
#include <app/server/CommissioningWindowManager.h>
#include <app/server/CommissioningWindowManager.h>

#include <app/util/af.h>

using namespace chip;
using namespace chip::DeviceLayer;
using namespace chip::app;

extern "C" void main(void)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    // Initialize the CHIP stack
    err = PlatformMgr().InitChipStack();
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "PlatformMgr().InitChipStack() failed: %s", ErrorStr(err));
        return;
    }

    // Initialize the Matter server
    err = chip::app::Server::GetInstance().Init();
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "Server::GetInstance().Init() failed: %s", ErrorStr(err));
        return;
    }

    // Start the CHIP stack
    err = PlatformMgr().StartEventLoopTask();
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "PlatformMgr().StartEventLoopTask() failed: %s", ErrorStr(err));
        return;
    }

    // This is a minimal example, so we just run the event loop forever
    while (true)
    {
        PlatformMgr().RunEventLoop();
    }
}