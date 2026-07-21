/*
 *
 *    Copyright (c) 2025 Project CHIP Authors
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

#include "ZigbeeStackMgr.h"

#if CONFIG_ZIGBEE_APP

#include <FreeRTOS.h>
#include <task.h>

#include <atomic>

#include <app/server/Server.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/DiagnosticDataProvider.h>
#include <platform/PlatformManager.h>
#include <platform/bouffalolab/common/DiagnosticDataProviderImpl.h>

// bouffalo_zigbee (BLZ) C API. The stack ships as prebuilt static libraries with
// C headers; macphy154 adapts them to the BL SDK's lmac154 802.15.4 MAC and
// enables coexistence (CFG_COEX_ENABLE) internally during zb_stackInit().
extern "C" {
#include "zb_common.h" // zbRet_t, ZB_SUCC, roles, zb_stackInit/registerCb/registerDevice/initDevices/setRole/start/isJoined
#include "zb_nwk.h"    // zb_nlmeLeave
#include "zcl_common.h"// ZCL_CLUST_ONOFF, ZCL_STATUS_SUCCESS
#include "zcl_onOff.h" // zcl_onOffRegisterServer, ZCL_EVT_ONOFF_UPDATE_ONOFF
#include "zcl_basic.h"

// Default app-framework event handler (zbapp_stackHandler.c): routes
// ZB_EVT_STARTUP_COMPLETE to the join/retry logic. Declared weak upstream.
zbRet_t zbapp_stackEventHandler(uint8_t evtId, uint8_t * evtParam);

// zbapp join-retry control (zbapp_stackHandler.c). bZigbeeAutoRejoin gates both the
// retry timer callback (zbapp_startupTimerHandler) and the failure handler
// (zbapp_handleStartupFailure); clearing it stops the "Do zb_start" retry loop.
extern bool bZigbeeAutoRejoin;
void zbapp_deleteStartupTimer(void);

// Stack main pump (defined inside the prebuilt zbstack library). The standalone
// zigbee examples call it from the FreeRTOS tick hook; here a dedicated task
// drives it so the Matter platform's tick hook is untouched.
void ZB_MONITOR(void);

// BL SDK WiFi/Zigbee coexistence control (wifi_mgmr_ext.h has no extern "C"
// guards; declared directly here to avoid pulling the heavy WiFi header in).
int wifi_mgmr_sta_coex_enable(void);
int wifi_mgmr_sta_coex_disable(void);
int wifi_mgmr_sta_coex_duty_set(uint8_t active_ms);
}

namespace chip {
namespace DeviceLayer {

namespace {

constexpr uint16_t kZbHaProfileId   = 0x0104; // Home Automation
constexpr uint16_t kZbOnOffLightDev = 0x0100; // On/Off Light device id
constexpr uint8_t kZbEndpoint       = 1;
constexpr uint32_t kZbTaskStackSize = 4096;
constexpr UBaseType_t kZbTaskPrio   = 4;

// ---- Matter<->Zigbee commissioning coordination ----------------------------------
// The device commits to exactly one commissioning path. Whichever side commissions
// first owns the device and closes the other side's commissioning/joining. These
// flags are the cross-task signal between the Matter event loop and the zigbee task.
std::atomic<bool> g_matterCommissioned{ false };
std::atomic<bool> g_zigbeeJoined{ false };

bool IsMatterCommissioned()
{
    return chip::Server::GetInstance().GetFabricTable().FabricCount() > 0;
}

// Close the Matter commissioning window so the device stops advertising
// commissionable. Must run on the Matter event loop (thread safety).
void CloseMatterCommissioningOnMatterTask()
{
    auto & cwm = chip::Server::GetInstance().GetCommissioningWindowManager();
    if (cwm.IsCommissioningWindowOpen())
    {
        cwm.CloseCommissioningWindow();
        ChipLogProgress(NotSpecified, "Coord: closed Matter commissioning window (Zigbee owns device)");
    }
}

// Schedule the Matter window close from any task (e.g. the zigbee task).
void CloseMatterCommissioningAsync()
{
    chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) { CloseMatterCommissioningOnMatterTask(); });
}

// Leave the current zigbee network (runs on the zigbee task, so zb_nlmeLeave is safe).
void LeaveZigbeeNetwork()
{
    uint8_t zeroIeee[8] = { 0 };
    zb_nlmeLeave(zeroIeee, 0, 0);
    g_zigbeeJoined.store(false);
}

// ---- WiFi/Zigbee coexistence -----------------------------------------------------
// Coex (WiFi duty-cycling to time-share the 2.4GHz radio with 15.4) is only needed
// while BOTH stacks are active -- i.e. the dual commissioning window, before either
// side owns the device. Once Matter is commissioned (zigbee goes idle) or zigbee has
// joined (only one radio user left), coex is turned off to let the active stack use
// the radio fully.
constexpr uint8_t kWifiCoexActiveMs = 30; // ~30ms WiFi per TBTT, rest to 15.4

bool WifiCoexNeeded()
{
    // Coex is only useful while neither side has won the commissioning race yet.
    return !g_matterCommissioned.load() && !g_zigbeeJoined.load();
}

void DisableWifiCoex()
{
    int ret = wifi_mgmr_sta_coex_disable();
    ChipLogProgress(NotSpecified, "Coord: WiFi/Zigbee coex disabled (single radio user, ret=%d)", ret);
}

// Log the remaining SRAM heap at key milestones (same GetCurrentHeapFree path as the
// AppTask "SRAM heap ... left" print).
void LogFreeHeap(const char * context)
{
    uint64_t freeHeap = 0;
    chip::DeviceLayer::DiagnosticDataProviderImpl::GetDefaultInstance().GetCurrentHeapFree(freeHeap);
    ChipLogProgress(NotSpecified, "Heap[%s]: SRAM free=%llu bytes", context, freeHeap);
}

// Zigbee stack event handler (runs in the zigbee task context). On a successful
// join it either claims the device (closing Matter commissioning) or, if Matter
// already commissioned, leaves again. Then forwards to the default app-framework
// handler for logging/retry.
zbRet_t ZigbeeStackEventHandler(uint8_t evtId, uint8_t * evtParam)
{
    if (evtId == ZB_EVT_STARTUP_COMPLETE && evtParam != nullptr)
    {
        uint8_t status   = evtParam[0];
        uint8_t roleType = evtParam[1];
        if (status == 0x00 && roleType != ZB_ROLE_COORDINATOR)
        {
            g_zigbeeJoined.store(true);
            // The commissioning race is over (zigbee owns the device) -> only one
            // radio user left, WiFi/Zigbee coex no longer needed.
            DisableWifiCoex();
            LogFreeHeap("zigbee joined");
            if (g_matterCommissioned.load())
            {
                ChipLogProgress(NotSpecified, "Coord: Zigbee joined but Matter already commissioned -> leaving zigbee network");
                LeaveZigbeeNetwork();
            }
            else
            {
                ChipLogProgress(NotSpecified, "Coord: Zigbee joined (role=%d) -> claiming device, closing Matter commissioning",
                                roleType);
                CloseMatterCommissioningAsync();
            }
        }
    }
    return zbapp_stackEventHandler(evtId, evtParam);
}

// Dedicated task that pumps the zigbee stack, mirroring the vApplicationTickHook
// -> ZB_MONITOR() pattern of the standalone zigbee examples without stealing the
// Matter platform's tick hook.
void ZigbeeStackTask(void * /*arg*/)
{
    ChipLogProgress(NotSpecified, "Zigbee stack task started");
    for (;;)
    {
        ZB_MONITOR();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

} // namespace

// Called from the Matter event loop when commissioning completes: the device is now
// a Matter device, so zigbee joining must stop. Clears bZigbeeAutoRejoin (which gates
// the zbapp retry timer + failure handler) and cancels any pending retry timer.
void OnMatterCommissioned()
{
    g_matterCommissioned.store(true);
    bZigbeeAutoRejoin    = false;
    zbapp_deleteStartupTimer();
    // Matter owns the device now -> zigbee idle, only one radio user -> coex off.
    DisableWifiCoex();
    ChipLogProgress(NotSpecified, "Coord: Matter commissioned -> Zigbee join stopped (autoRejoin off)");
    LogFreeHeap("matter commissioned");
}

// External (called from platform.cpp on WiFi IP-up). Enables WiFi/Zigbee coex only
// while neither side owns the device yet (the dual commissioning window).
void EnableWifiCoexIfNeeded()
{
    if (!WifiCoexNeeded())
    {
        ChipLogProgress(NotSpecified, "Coord: WiFi/Zigbee coex not needed at IP-up (device already owned)");
        return;
    }
    int retEn   = wifi_mgmr_sta_coex_enable();
    int retDuty = wifi_mgmr_sta_coex_duty_set(kWifiCoexActiveMs);
    ChipLogProgress(NotSpecified, "Coord: WiFi/Zigbee coex enabled (dual commissioning, duty %dms, enable=%d duty_set=%d)",
                    kWifiCoexActiveMs, retEn, retDuty);
}

CHIP_ERROR InitZigbeePlatform()
{
    ChipLogProgress(NotSpecified, "Initializing Zigbee stack (Zigbee Router on lmac154)");

    zbRet_t status = zb_stackInit();
    if (status != ZB_SUCC)
    {
        ChipLogError(NotSpecified, "zb_stackInit failed: 0x%08x", static_cast<unsigned>(status));
        return CHIP_ERROR_INTERNAL;
    }
    ChipLogProgress(NotSpecified, "zb_stackInit ok");

    // Register our coordination-aware event handler (it forwards to zbapp_stackEventHandler
    // for the default logging/retry behavior).
    zb_registerCb(ZigbeeStackEventHandler);

    // Minimal HA On/Off Light endpoint so the device is a functional Zigbee Router.
    struct _deviceFlags deviceFlags = { 0 };
    status = zb_registerDevice(kZbEndpoint, kZbHaProfileId, kZbOnOffLightDev, deviceFlags);
    if (status != ZB_SUCC)
    {
        ChipLogError(NotSpecified, "zb_registerDevice failed: 0x%08x", static_cast<unsigned>(status));
        return CHIP_ERROR_INTERNAL;
    }

    zcl_basicRegisterServer(kZbEndpoint);
    zcl_onOffRegisterServer(kZbEndpoint);

    status = zb_initDevices();
    if (status != ZB_SUCC)
    {
        ChipLogError(NotSpecified, "zb_initDevices failed: 0x%08x", static_cast<unsigned>(status));
        return CHIP_ERROR_INTERNAL;
    }

    // ---- Boot-time Matter<->Zigbee mutual exclusion ----
    bool matterCommissioned = IsMatterCommissioned();
    bool zigbeeJoined       = zb_isJoined();
    g_matterCommissioned.store(matterCommissioned);
    g_zigbeeJoined.store(zigbeeJoined);
    ChipLogProgress(NotSpecified, "Coord boot: Matter commissioned=%d, Zigbee joined=%d", matterCommissioned, zigbeeJoined);

    if (matterCommissioned)
    {
        // Device is already a Matter device -> zigbee must not try to join.
        bZigbeeAutoRejoin = false;
        ChipLogProgress(NotSpecified, "Coord: Matter commissioned -> Zigbee will not join (no zb_start)");
    }
    else
    {
        zb_setRole(ZB_ROLE_ROUTER);
        if (zigbeeJoined)
        {
            // Device is already a Zigbee device -> Matter must not be commissionable.
            ChipLogProgress(NotSpecified, "Coord: Zigbee joined -> suppressing Matter commissionable");
            CloseMatterCommissioningAsync();
        }
        else
        {
            // Neither side owns the device yet -> let zigbee try to join as a Router;
            // Matter stays commissionable. First commissioning to complete wins.
            ChipLogProgress(NotSpecified, "Coord: neither commissioned -> Zigbee joining as Router (first commissioning wins)");
        }
        status = zb_start();
        ChipLogProgress(NotSpecified, "zb_start (%s)", status == ZB_SUCC ? "ok" : "failed");
    }

    BaseType_t created = xTaskCreate(ZigbeeStackTask, "zb", kZbTaskStackSize / sizeof(StackType_t), nullptr, kZbTaskPrio,
                                     nullptr);
    VerifyOrReturnError(created == pdPASS, CHIP_ERROR_INTERNAL);

    LogFreeHeap("startup complete");
    return CHIP_NO_ERROR;
}

} // namespace DeviceLayer
} // namespace chip

// Drives the shared lighting LED (defined in AppTask.cpp). Matter and zigbee are
// mutually exclusive, so zigbee OnOff just sets the LED directly -- no cross-protocol
// state sync, no Matter data-model write.
extern void AppSetLightOnOff(bool onOff);

// Override the zigbee component's weak zcl_dispatchClusterEvent (zbapp_clusterCallback.c)
// to receive ZCL cluster events. Matter and zigbee never run at the same time (one
// disables the other), so cluster state is handled locally without mirroring between
// the two data models. OnOff drives the shared LED directly.
extern "C" uint8_t zcl_dispatchClusterEvent(uint8_t ep, uint16_t clustId, uint8_t evtId, void * evtParam)
{
    if (clustId == ZCL_CLUST_ONOFF && evtId == ZCL_EVT_ONOFF_UPDATE_ONOFF && evtParam != nullptr)
    {
        uint8_t value = *static_cast<uint8_t *>(evtParam);
        ChipLogProgress(NotSpecified, "Zigbee OnOff (ep=%d) -> %d", ep, value);
        AppSetLightOnOff(value != 0);
    }
    else
    {
        ChipLogProgress(NotSpecified, "Zigbee ZCL ep=%d cluster=0x%04x evt=%d", ep, clustId, evtId);
    }
    return ZCL_STATUS_SUCCESS;
}

#endif // CONFIG_ZIGBEE_APP
