/*
 *    Copyright (c) 2022 Project CHIP Authors
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

#include <platform/internal/CHIPDeviceLayerInternal.h>

#include <platform/ThreadStackManager.h>

#include <platform/FreeRTOS/GenericThreadStackManagerImpl_FreeRTOS.hpp>
#include <platform/OpenThread/GenericThreadStackManagerImpl_OpenThread.hpp>

#include <mbedtls/platform.h>

#include <openthread_port.h>

namespace chip {
namespace DeviceLayer {

ThreadStackManagerImpl ThreadStackManagerImpl::sInstance;

CHIP_ERROR ThreadStackManagerImpl::_InitThreadStack(void)
{
    return InitThreadStack(NULL);
}

CHIP_ERROR ThreadStackManagerImpl::InitThreadStack(otInstance * otInst)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    // Initialize the generic implementation base classes.
    err = GenericThreadStackManagerImpl_FreeRTOS<ThreadStackManagerImpl>::DoInit();
    SuccessOrExit(err);
    err = GenericThreadStackManagerImpl_OpenThread<ThreadStackManagerImpl>::DoInit(otInst);
    SuccessOrExit(err);

    ot_radioSetInstance(OTInstance());

#if CHIP_DEVICE_LAYER_TARGET_BL702 || CHIP_DEVICE_LAYER_TARGET_BL702L
    mbedtls_platform_set_calloc_free(pvPortCalloc, vPortFree);
#endif

exit:
    return err;
}

bool ThreadStackManagerImpl::IsInitialized()
{
    return sInstance.mThreadStackLock != NULL;
}

void ThreadStackManagerImpl::FactoryResetThreadStack(void)
{
    VerifyOrReturn(ThreadStackMgrImpl().OTInstance() != NULL);
    otInstanceFactoryReset(ThreadStackMgrImpl().OTInstance());
}

} // namespace DeviceLayer
} // namespace chip

// The OpenThread port glue below implements CHIP's task ownership of the OT
// stack: CHIP drives the OT task (GenericThreadStackManagerImpl_FreeRTOS)
// and provides the lock/event plumbing the SDK openthread_port expects. The
// 15.4 radio and platform-driver bring-up live in the SDK SYSINIT flow
// (components/wireless/thread/openthread_port, ot_port_sysinit).

using namespace ::chip::DeviceLayer;

extern "C" ot_system_event_t otrGetNotifyEvent(void);

ot_system_event_t ot_system_event_var = OT_SYSTEM_EVENT_NONE;

void otSysProcessDrivers(otInstance * aInstance)
{
    ot_system_event_t sevent = otrGetNotifyEvent();

    if (sevent)
    {
        ot_alarmTask(sevent);
        ot_radioTask(sevent);
    }
}

extern "C" void otTaskletsSignalPending(otInstance * p_instance)
{
    otSysEventSignalPending();
}

extern "C" void otSysEventSignalPending(void)
{
    if (xPortIsInsideInterrupt())
    {
        BaseType_t yieldRequired = ThreadStackMgrImpl().SignalThreadActivityPendingFromISR();
        portYIELD_FROM_ISR(yieldRequired);
    }
    else
    {
        ThreadStackMgrImpl().SignalThreadActivityPending();
    }
}

extern "C" otInstance * otrGetInstance()
{
    return ThreadStackMgrImpl().OTInstance();
}

extern "C" void otrLock(void)
{
    ThreadStackMgrImpl().LockThreadStack();
}

extern "C" void otrUnlock(void)
{
    ThreadStackMgrImpl().UnlockThreadStack();
}

extern "C" uint32_t otrEnterCrit(void)
{
    if (xPortIsInsideInterrupt())
    {
        return taskENTER_CRITICAL_FROM_ISR();
    }
    else
    {
        taskENTER_CRITICAL();
        return 0;
    }
}

extern "C" void otrExitCrit(uint32_t tag)
{
    if (xPortIsInsideInterrupt())
    {
        taskEXIT_CRITICAL_FROM_ISR(tag);
    }
    else
    {
        taskEXIT_CRITICAL();
    }
}

extern "C" ot_system_event_t otrGetNotifyEvent(void)
{
    ot_system_event_t sevent = OT_SYSTEM_EVENT_NONE;

    taskENTER_CRITICAL();
    sevent              = ot_system_event_var;
    ot_system_event_var = OT_SYSTEM_EVENT_NONE;
    taskEXIT_CRITICAL();

    return sevent;
}

extern "C" void otrNotifyEvent(ot_system_event_t sevent)
{
    uint32_t tag        = otrEnterCrit();
    ot_system_event_var = (ot_system_event_t) (ot_system_event_var | sevent);
    otrExitCrit(tag);

    otSysEventSignalPending();
}
