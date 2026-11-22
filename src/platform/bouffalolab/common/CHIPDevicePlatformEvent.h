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

#pragma once

#include <platform/CHIPDeviceEvent.h>
#include "CHIPDevicePlatformConfig.h"
#include <system/SystemPacketBuffer.h>

namespace chip {
namespace DeviceLayer {

namespace DeviceEventType {

/**
 * Enumerates Bouffalo Lab platforms event types that are visible to the application.
 */
enum PublicPlatformSpecificEventTypes
{
    // Wi-Fi commissioning events: posted by wifi_event_handler
    // (NetworkCommissioningDriver.cpp) and consumed by NetworkEventHandler.
    kWiFiOnInitDone = kRange_PublicPlatformSpecific,
    kWiFiOnScanDone,
    kWiFiOnConnecting,
    kWiFiOnConnected,
    kGotIpAddress,
    kGotIpv6Address,
    kWiFiOnDisconnected,

    /**
     * Posted by the platform when it requires a restart (factory-reset wipe
     * complete, applied OTA image pending). The application layer receives
     * this event through its own handler and owns the reboot decision
     * itself (whether and when to restart).
     */
    kPlatformBflbRebootRequest,
};

/**
 * Enumerates Bouffalo Lab platforms event types that are internal to the chip Device Layer.
 */
enum InternalPlatformSpecificEventTypes
{
    kPlatformBflbEvent = kRange_InternalPlatformSpecific,
    kPlatformBflbBleConnected,
    kPlatformBflbBleDisconnected,
    kPlatformBflbBleCCCWrite,
    kPlatformBflbBleC1WriteEvent,
    kPlatformBflbBleC2IndDoneEvent,
    kPlatformBflbBleOutOfBuffersEvent,
#if CHIP_DEVICE_CONFIG_ENABLE_BOUFFALO_BLE_CENTRAL
    kPlatformBflbBleCentralScanMatch,
    kPlatformBflbBleCentralScanError,
    kPlatformBflbBleCentralConnected,
    kPlatformBflbBleCentralDisconnected,
    kPlatformBflbBleCentralGattReady,
    kPlatformBflbBleCentralIndication,
    kPlatformBflbBleCentralWriteComplete,
#endif
};

} // namespace DeviceEventType

#if CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE
struct BleConnEventType
{
    struct bt_conn * BtConn;
    uint8_t HciResult;
};

struct BleCCCWriteEventType
{
    struct bt_conn * BtConn;
    uint16_t Value;
};

struct BleC1WriteEventType
{
    struct bt_conn * BtConn;
    ::chip::System::PacketBuffer * Data;
};

struct BleC2IndDoneEventType
{
    struct bt_conn * BtConn;
    uint8_t Result;
};

#if CHIP_DEVICE_CONFIG_ENABLE_BOUFFALO_BLE_CENTRAL
struct BleCentralScanMatchEventType
{
    uint8_t Address[6];
    uint8_t AddressType;
};

struct BleCentralScanErrorEventType
{
    int32_t Status;
};

struct BleCentralConnectionEventType
{
    struct bt_conn * BtConn;
    uint8_t HciResult;
};

struct BleCentralGattReadyEventType
{
    uint8_t Status;
};

struct BleCentralDataEventType
{
    struct bt_conn * BtConn;
    ::chip::System::PacketBuffer * Data;
    uint8_t Status;
};
#endif

/**
 * Represents platform-specific event information for Bouffalo Lab platforms.
 */
struct ChipDevicePlatformEvent final
{
    union
    {
        BleConnEventType BleConnEvent;
        BleCCCWriteEventType BleCCCWriteEvent;
        BleC1WriteEventType BleC1WriteEvent;
        BleC2IndDoneEventType BleC2IndDoneEvent;
#if CHIP_DEVICE_CONFIG_ENABLE_BOUFFALO_BLE_CENTRAL
        BleCentralScanMatchEventType BleCentralScanMatchEvent;
        BleCentralScanErrorEventType BleCentralScanErrorEvent;
        BleCentralConnectionEventType BleCentralConnectionEvent;
        BleCentralGattReadyEventType BleCentralGattReadyEvent;
        BleCentralDataEventType BleCentralDataEvent;
#endif
    };
};
#else
struct ChipDevicePlatformEvent final
{
};
#endif

} // namespace DeviceLayer
} // namespace chip
