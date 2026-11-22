/*
 *
 *    Copyright (c) 2020-2022 Project CHIP Authors
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

#if CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE

#include <ble/Ble.h>
#include <ble/CHIPBleServiceData.h>
#include <lib/support/CHIPMemString.h>
#include <lib/support/SafeInt.h>
#include <platform/DeviceInstanceInfoProvider.h>
#include <platform/internal/BLEManager.h>
#if CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
#include <setup_payload/AdditionalDataPayloadGenerator.h>
#endif

extern "C" {
#if CHIP_DEVICE_LAYER_TARGET_BL702L || CHIP_DEVICE_LAYER_TARGET_BFLB
#include <btble_lib_api.h>
#else
#include <ble_lib_api.h>
#endif
}

#include <addr.h>
#include <bluetooth.h>
#include <hci_driver.h>

#include "BLEManagerImpl.h"

using namespace ::chip;
using namespace ::chip::Ble;
using namespace ::chip::System;

namespace chip {
namespace DeviceLayer {
namespace Internal {

#if CHIP_DEVICE_CONFIG_ENABLE_BOUFFALO_BLE_CENTRAL
namespace {

constexpr System::Clock::Timeout kCentralScanTimeout = System::Clock::Seconds16(20);

bt_uuid_16 sMatterBleServiceUuid = BT_UUID_INIT_16(0xFFF6);
bt_uuid_16 sMatterBleCccUuid = BT_UUID_INIT_16(0x2902);
bt_uuid_128 sMatterBleRxUuid =
    BT_UUID_INIT_128(0x11, 0x9D, 0x9F, 0x42, 0x9C, 0x4F, 0x9F, 0x95, 0x59, 0x45, 0x3D, 0x26, 0xF5, 0x2E, 0xEE, 0x18);
bt_uuid_128 sMatterBleTxUuid =
    BT_UUID_INIT_128(0x12, 0x9D, 0x9F, 0x42, 0x9C, 0x4F, 0x9F, 0x95, 0x59, 0x45, 0x3D, 0x26, 0xF5, 0x2E, 0xEE, 0x18);

struct MatterAdvertisementMatch
{
    const SetupDiscriminator * discriminator;
    bool matched = false;
    bool hasMatterServiceData = false;
    uint16_t discriminatorValue = 0;
};

bool MatchMatterServiceData(bt_data * data, void * context)
{
    auto * match = static_cast<MatterAdvertisementMatch *>(context);
    if (data->type != BT_DATA_SVC_DATA16 || data->data_len < 2 || data->data[0] != 0xF6 || data->data[1] != 0xFF)
    {
        return true;
    }

    match->hasMatterServiceData = true;
    if (data->data_len < (2 + sizeof(ChipBLEDeviceIdentificationInfo)))
    {
        return true;
    }

    ChipBLEDeviceIdentificationInfo info;
    memcpy(&info, data->data + 2, sizeof(info));
    match->discriminatorValue = info.GetDeviceDiscriminator();
    match->matched            = match->discriminator->MatchesLongDiscriminator(match->discriminatorValue);
    return !match->matched;
}

} // namespace
#endif

const bt_uuid_128 UUID128_CHIPoBLEChar_RX =
    BT_UUID_INIT_128(0x11, 0x9D, 0x9F, 0x42, 0x9C, 0x4F, 0x9F, 0x95, 0x59, 0x45, 0x3D, 0x26, 0xF5, 0x2E, 0xEE, 0x18);
const bt_uuid_128 UUID128_CHIPoBLEChar_TX =
    BT_UUID_INIT_128(0x12, 0x9D, 0x9F, 0x42, 0x9C, 0x4F, 0x9F, 0x95, 0x59, 0x45, 0x3D, 0x26, 0xF5, 0x2E, 0xEE, 0x18);
bt_uuid_16 UUID16_CHIPoBLEService = BT_UUID_INIT_16(0xFFF6);

#if CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
const bt_uuid_128 UUID128_CHIPoBLEChar_C3 =
    BT_UUID_INIT_128(0x04, 0x8F, 0x21, 0x83, 0x8A, 0x74, 0x7D, 0xB8, 0xF2, 0x45, 0x72, 0x87, 0x38, 0x02, 0x63, 0x64);
#endif

_bt_gatt_ccc CHIPoBLEChar_TX_CCC = BT_GATT_CCC_INITIALIZER(nullptr, BLEManagerImpl::HandleTXCCCWrite, nullptr);

struct bt_gatt_attr sChipoBleAttributes[] = {
    BT_GATT_PRIMARY_SERVICE(&UUID16_CHIPoBLEService.uuid),
    BT_GATT_CHARACTERISTIC(&UUID128_CHIPoBLEChar_RX.uuid, BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, nullptr, BLEManagerImpl::HandleRXWrite, nullptr),
    BT_GATT_CHARACTERISTIC(&UUID128_CHIPoBLEChar_TX.uuid, BT_GATT_CHRC_INDICATE, BT_GATT_PERM_NONE, nullptr, nullptr, nullptr),
    BT_GATT_CCC_MANAGED(&CHIPoBLEChar_TX_CCC, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
#if CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
    BT_GATT_CHARACTERISTIC(&UUID128_CHIPoBLEChar_C3.uuid, BT_GATT_CHRC_READ, BT_GATT_PERM_READ, BLEManagerImpl::HandleC3Read,
                           nullptr, nullptr),
#endif
};

struct bt_gatt_service sChipoBleService = {
    .attrs      = sChipoBleAttributes,
    .attr_count = sizeof(sChipoBleAttributes) / sizeof(sChipoBleAttributes[0]),
};

static const int kCHIPoBLE_CCC_AttributeIndex = 3;

CHIP_ERROR InitRandomStaticAddress()
{
    // When the BT privacy feature is disabled, generate a random static address once per boot.
    // This must be done before bt_enable() has been called.
    bt_addr_le_t addr;

    int error = bt_addr_le_create_static(&addr);
    if (error)
    {
        ChipLogError(DeviceLayer, "Failed to create BLE address: %d", error);
        return CHIP_ERROR_POSIX(-error);
    }

    error = bt_id_create(&addr, nullptr);

    if (error < 0)
    {
        ChipLogError(DeviceLayer, "Failed to create BLE identity: %d", error);
        return CHIP_ERROR_POSIX(-error);
    }

    ChipLogProgress(DeviceLayer, "BLE address: %02X:%02X:%02X:%02X:%02X:%02X", addr.a.val[5], addr.a.val[4], addr.a.val[3],
                    addr.a.val[2], addr.a.val[1], addr.a.val[0]);
    return CHIP_NO_ERROR;
}

BLEManagerImpl BLEManagerImpl::sInstance;

CHIP_ERROR BLEManagerImpl::_Init()
{
    mServiceMode = ConnectivityManager::kCHIPoBLEServiceMode_Enabled;
    mFlags.ClearAll().Set(Flags::kAdvertisingEnabled, CHIP_DEVICE_CONFIG_CHIPOBLE_ENABLE_ADVERTISING_AUTOSTART);
    mFlags.Set(Flags::kFastAdvertisingEnabled, true);
    mGAPConns = 0;

    memset(mSubscribedConns, 0, sizeof(mSubscribedConns));

    // The BLE controller/host stack is brought up by the SDK example
    // backbone (btble_controller_init + hci_driver_init + bt_enable, as in
    // the SDK BLE examples). Only the Matter identity and CHIPoBLE setup
    // remain here.

    ReturnErrorOnFailure(InitRandomStaticAddress());

    memset(&mConnCallbacks, 0, sizeof(mConnCallbacks));
    mConnCallbacks.connected    = HandleConnect;
    mConnCallbacks.disconnected = HandleDisconnect;

    bt_conn_cb_register(&mConnCallbacks);

    // Initialize the CHIP BleLayer.
#if CHIP_DEVICE_CONFIG_ENABLE_BOUFFALO_BLE_CENTRAL
    ReturnErrorOnFailure(BleLayer::Init(this, this, this, &DeviceLayer::SystemLayer()));
#else
    ReturnErrorOnFailure(BleLayer::Init(this, this, &DeviceLayer::SystemLayer()));
#endif

    TEMPORARY_RETURN_IGNORED PlatformMgr().ScheduleWork(DriveBLEState, 0);

    return CHIP_NO_ERROR;
}

#if CHIP_DEVICE_CONFIG_ENABLE_BOUFFALO_BLE_CENTRAL
void BLEManagerImpl::NewConnection(BleLayer * bleLayer, void * appState, const SetupDiscriminator & connDiscriminator)
{
    if (bleLayer != this || mCentralState != CentralState::kIdle)
    {
        BleConnectionDelegate::OnConnectionError(appState, CHIP_ERROR_INCORRECT_STATE);
        return;
    }

    mCentralDiscriminator = connDiscriminator;
    mCentralAppState      = appState;
    mCentralDiagnosticsOnly = false;
    CHIP_ERROR err        = PlatformMgr().ScheduleWork(StartCentralScanWork, 0);
    if (err != CHIP_NO_ERROR)
    {
        HandleCentralConnectionError(err);
    }
}

void BLEManagerImpl::NewConnection(BleLayer * bleLayer, void * appState, BLE_CONNECTION_OBJECT connObj)
{
    if (bleLayer != this || connObj == BLE_CONNECTION_UNINITIALIZED)
    {
        BleConnectionDelegate::OnConnectionError(appState, CHIP_ERROR_INCORRECT_STATE);
        return;
    }

    BleConnectionDelegate::OnConnectionComplete(appState, connObj);
}

CHIP_ERROR BLEManagerImpl::CancelConnection()
{
    DeviceLayer::SystemLayer().CancelTimer(HandleCentralScanTimeout, this);

    if (mCentralState == CentralState::kDisconnecting)
    {
        mCentralAppState = nullptr;
        return CHIP_NO_ERROR;
    }

    if (mCentralState == CentralState::kScanning || mCentralState == CentralState::kScanMatchPending)
    {
        const int status = bt_le_scan_stop();
        if (status != 0)
        {
            ChipLogError(Ble, "Failed to stop BLE central scan: %d", status);
        }
    }

    if (mCentralConnection != nullptr && mCentralState != CentralState::kIdle)
    {
        mCentralState = CentralState::kDisconnecting;
        const int status = bt_conn_disconnect(mCentralConnection, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        if (status != 0)
        {
            ChipLogError(Ble, "Failed to cancel BLE central connection: %d", status);
            mCentralAppState = nullptr;
            ResetCentralConnection();
            return CHIP_NO_ERROR;
        }
        mCentralAppState = nullptr;
        return CHIP_NO_ERROR;
    }

    mCentralAppState = nullptr;
    ResetCentralConnection();
    return CHIP_NO_ERROR;
}

CHIP_ERROR BLEManagerImpl::StartCentralScanForTest()
{
    VerifyOrReturnError(mCentralState == CentralState::kIdle && !mCentralScanStartPending, CHIP_ERROR_BUSY);

    mCentralDiscriminator   = SetupDiscriminator();
    mCentralAppState        = nullptr;
    mCentralDiagnosticsOnly = true;
    mCentralScanStartPending = true;
    CHIP_ERROR err = PlatformMgr().ScheduleWork(StartCentralScanWork, 0);
    if (err != CHIP_NO_ERROR)
    {
        mCentralScanStartPending = false;
        mCentralDiagnosticsOnly  = false;
    }
    return err;
}

CHIP_ERROR BLEManagerImpl::StopCentralScanForTest()
{
    VerifyOrReturnError(mCentralDiagnosticsOnly || mCentralScanStartPending, CHIP_ERROR_INCORRECT_STATE);
    return PlatformMgr().ScheduleWork(StopCentralScanWork, 0);
}

CHIP_ERROR BLEManagerImpl::StartCentralScan()
{
    VerifyOrReturnError(mCentralState == CentralState::kIdle, CHIP_ERROR_INCORRECT_STATE);

    bt_le_scan_param parameters{};
    parameters.type       = mCentralDiagnosticsOnly ? BT_LE_SCAN_TYPE_PASSIVE : BT_LE_SCAN_TYPE_ACTIVE;
    parameters.filter_dup = mCentralDiagnosticsOnly ? 0 : BT_LE_SCAN_FILTER_DUPLICATE;
    parameters.interval  = BT_GAP_SCAN_FAST_INTERVAL;
    parameters.window    = BT_GAP_SCAN_FAST_WINDOW;
    mCentralScanReportCount = 0;

    mCentralState = CentralState::kScanning;
    const int status = bt_le_scan_start(&parameters, HandleCentralScanResult);
    if (status != 0)
    {
        mCentralState = CentralState::kIdle;
        return CHIP_ERROR_INTERNAL;
    }

    CHIP_ERROR timerError = DeviceLayer::SystemLayer().StartTimer(kCentralScanTimeout, HandleCentralScanTimeout, this);
    if (timerError != CHIP_NO_ERROR)
    {
        bt_le_scan_stop();
        mCentralState = CentralState::kIdle;
        return timerError;
    }

    ChipLogProgress(Ble, "Started BLE central scan (type %u, filter %u, interval %u, window %u)%s",
                    parameters.type, parameters.filter_dup, parameters.interval, parameters.window,
                    mCentralDiagnosticsOnly ? " [diagnostic]" : "");
    return CHIP_NO_ERROR;
}

CHIP_ERROR BLEManagerImpl::StartCentralConnection()
{
    VerifyOrReturnError(mCentralState == CentralState::kConnecting, CHIP_ERROR_INCORRECT_STATE);

    const bt_le_conn_param parameters = { BT_GAP_INIT_CONN_INT_MIN, BT_GAP_INIT_CONN_INT_MAX, 0, 400 };
    mCentralConnection = bt_conn_create_le(&mCentralPeerAddress, &parameters);
    VerifyOrReturnError(mCentralConnection != nullptr, CHIP_ERROR_CONNECTION_ABORTED);

    ChipLogProgress(Ble, "Connecting to Matter BLE commissionee");
    return CHIP_NO_ERROR;
}

CHIP_ERROR BLEManagerImpl::StartCentralServiceDiscovery()
{
    VerifyOrReturnError(mCentralConnection != nullptr, CHIP_ERROR_INCORRECT_STATE);

    mCentralServiceStartHandle = 0;
    mCentralServiceEndHandle   = 0;
    mCentralRxValueHandle      = 0;
    mCentralTxValueHandle      = 0;
    mCentralTxCccHandle        = 0;
    mCentralState              = CentralState::kDiscoveringService;
    mCentralDiscoverParams     = {};
    mCentralDiscoverParams.uuid        = &sMatterBleServiceUuid.uuid;
    mCentralDiscoverParams.func        = HandleCentralServiceDiscovered;
    mCentralDiscoverParams.start_handle = 1;
    mCentralDiscoverParams.end_handle   = 0xFFFF;
    mCentralDiscoverParams.type         = BT_GATT_DISCOVER_PRIMARY;

    const int status = bt_gatt_discover(mCentralConnection, &mCentralDiscoverParams);
    if (status != 0)
    {
        ChipLogError(Ble, "Failed to start Matter GATT service discovery: %d", status);
    }
    return status == 0 ? CHIP_NO_ERROR : CHIP_ERROR_INTERNAL;
}

CHIP_ERROR BLEManagerImpl::StartCentralCharacteristicDiscovery()
{
    mCentralState          = CentralState::kDiscoveringCharacteristics;
    mCentralDiscoverParams = {};
    mCentralDiscoverParams.func         = HandleCentralCharacteristicDiscovered;
    mCentralDiscoverParams.start_handle = mCentralServiceStartHandle;
    mCentralDiscoverParams.end_handle   = mCentralServiceEndHandle;
    mCentralDiscoverParams.type         = BT_GATT_DISCOVER_CHARACTERISTIC;

    const int status = bt_gatt_discover(mCentralConnection, &mCentralDiscoverParams);
    return status == 0 ? CHIP_NO_ERROR : CHIP_ERROR_INTERNAL;
}

CHIP_ERROR BLEManagerImpl::StartCentralCccDiscovery()
{
    mCentralState          = CentralState::kDiscoveringCcc;
    mCentralDiscoverParams = {};
    mCentralDiscoverParams.uuid         = &sMatterBleCccUuid.uuid;
    mCentralDiscoverParams.func         = HandleCentralCccDiscovered;
    mCentralDiscoverParams.start_handle = static_cast<uint16_t>(mCentralTxValueHandle + 1);
    mCentralDiscoverParams.end_handle   = mCentralServiceEndHandle;
    mCentralDiscoverParams.type         = BT_GATT_DISCOVER_DESCRIPTOR;

    const int status = bt_gatt_discover(mCentralConnection, &mCentralDiscoverParams);
    return status == 0 ? CHIP_NO_ERROR : CHIP_ERROR_INTERNAL;
}

void BLEManagerImpl::HandleCentralGattDiscoveryComplete(uint8_t status)
{
    ChipDeviceEvent event{};
    event.Type                                    = DeviceEventType::kPlatformBflbBleCentralGattReady;
    event.Platform.BleCentralGattReadyEvent.Status = status;
    PlatformMgr().PostEventOrDie(&event);
}

void BLEManagerImpl::HandleCentralGattNotification(const void * data, uint16_t length)
{
    if (mCentralConnection == nullptr)
    {
        return;
    }

    System::PacketBufferHandle buffer;
    if (data != nullptr)
    {
        buffer = System::PacketBufferHandle::NewWithData(data, length);
        if (buffer.IsNull())
        {
            ChipLogError(Ble, "Failed to allocate BLE central indication buffer");
            return;
        }
    }

    ChipDeviceEvent event{};
    event.Type                                       = DeviceEventType::kPlatformBflbBleCentralIndication;
    event.Platform.BleCentralDataEvent.BtConn        = bt_conn_ref(mCentralConnection);
    event.Platform.BleCentralDataEvent.Data          = std::move(buffer).UnsafeRelease();
    event.Platform.BleCentralDataEvent.Status        = 0;
    PlatformMgr().PostEventOrDie(&event);
}

void BLEManagerImpl::HandleCentralWriteComplete(uint8_t status)
{
    if (mCentralConnection == nullptr)
    {
        mCentralWriteBuffer = nullptr;
        return;
    }

    ChipDeviceEvent event{};
    event.Type                                       = DeviceEventType::kPlatformBflbBleCentralWriteComplete;
    event.Platform.BleCentralDataEvent.BtConn        = bt_conn_ref(mCentralConnection);
    event.Platform.BleCentralDataEvent.Data          = nullptr;
    event.Platform.BleCentralDataEvent.Status        = status;
    PlatformMgr().PostEventOrDie(&event);
}

void BLEManagerImpl::HandleCentralConnectionError(CHIP_ERROR error)
{
    DeviceLayer::SystemLayer().CancelTimer(HandleCentralScanTimeout, this);
    if (mCentralState == CentralState::kScanning || mCentralState == CentralState::kScanMatchPending)
    {
        const int status = bt_le_scan_stop();
        if (status != 0)
        {
            ChipLogError(Ble, "Failed to stop BLE scan while aborting central attempt: %d", status);
        }
    }

    void * appState = mCentralAppState;
    mCentralAppState = nullptr;
    if (mCentralConnection != nullptr)
    {
        mCentralState = CentralState::kDisconnecting;
        const int status = bt_conn_disconnect(mCentralConnection, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        if (status != 0)
        {
            ChipLogError(Ble, "Failed to disconnect BLE central connection after error: %d", status);
            ResetCentralConnection();
        }
    }
    else
    {
        ResetCentralConnection();
    }
    if (appState != nullptr)
    {
        BleConnectionDelegate::OnConnectionError(appState, error);
    }
}

void BLEManagerImpl::HandleCentralScanTimeout(System::Layer *, void * context)
{
    auto * self = static_cast<BLEManagerImpl *>(context);
    if (self->mCentralState == CentralState::kScanning)
    {
        if (self->mCentralDiagnosticsOnly)
        {
            ChipLogError(Ble, "Diagnostic BLE scan timed out; received %u advertising reports",
                         static_cast<unsigned int>(self->mCentralScanReportCount));
        }
        else
        {
            ChipLogError(Ble, "BLE central scan timed out without finding discriminator %u",
                         self->mCentralDiscriminator.GetLongValue());
        }
        self->HandleCentralConnectionError(CHIP_ERROR_TIMEOUT);
    }
}

void BLEManagerImpl::ResetCentralConnection()
{
    mCentralState              = CentralState::kIdle;
    mCentralAppState           = nullptr;
    mCentralSubscribePending   = false;
    mCentralDiagnosticsOnly    = false;
    mCentralScanStartPending   = false;
    mCentralWriteBuffer = nullptr;
    if (mCentralConnection != nullptr)
    {
        bt_conn_unref(mCentralConnection);
        mCentralConnection = nullptr;
    }
}

void BLEManagerImpl::StartCentralScanWork(intptr_t)
{
    auto & manager = BLEMgrImpl();
    manager.mCentralScanStartPending = false;
    CHIP_ERROR err = manager.StartCentralScan();
    if (err != CHIP_NO_ERROR)
    {
        manager.HandleCentralConnectionError(err);
    }
}

void BLEManagerImpl::StopCentralScanWork(intptr_t)
{
    auto & manager = BLEMgrImpl();
    if (manager.mCentralDiagnosticsOnly)
    {
        CHIP_ERROR err = manager.CancelConnection();
        if (err != CHIP_NO_ERROR)
        {
            ChipLogError(Ble, "Failed to stop diagnostic BLE scan: %" CHIP_ERROR_FORMAT, err.Format());
        }
        else
        {
            ChipLogProgress(Ble, "Diagnostic BLE scan stopped");
        }
    }
}

void BLEManagerImpl::StartCentralConnectionWork(intptr_t)
{
    CHIP_ERROR err = BLEMgrImpl().StartCentralConnection();
    if (err != CHIP_NO_ERROR)
    {
        BLEMgrImpl().HandleCentralConnectionError(err);
    }
}

void BLEManagerImpl::ContinueCentralGattDiscoveryWork(intptr_t)
{
    auto & self = BLEMgrImpl();
    CHIP_ERROR err;
    if (self.mCentralState == CentralState::kDiscoveringService)
    {
        err = self.StartCentralCharacteristicDiscovery();
    }
    else if (self.mCentralState == CentralState::kDiscoveringCharacteristics)
    {
        err = self.StartCentralCccDiscovery();
    }
    else
    {
        return;
    }

    if (err != CHIP_NO_ERROR)
    {
        self.HandleCentralGattDiscoveryComplete(1);
    }
}

void BLEManagerImpl::HandleCentralSubscribeWork(intptr_t)
{
    auto & self = BLEMgrImpl();
    if (self.mCentralConnection != nullptr && self.mCentralSubscribePending)
    {
        self.mCentralSubscribePending = false;
        self.HandleSubscribeComplete(self.mCentralConnection, &CHIP_BLE_SVC_ID, &CHIP_BLE_CHAR_2_UUID);
    }
}

void BLEManagerImpl::HandleCentralScanResult(const bt_addr_le_t * address, s8_t rssi, u8_t advertisingType,
                                             net_buf_simple * advertisingData)
{
    auto & self = BLEMgrImpl();
    if (self.mCentralState != CentralState::kScanning || address == nullptr || advertisingData == nullptr)
    {
        return;
    }
    ++self.mCentralScanReportCount;

    MatterAdvertisementMatch match{ .discriminator = &self.mCentralDiscriminator };
    bt_data_parse(advertisingData, MatchMatterServiceData, &match);
    char addressString[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(address, addressString, sizeof(addressString));
    if (match.hasMatterServiceData)
    {
        ChipLogProgress(Ble, "BLE scan: %s RSSI %d type %u data %u bytes Matter discriminator %u%s", addressString,
                        rssi, advertisingType, advertisingData->len, match.discriminatorValue,
                        match.matched ? " (match)" : "");
    }
    else
    {
        ChipLogProgress(Ble, "BLE scan: %s RSSI %d type %u data %u bytes", addressString, rssi, advertisingType,
                        advertisingData->len);
    }

    if (!match.matched)
    {
        return;
    }

    if (self.mCentralDiagnosticsOnly)
    {
        return;
    }

    self.mCentralState = CentralState::kScanMatchPending;
    const int stopStatus = bt_le_scan_stop();
    if (stopStatus != 0)
    {
        ChipDeviceEvent event{};
        event.Type = DeviceEventType::kPlatformBflbBleCentralScanError;
        event.Platform.BleCentralScanErrorEvent.Status = stopStatus;
        PlatformMgr().PostEventOrDie(&event);
        return;
    }

    ChipDeviceEvent event{};
    event.Type                                        = DeviceEventType::kPlatformBflbBleCentralScanMatch;
    memcpy(event.Platform.BleCentralScanMatchEvent.Address, address->a.val, sizeof(address->a.val));
    event.Platform.BleCentralScanMatchEvent.AddressType = address->type;
    PlatformMgr().PostEventOrDie(&event);
}

uint8_t BLEManagerImpl::HandleCentralServiceDiscovered(bt_conn *, const bt_gatt_attr * attribute,
                                                        bt_gatt_discover_params *)
{
    auto & self = BLEMgrImpl();
    if (attribute == nullptr)
    {
        self.HandleCentralGattDiscoveryComplete(1);
        return BT_GATT_ITER_STOP;
    }

    const auto * service = static_cast<const bt_gatt_service_val *>(attribute->user_data);
    if (service == nullptr || bt_uuid_cmp(service->uuid, &sMatterBleServiceUuid.uuid) != 0)
    {
        return BT_GATT_ITER_CONTINUE;
    }

    self.mCentralServiceStartHandle = attribute->handle;
    self.mCentralServiceEndHandle   = service->end_handle;
    const CHIP_ERROR err = PlatformMgr().ScheduleWork(ContinueCentralGattDiscoveryWork, 0);
    if (err != CHIP_NO_ERROR)
    {
        self.HandleCentralGattDiscoveryComplete(1);
    }
    return BT_GATT_ITER_STOP;
}

uint8_t BLEManagerImpl::HandleCentralCharacteristicDiscovered(bt_conn *, const bt_gatt_attr * attribute,
                                                              bt_gatt_discover_params *)
{
    auto & self = BLEMgrImpl();
    if (attribute != nullptr)
    {
        const auto * characteristic = static_cast<const bt_gatt_chrc *>(attribute->user_data);
        if (characteristic != nullptr)
        {
            if (bt_uuid_cmp(characteristic->uuid, &sMatterBleRxUuid.uuid) == 0)
            {
                self.mCentralRxValueHandle = characteristic->value_handle;
            }
            else if (bt_uuid_cmp(characteristic->uuid, &sMatterBleTxUuid.uuid) == 0)
            {
                self.mCentralTxValueHandle = characteristic->value_handle;
            }
        }
        return BT_GATT_ITER_CONTINUE;
    }

    if (self.mCentralRxValueHandle == 0 || self.mCentralTxValueHandle == 0)
    {
        self.HandleCentralGattDiscoveryComplete(1);
        return BT_GATT_ITER_STOP;
    }

    self.mCentralState = CentralState::kDiscoveringCharacteristics;
    const CHIP_ERROR err = PlatformMgr().ScheduleWork(ContinueCentralGattDiscoveryWork, 0);
    if (err != CHIP_NO_ERROR)
    {
        self.HandleCentralGattDiscoveryComplete(1);
    }
    return BT_GATT_ITER_STOP;
}

uint8_t BLEManagerImpl::HandleCentralCccDiscovered(bt_conn *, const bt_gatt_attr * attribute,
                                                   bt_gatt_discover_params *)
{
    auto & self = BLEMgrImpl();
    if (attribute != nullptr)
    {
        self.mCentralTxCccHandle = attribute->handle;
        return BT_GATT_ITER_CONTINUE;
    }

    if (self.mCentralTxCccHandle == 0)
    {
        self.HandleCentralGattDiscoveryComplete(1);
    }
    else
    {
        self.HandleCentralGattDiscoveryComplete(0);
    }
    return BT_GATT_ITER_STOP;
}

uint8_t BLEManagerImpl::HandleCentralNotification(bt_conn *, bt_gatt_subscribe_params * parameters, const void * data,
                                                    uint16_t length)
{
    if (data == nullptr && parameters != nullptr && parameters->value != 0)
    {
        // Bouffalo's BFLB_BLE_PATCH_NOTIFY_WRITE_CCC_RSP reports a successful CCC write
        // using the same NULL-data callback used by the stack for unsubscribe completion.
        return BT_GATT_ITER_CONTINUE;
    }

    BLEMgrImpl().HandleCentralGattNotification(data, length);
    return BT_GATT_ITER_CONTINUE;
}

void BLEManagerImpl::HandleCentralWriteResponse(bt_conn *, uint8_t status, bt_gatt_write_params *)
{
    BLEMgrImpl().HandleCentralWriteComplete(status);
}
#endif // CHIP_DEVICE_CONFIG_ENABLE_BOUFFALO_BLE_CENTRAL

void BLEManagerImpl::_Shutdown()
{
    // Release BLE Stack resources
    mFlags.Set(Flags::kChipoBleShutDown);
}

void BLEManagerImpl::DriveBLEState(intptr_t arg)
{
    BLEMgrImpl().DriveBLEState();
}

void BLEManagerImpl::DriveBLEState()
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    // Perform any initialization actions that must occur after the CHIP task is running.
    if (!mFlags.Has(Flags::kAsyncInitCompleted))
    {
        mFlags.Set(Flags::kAsyncInitCompleted);
    }

    // If the application has enabled CHIPoBLE and BLE advertising...
    if (mServiceMode == ConnectivityManager::kCHIPoBLEServiceMode_Enabled &&
        mFlags.Has(Flags::kAdvertisingEnabled)
#if CHIP_DEVICE_CONFIG_CHIPOBLE_SINGLE_CONNECTION
        // and no connections are active...
        && (NumConnections() == 0)
#endif
    )
    {
        // Start/re-start advertising if not already advertising, or if the
        // advertising state needs to be refreshed.
        if (!mFlags.Has(Flags::kAdvertising) || mFlags.Has(Flags::kAdvertisingRefreshNeeded))
        {
            mFlags.Clear(Flags::kAdvertisingRefreshNeeded);
            err = StartAdvertising();
            SuccessOrExit(err);
        }
    }
    else
    {
        if (mFlags.Has(Flags::kAdvertising))
        {
            err = StopAdvertising();
            SuccessOrExit(err);
        }

        // If no connections are active unregister also CHIPoBLE GATT service
        if (NumConnections() == 0 && mFlags.Has(Flags::kChipoBleGattServiceRegister))
        {
            // Unregister CHIPoBLE service to not allow discovering it when pairing is disabled.
            if (bt_gatt_service_unregister(&sChipoBleService) != 0)
            {
                ChipLogError(DeviceLayer, "Failed to unregister CHIPoBLE GATT service");
            }
            else
            {
                mFlags.Clear(Flags::kChipoBleGattServiceRegister);
            }
        }
    }

exit:
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "Disabling CHIPoBLE service due to error: %" CHIP_ERROR_FORMAT, err.Format());
        mServiceMode = ConnectivityManager::kCHIPoBLEServiceMode_Disabled;
    }
}

struct BLEManagerImpl::ServiceData
{
    uint8_t uuid[2];
    ChipBLEDeviceIdentificationInfo deviceIdInfo;
} __attribute__((packed));

CHIP_ERROR BLEManagerImpl::StartAdvertising(void)
{
    int err                       = 0;
    const bool isAdvertisingRerun = mFlags.Has(Flags::kAdvertising);

    // At first run always select fast advertising, on the next attempt slow down interval.
    const uint32_t intervalMin = mFlags.Has(Flags::kFastAdvertisingEnabled) ? CHIP_DEVICE_CONFIG_BLE_FAST_ADVERTISING_INTERVAL_MIN
                                                                            : CHIP_DEVICE_CONFIG_BLE_SLOW_ADVERTISING_INTERVAL_MIN;
    const uint32_t intervalMax = mFlags.Has(Flags::kFastAdvertisingEnabled) ? CHIP_DEVICE_CONFIG_BLE_FAST_ADVERTISING_INTERVAL_MAX
                                                                            : CHIP_DEVICE_CONFIG_BLE_SLOW_ADVERTISING_INTERVAL_MAX;

    bt_le_adv_param advParams;
    advParams.id           = BT_ID_DEFAULT;
    advParams.options      = BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_ONE_TIME;
    advParams.interval_min = intervalMin;
    advParams.interval_max = intervalMax;

    // Define advertising and, if BLE device name is set, scan response data
    ServiceData serviceData;
    const uint8_t advFlags          = BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR;
    const bt_data advertisingData[] = { BT_DATA(BT_DATA_FLAGS, &advFlags, sizeof(advFlags)),
                                        BT_DATA(BT_DATA_SVC_DATA16, &serviceData, sizeof(serviceData)) };

    const char * deviceName             = bt_get_name();
    const uint8_t deviceNameSize        = static_cast<uint8_t>(strlen(deviceName));
    const bt_data scanResponseData[]    = { BT_DATA(BT_DATA_NAME_COMPLETE, deviceName, deviceNameSize) };
    const bt_data * scanResponseDataPtr = deviceNameSize > 0 ? scanResponseData : nullptr;
    const size_t scanResponseDataLen    = deviceNameSize > 0 ? sizeof(scanResponseData) / sizeof(scanResponseData[0]) : 0u;

    // Register dynamically CHIPoBLE GATT service
    if (!mFlags.Has(Flags::kChipoBleGattServiceRegister))
    {
        err = bt_gatt_service_register(&sChipoBleService);

        if (err != 0)
            ChipLogError(DeviceLayer, "Failed to register CHIPoBLE GATT service");

        VerifyOrReturnError(err == 0, CHIP_ERROR_POSIX(-err));

        mFlags.Set(Flags::kChipoBleGattServiceRegister);
    }

    // Initialize service data
    static_assert(sizeof(serviceData) == 10, "Size of BLE advertisement data changed! Was that intentional?");
    chip::Encoding::LittleEndian::Put16(serviceData.uuid, UUID16_CHIPoBLEService.val);
    ReturnErrorOnFailure(ConfigurationMgr().GetBLEDeviceIdentificationInfo(serviceData.deviceIdInfo));

#if CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
    ReturnErrorOnFailure(PrepareC3CharData());
#endif

    if (!isAdvertisingRerun)
    {
#ifdef CONFIG_BT_PRIVACY
        static_assert(CHIP_DEVICE_CONFIG_DISCOVERY_TIMEOUT_SECS <= CONFIG_BT_RPA_TIMEOUT,
                      "BLE advertising timeout is too long relative to RPA timeout");
        // Generate new private BLE address
        bt_le_oob bleOobInfo;
        err = bt_le_oob_get_local(advParams.id, &bleOobInfo);
        VerifyOrReturnError(err == 0, CHIP_ERROR_POSIX(-err));
#endif // CONFIG_BT_PRIVACY
    }

    // Restart advertising
    err = bt_le_adv_stop();
    VerifyOrReturnError(err == 0, CHIP_ERROR_POSIX(-err));

    err = bt_le_adv_start(&advParams, advertisingData, sizeof(advertisingData) / sizeof(advertisingData[0]), scanResponseDataPtr,
                          scanResponseDataLen);
    VerifyOrReturnError(err == 0, CHIP_ERROR_POSIX(-err));

    // Transition to the Advertising state...
    if (!mFlags.Has(Flags::kAdvertising))
    {
        ChipLogProgress(DeviceLayer, "CHIPoBLE advertising started");

        mFlags.Set(Flags::kAdvertising);

        // Post a CHIPoBLEAdvertisingChange(Started) event.
        {
            ChipDeviceEvent advChange;
            advChange.Type                             = DeviceEventType::kCHIPoBLEAdvertisingChange;
            advChange.CHIPoBLEAdvertisingChange.Result = kActivity_Started;
            ReturnErrorOnFailure(PlatformMgr().PostEvent(&advChange));
        }

        if (mFlags.Has(Flags::kFastAdvertisingEnabled))
        {
            // Start timer to change advertising interval.
            TEMPORARY_RETURN_IGNORED DeviceLayer::SystemLayer().StartTimer(
                System::Clock::Milliseconds32(CHIP_DEVICE_CONFIG_BLE_ADVERTISING_INTERVAL_CHANGE_TIME),
                HandleBLEAdvertisementIntervalChange, this);
        }
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR BLEManagerImpl::StopAdvertising(void)
{
    int err = bt_le_adv_stop();
    VerifyOrReturnError(err == 0, CHIP_ERROR_POSIX(-err));

    // Transition to the not Advertising state...
    if (mFlags.Has(Flags::kAdvertising))
    {
        mFlags.Clear(Flags::kAdvertising);
        mFlags.Set(Flags::kFastAdvertisingEnabled, true);

        ChipLogProgress(DeviceLayer, "CHIPoBLE advertising stopped");

        // Post a CHIPoBLEAdvertisingChange(Stopped) event.
        {
            ChipDeviceEvent advChange;
            advChange.Type                             = DeviceEventType::kCHIPoBLEAdvertisingChange;
            advChange.CHIPoBLEAdvertisingChange.Result = kActivity_Stopped;
            ReturnErrorOnFailure(PlatformMgr().PostEvent(&advChange));
        }

        // Cancel timer event changing CHIPoBLE advertisement interval
        DeviceLayer::SystemLayer().CancelTimer(HandleBLEAdvertisementIntervalChange, this);
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR BLEManagerImpl::_SetAdvertisingEnabled(bool val)
{
    VerifyOrReturnError(mServiceMode != ConnectivityManager::kCHIPoBLEServiceMode_NotSupported,
                        CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE);

    if (mFlags.Has(Flags::kAdvertisingEnabled) != val)
    {
        ChipLogDetail(DeviceLayer, "CHIPoBLE advertising set to %s", val ? "on" : "off");

        mFlags.Set(Flags::kAdvertisingEnabled, val);
        TEMPORARY_RETURN_IGNORED PlatformMgr().ScheduleWork(DriveBLEState, 0);
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR BLEManagerImpl::_SetAdvertisingMode(BLEAdvertisingMode mode)
{
    switch (mode)
    {
    case BLEAdvertisingMode::kFastAdvertising:
        mFlags.Set(Flags::kFastAdvertisingEnabled, true);
        break;
    case BLEAdvertisingMode::kSlowAdvertising:
        mFlags.Set(Flags::kFastAdvertisingEnabled, false);
        break;
    default:
        return CHIP_ERROR_INVALID_ARGUMENT;
    }
    mFlags.Set(Flags::kAdvertisingRefreshNeeded);
    TEMPORARY_RETURN_IGNORED PlatformMgr().ScheduleWork(DriveBLEState, 0);
    return CHIP_NO_ERROR;
}

CHIP_ERROR BLEManagerImpl::_GetDeviceName(char * buf, size_t bufSize)
{
    Platform::CopyString(buf, bufSize, bt_get_name());

    return CHIP_NO_ERROR;
}

CHIP_ERROR BLEManagerImpl::_SetDeviceName(const char * deviceName)
{
    if (mServiceMode == ConnectivityManager::kCHIPoBLEServiceMode_NotSupported)
    {
        return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
    }

    ChipLogDetail(DeviceLayer, "Device name set to: %s", StringOrNullMarker(deviceName));
    return CHIP_ERROR_POSIX(-bt_set_name(deviceName));
}

CHIP_ERROR BLEManagerImpl::HandleGAPConnect(const ChipDeviceEvent * event)
{
    const BleConnEventType * connEvent = &event->Platform.BleConnEvent;

    if (connEvent->HciResult == BT_HCI_ERR_SUCCESS)
    {
        ChipLogProgress(DeviceLayer, "BLE connection established (ConnId: 0x%02x)", bt_conn_index(connEvent->BtConn));
        mGAPConns++;
    }
    else
    {
        ChipLogError(DeviceLayer, "BLE connection failed (reason: 0x%02x)", connEvent->HciResult);
    }

    ChipLogProgress(DeviceLayer, "Current number of connections: %u/%u", NumConnections(), CONFIG_BT_MAX_CONN);

    mFlags.Set(Flags::kAdvertisingRefreshNeeded);
    TEMPORARY_RETURN_IGNORED PlatformMgr().ScheduleWork(DriveBLEState, 0);

    bt_conn_unref(connEvent->BtConn);

    return CHIP_NO_ERROR;
}

CHIP_ERROR BLEManagerImpl::HandleGAPDisconnect(const ChipDeviceEvent * event)
{
    const BleConnEventType * connEvent = &event->Platform.BleConnEvent;

    ChipLogProgress(DeviceLayer, "BLE GAP connection terminated (reason 0x%02x)", connEvent->HciResult);

    mGAPConns--;

    // If indications were enabled for this connection, record that they are now disabled and
    // notify the BLE Layer of a disconnect.
    if (UnsetSubscribed(connEvent->BtConn))
    {
        CHIP_ERROR disconReason;
        switch (connEvent->HciResult)
        {
        case BT_HCI_ERR_REMOTE_USER_TERM_CONN:
            // Do not treat proper connection termination as an error and exit.
            VerifyOrExit(!ConfigurationMgr().IsFullyProvisioned(), BLEMgrImpl()._Shutdown());
            disconReason = BLE_ERROR_REMOTE_DEVICE_DISCONNECTED;
            break;
        case BT_HCI_ERR_LOCALHOST_TERM_CONN:
            disconReason = BLE_ERROR_APP_CLOSED_CONNECTION;
            break;
        default:
            disconReason = BLE_ERROR_CHIPOBLE_PROTOCOL_ABORT;
            break;
        }
        HandleConnectionError(connEvent->BtConn, disconReason);
    }

exit:
    // Unref bt_conn before scheduling DriveBLEState.
    bt_conn_unref(connEvent->BtConn);

    ChipLogProgress(DeviceLayer, "Current number of connections: %u/%u", NumConnections(), CONFIG_BT_MAX_CONN);

    ChipDeviceEvent disconnectEvent;
    disconnectEvent.Type = DeviceEventType::kCHIPoBLEConnectionClosed;
    ReturnErrorOnFailure(PlatformMgr().PostEvent(&disconnectEvent));
    if (mFlags.Has(Flags::kChipoBleShutDown))
    {
        // The BLE host/controller lifecycle is owned by the application
        // backbone; Matter only clears its own state here.
        mFlags.Clear(Flags::kChipoBleShutDown);
        return CHIP_NO_ERROR;
    }
    // Force a reconfiguration of advertising in case we switched to non-connectable mode when
    // the BLE connection was established.
    mFlags.Set(Flags::kAdvertisingRefreshNeeded);
    TEMPORARY_RETURN_IGNORED PlatformMgr().ScheduleWork(DriveBLEState, 0);

    return CHIP_NO_ERROR;
}

CHIP_ERROR BLEManagerImpl::HandleTXCharCCCDWrite(const ChipDeviceEvent * event)
{
    const BleCCCWriteEventType * writeEvent = &event->Platform.BleCCCWriteEvent;

    ChipLogDetail(DeviceLayer, "ConnId: 0x%02x, New CCCD value: 0x%04x", bt_conn_index(writeEvent->BtConn), writeEvent->Value);

    // If the client has requested to enable indications and if it is not yet subscribed
    if (writeEvent->Value == BT_GATT_CCC_INDICATE && SetSubscribed(writeEvent->BtConn))
    {
        // Alert the BLE layer that CHIPoBLE "subscribe" has been received and increment the bt_conn reference counter.
        HandleSubscribeReceived(writeEvent->BtConn, &CHIP_BLE_SVC_ID, &Ble::CHIP_BLE_CHAR_2_UUID);

        ChipLogProgress(DeviceLayer, "CHIPoBLE connection established (ConnId: 0x%02x, GATT MTU: %u)",
                        bt_conn_index(writeEvent->BtConn), GetMTU(writeEvent->BtConn));

        // Post a CHIPoBLEConnectionEstablished event to the DeviceLayer and the application.
        {
            ChipDeviceEvent conEstEvent;
            conEstEvent.Type = DeviceEventType::kCHIPoBLEConnectionEstablished;
            ReturnErrorOnFailure(PlatformMgr().PostEvent(&conEstEvent));
        }
    }
    else
    {
        if (UnsetSubscribed(writeEvent->BtConn))
        {
            HandleUnsubscribeReceived(writeEvent->BtConn, &CHIP_BLE_SVC_ID, &Ble::CHIP_BLE_CHAR_2_UUID);
        }
    }

    bt_conn_unref(writeEvent->BtConn);

    return CHIP_NO_ERROR;
}

CHIP_ERROR BLEManagerImpl::HandleRXCharWrite(const ChipDeviceEvent * event)
{
    const BleC1WriteEventType * c1WriteEvent = &event->Platform.BleC1WriteEvent;

    ChipLogDetail(DeviceLayer, "Write request received for CHIPoBLE RX characteristic (ConnId 0x%02x)",
                  bt_conn_index(c1WriteEvent->BtConn));

    HandleWriteReceived(c1WriteEvent->BtConn, &CHIP_BLE_SVC_ID, &Ble::CHIP_BLE_CHAR_1_UUID,
                        PacketBufferHandle::Adopt(c1WriteEvent->Data));
    bt_conn_unref(c1WriteEvent->BtConn);

    return CHIP_NO_ERROR;
}

CHIP_ERROR BLEManagerImpl::HandleTXCharComplete(const ChipDeviceEvent * event)
{
    const BleC2IndDoneEventType * c2IndDoneEvent = &event->Platform.BleC2IndDoneEvent;

    ChipLogDetail(DeviceLayer, "Indication for CHIPoBLE TX characteristic done (ConnId 0x%02x, result 0x%02x)",
                  bt_conn_index(c2IndDoneEvent->BtConn), c2IndDoneEvent->Result);

    // Signal the BLE Layer that the outstanding indication is complete.
    HandleIndicationConfirmation(c2IndDoneEvent->BtConn, &CHIP_BLE_SVC_ID, &Ble::CHIP_BLE_CHAR_2_UUID);
    bt_conn_unref(c2IndDoneEvent->BtConn);

    return CHIP_NO_ERROR;
}

#if CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
CHIP_ERROR BLEManagerImpl::PrepareC3CharData()
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    BitFlags<AdditionalDataFields> additionalDataFields;
    AdditionalDataPayloadGeneratorParams additionalDataPayloadParams;

#if CHIP_ENABLE_ROTATING_DEVICE_ID && defined(CHIP_DEVICE_CONFIG_ROTATING_DEVICE_ID_UNIQUE_ID)
    uint8_t rotatingDeviceIdUniqueId[ConfigurationManager::kRotatingDeviceIDUniqueIDLength] = {};
    MutableByteSpan rotatingDeviceIdUniqueIdSpan(rotatingDeviceIdUniqueId);

    err = DeviceLayer::GetDeviceInstanceInfoProvider()->GetRotatingDeviceIdUniqueId(rotatingDeviceIdUniqueIdSpan);
    SuccessOrExit(err);
    err = ConfigurationMgr().GetLifetimeCounter(additionalDataPayloadParams.rotatingDeviceIdLifetimeCounter);
    SuccessOrExit(err);
    additionalDataPayloadParams.rotatingDeviceIdUniqueId = rotatingDeviceIdUniqueIdSpan;
    additionalDataFields.Set(AdditionalDataFields::RotatingDeviceId);
#endif /* CHIP_ENABLE_ROTATING_DEVICE_ID && defined(CHIP_DEVICE_CONFIG_ROTATING_DEVICE_ID_UNIQUE_ID) */

    err = AdditionalDataPayloadGenerator().generateAdditionalDataPayload(additionalDataPayloadParams, c3CharDataBufferHandle,
                                                                         additionalDataFields);

exit:
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "Failed to generate TLV encoded Additional Data (%s)", __func__);
    }

    return err;
}
#endif

void BLEManagerImpl::HandleBLEAdvertisementIntervalChange(System::Layer * layer, void * param)
{
    TEMPORARY_RETURN_IGNORED BLEMgr().SetAdvertisingMode(BLEAdvertisingMode::kSlowAdvertising);
    ChipLogProgress(DeviceLayer, "CHIPoBLE advertising mode changed to slow");
}

void BLEManagerImpl::_OnPlatformEvent(const ChipDeviceEvent * event)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    switch (event->Type)
    {
    case DeviceEventType::kPlatformBflbBleConnected:
        err = HandleGAPConnect(event);
        break;

    case DeviceEventType::kPlatformBflbBleDisconnected:
        err = HandleGAPDisconnect(event);
        break;

    case DeviceEventType::kPlatformBflbBleCCCWrite:
        err = HandleTXCharCCCDWrite(event);
        break;

    case DeviceEventType::kPlatformBflbBleC1WriteEvent:
        err = HandleRXCharWrite(event);
        break;

    case DeviceEventType::kPlatformBflbBleC2IndDoneEvent:
        err = HandleTXCharComplete(event);
        break;

#if CHIP_DEVICE_CONFIG_ENABLE_BOUFFALO_BLE_CENTRAL
    case DeviceEventType::kPlatformBflbBleCentralScanMatch:
        if (mCentralState != CentralState::kScanMatchPending)
        {
            break;
        }
        memcpy(mCentralPeerAddress.a.val, event->Platform.BleCentralScanMatchEvent.Address,
               sizeof(mCentralPeerAddress.a.val));
        mCentralPeerAddress.type = event->Platform.BleCentralScanMatchEvent.AddressType;
        DeviceLayer::SystemLayer().CancelTimer(HandleCentralScanTimeout, this);
        mCentralState = CentralState::kConnecting;
        err = StartCentralConnection();
        if (err != CHIP_NO_ERROR)
        {
            HandleCentralConnectionError(err);
        }
        break;

    case DeviceEventType::kPlatformBflbBleCentralScanError:
        if (mCentralState == CentralState::kScanMatchPending)
        {
            ChipLogError(Ble, "Failed to stop BLE scan after Matter advertisement match: %d",
                         static_cast<int>(event->Platform.BleCentralScanErrorEvent.Status));
            mCentralState = CentralState::kScanning;
            HandleCentralConnectionError(CHIP_ERROR_INTERNAL);
        }
        break;

    case DeviceEventType::kPlatformBflbBleCentralConnected: {
        const auto & connectionEvent = event->Platform.BleCentralConnectionEvent;
        bt_conn * connection         = connectionEvent.BtConn;
        if (connectionEvent.HciResult != BT_HCI_ERR_SUCCESS)
        {
            bt_conn_unref(connection);
            HandleCentralConnectionError(CHIP_ERROR_CONNECTION_ABORTED);
            break;
        }

        ++mGAPConns;
        bt_conn_unref(connection);
        ChipLogProgress(Ble, "BLE central connection established");
        err = StartCentralServiceDiscovery();
        if (err != CHIP_NO_ERROR)
        {
            HandleCentralConnectionError(err);
        }
        break;
    }

    case DeviceEventType::kPlatformBflbBleCentralDisconnected: {
        const auto & connectionEvent = event->Platform.BleCentralConnectionEvent;
        bt_conn * connection         = connectionEvent.BtConn;
        const bool isCentralConnection = mCentralConnection == connection;
        const bool connectionReady   = mCentralState == CentralState::kConnected;
        const bool disconnectPending = mCentralState == CentralState::kDisconnecting;
        if (mGAPConns > 0)
        {
            --mGAPConns;
        }

        if (connectionReady)
        {
            mCentralState = CentralState::kDisconnecting;
            HandleConnectionError(connection, BLE_ERROR_REMOTE_DEVICE_DISCONNECTED);
        }
        else if (!disconnectPending)
        {
            void * appState = mCentralAppState;
            mCentralAppState = nullptr;
            mCentralState = CentralState::kDisconnecting;
            if (appState != nullptr)
            {
                BleConnectionDelegate::OnConnectionError(appState, CHIP_ERROR_CONNECTION_ABORTED);
            }
        }

        bt_conn_unref(connection);
        if (isCentralConnection)
        {
            // The Bouffalo stack has already consumed the connection's original reference by the time
            // its disconnection callback is processed. The event reference above is the final live one.
            mCentralConnection = nullptr;
        }
        ResetCentralConnection();
        break;
    }

    case DeviceEventType::kPlatformBflbBleCentralGattReady:
        if (event->Platform.BleCentralGattReadyEvent.Status != 0 || mCentralConnection == nullptr)
        {
            ChipLogError(Ble, "Matter GATT service discovery failed: status %u",
                         event->Platform.BleCentralGattReadyEvent.Status);
            HandleCentralConnectionError(CHIP_ERROR_NOT_FOUND);
            break;
        }

        mCentralState = CentralState::kConnected;
        ChipLogProgress(Ble, "Matter BLE GATT service discovered; starting PASE");
        BleConnectionDelegate::OnConnectionComplete(mCentralAppState, mCentralConnection);
        mCentralAppState = nullptr;
        break;

    case DeviceEventType::kPlatformBflbBleCentralIndication: {
        const auto & dataEvent = event->Platform.BleCentralDataEvent;
        bt_conn * connection  = dataEvent.BtConn;
        if (dataEvent.Data == nullptr)
        {
            HandleUnsubscribeComplete(connection, &CHIP_BLE_SVC_ID, &CHIP_BLE_CHAR_2_UUID);
        }
        else
        {
            if (mCentralSubscribePending)
            {
                mCentralSubscribePending = false;
                HandleSubscribeComplete(connection, &CHIP_BLE_SVC_ID, &CHIP_BLE_CHAR_2_UUID);
            }
            HandleIndicationReceived(connection, &CHIP_BLE_SVC_ID, &CHIP_BLE_CHAR_2_UUID,
                                     PacketBufferHandle::Adopt(dataEvent.Data));
        }
        if (connection != nullptr)
        {
            bt_conn_unref(connection);
        }
        break;
    }

    case DeviceEventType::kPlatformBflbBleCentralWriteComplete: {
        const auto & dataEvent = event->Platform.BleCentralDataEvent;
        bt_conn * connection  = dataEvent.BtConn;
        mCentralWriteBuffer = nullptr;
        if (dataEvent.Status == 0)
        {
            HandleWriteConfirmation(connection, &CHIP_BLE_SVC_ID, &CHIP_BLE_CHAR_1_UUID);
        }
        else
        {
            HandleConnectionError(connection, BLE_ERROR_GATT_WRITE_FAILED);
        }
        bt_conn_unref(connection);
        break;
    }
#endif

    default:
        break;
    }

    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "Disabling CHIPoBLE service due to error: %" CHIP_ERROR_FORMAT, err.Format());
        mServiceMode = ConnectivityManager::kCHIPoBLEServiceMode_Disabled;
        TEMPORARY_RETURN_IGNORED PlatformMgr().ScheduleWork(DriveBLEState, 0);
    }
}

uint16_t BLEManagerImpl::_NumConnections(void)
{
    return mGAPConns;
}

CHIP_ERROR BLEManagerImpl::CloseConnection(BLE_CONNECTION_OBJECT conId)
{
    ChipLogProgress(DeviceLayer, "Closing BLE GATT connection (ConnId %02x)", bt_conn_index((bt_conn *) conId));
    return CHIP_ERROR_POSIX(-bt_conn_disconnect((bt_conn *) conId, BT_HCI_ERR_REMOTE_USER_TERM_CONN));
}

uint16_t BLEManagerImpl::GetMTU(BLE_CONNECTION_OBJECT conId) const
{
    return bt_gatt_get_mtu((bt_conn *) conId);
}

CHIP_ERROR BLEManagerImpl::SubscribeCharacteristic(BLE_CONNECTION_OBJECT conId, const ChipBleUUID * svcId,
                                                   const ChipBleUUID * charId)
{
#if CHIP_DEVICE_CONFIG_ENABLE_BOUFFALO_BLE_CENTRAL
    VerifyOrReturnError(mCentralState == CentralState::kConnected && conId == mCentralConnection, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(Ble::UUIDsMatch(svcId, &CHIP_BLE_SVC_ID) && Ble::UUIDsMatch(charId, &CHIP_BLE_CHAR_2_UUID),
                        CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(mCentralTxValueHandle != 0 && mCentralTxCccHandle != 0, CHIP_ERROR_INCORRECT_STATE);

    mCentralSubscribeParams            = {};
    mCentralSubscribeParams.notify     = HandleCentralNotification;
    mCentralSubscribeParams.value_handle = mCentralTxValueHandle;
    mCentralSubscribeParams.ccc_handle   = mCentralTxCccHandle;
    mCentralSubscribeParams.value        = BT_GATT_CCC_INDICATE;

    const int status = bt_gatt_subscribe(mCentralConnection, &mCentralSubscribeParams);
    VerifyOrReturnError(status == 0, CHIP_ERROR_INTERNAL);

    // The SDK subscribe API has no completion callback for the CCC write. Queue
    // the Matter completion notification after this stack call has returned;
    // indications themselves are still delivered only through the subscription.
    mCentralSubscribePending = true;
    ReturnErrorOnFailure(PlatformMgr().ScheduleWork(HandleCentralSubscribeWork, 0));
    return CHIP_NO_ERROR;
#else
    ChipLogError(DeviceLayer, "%s: NOT IMPLEMENTED", __PRETTY_FUNCTION__);
    return CHIP_ERROR_NOT_IMPLEMENTED;
#endif
}

CHIP_ERROR BLEManagerImpl::UnsubscribeCharacteristic(BLE_CONNECTION_OBJECT conId, const ChipBleUUID * svcId,
                                                     const ChipBleUUID * charId)
{
#if CHIP_DEVICE_CONFIG_ENABLE_BOUFFALO_BLE_CENTRAL
    VerifyOrReturnError(mCentralConnection != nullptr && conId == mCentralConnection, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(Ble::UUIDsMatch(svcId, &CHIP_BLE_SVC_ID) && Ble::UUIDsMatch(charId, &CHIP_BLE_CHAR_2_UUID),
                        CHIP_ERROR_INVALID_ARGUMENT);
    const int status = bt_gatt_unsubscribe(mCentralConnection, &mCentralSubscribeParams);
    return status == 0 ? CHIP_NO_ERROR : CHIP_ERROR_INTERNAL;
#else
    ChipLogError(DeviceLayer, "%s: NOT IMPLEMENTED", __PRETTY_FUNCTION__);
    return CHIP_ERROR_NOT_IMPLEMENTED;
#endif
}

CHIP_ERROR BLEManagerImpl::SendIndication(BLE_CONNECTION_OBJECT conId, const ChipBleUUID * svcId, const ChipBleUUID * charId,
                                          PacketBufferHandle pBuf)
{
    CHIP_ERROR err                   = CHIP_NO_ERROR;
    int status                       = 0;
    uint8_t index                    = bt_conn_index(conId);
    bt_gatt_indicate_params * params = &mIndicateParams[index];

    VerifyOrExit(IsSubscribed(conId) == true, err = CHIP_ERROR_INVALID_ARGUMENT);

    ChipLogDetail(DeviceLayer, "Sending indication for CHIPoBLE TX characteristic (ConnId %02x, len %u)", index,
                  pBuf->DataLength());

    params->uuid = nullptr;
    params->attr = &sChipoBleAttributes[kCHIPoBLE_CCC_AttributeIndex];
    params->func = HandleTXIndicated;
    params->data = pBuf->Start();
    // For BLE, the buffer is capped at UINT16_MAX. Nevertheless, have a verify
    // check before the cast to uint16_t.
    VerifyOrExit(CanCastTo<uint16_t>(pBuf->DataLength()), err = CHIP_ERROR_MESSAGE_TOO_LONG);
    params->len = static_cast<uint16_t>(pBuf->DataLength());

    status = bt_gatt_indicate(conId, params);
    VerifyOrExit(status == 0, err = CHIP_ERROR_POSIX(-status));

exit:
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "BLEManagerImpl::SendIndication() failed: %d", status);
    }

    return err;
}

CHIP_ERROR BLEManagerImpl::SendWriteRequest(BLE_CONNECTION_OBJECT conId, const ChipBleUUID * svcId, const ChipBleUUID * charId,
                                            PacketBufferHandle pBuf)
{
#if CHIP_DEVICE_CONFIG_ENABLE_BOUFFALO_BLE_CENTRAL
    VerifyOrReturnError(mCentralConnection != nullptr && conId == mCentralConnection, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(Ble::UUIDsMatch(svcId, &CHIP_BLE_SVC_ID) && Ble::UUIDsMatch(charId, &CHIP_BLE_CHAR_1_UUID),
                        CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(mCentralRxValueHandle != 0 && !pBuf.IsNull() && mCentralWriteBuffer.IsNull(), CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(CanCastTo<uint16_t>(pBuf->DataLength()), CHIP_ERROR_MESSAGE_TOO_LONG);

    mCentralWriteBuffer = std::move(pBuf);
    mCentralWriteParams = {};
    mCentralWriteParams.func   = HandleCentralWriteResponse;
    mCentralWriteParams.handle = mCentralRxValueHandle;
    mCentralWriteParams.data   = mCentralWriteBuffer->Start();
    mCentralWriteParams.length = static_cast<uint16_t>(mCentralWriteBuffer->DataLength());

    const int status = bt_gatt_write(mCentralConnection, &mCentralWriteParams);
    if (status != 0)
    {
        mCentralWriteBuffer = nullptr;
        return CHIP_ERROR_INTERNAL;
    }
    return CHIP_NO_ERROR;
#else
    ChipLogError(DeviceLayer, "%s: NOT IMPLEMENTED", __PRETTY_FUNCTION__);
    return CHIP_ERROR_NOT_IMPLEMENTED;
#endif
}

void BLEManagerImpl::NotifyChipConnectionClosed(BLE_CONNECTION_OBJECT conId)
{
    TEMPORARY_RETURN_IGNORED CloseConnection(conId);
}

bool BLEManagerImpl::IsSubscribed(bt_conn * conn)
{
    return mSubscribedConns[bt_conn_index(conn)];
}

bool BLEManagerImpl::SetSubscribed(bt_conn * conn)
{
    uint8_t index           = bt_conn_index(conn);
    bool isSubscribed       = mSubscribedConns[index];
    mSubscribedConns[index] = true;

    // If we were not subscribed previously, increment the reference counter of the connection.
    if (!isSubscribed)
    {
        bt_conn_ref(conn);
    }

    return !isSubscribed;
}

bool BLEManagerImpl::UnsetSubscribed(bt_conn * conn)
{
    uint8_t index           = bt_conn_index(conn);
    bool isSubscribed       = mSubscribedConns[index];
    mSubscribedConns[index] = false;

    // If we were subscribed previously, decrement the reference counter of the connection.
    if (isSubscribed)
    {
        bt_conn_unref(conn);
    }

    return isSubscribed;
}

ssize_t BLEManagerImpl::HandleRXWrite(struct bt_conn * conId, const struct bt_gatt_attr * attr, const void * buf, uint16_t len,
                                      uint16_t offset, uint8_t flags)
{
    ChipDeviceEvent event;
    PacketBufferHandle packetBuf = PacketBufferHandle::NewWithData(buf, len);

    if (!packetBuf.IsNull())
    {
        // Arrange to post a CHIPoBLERXWriteEvent event to the CHIP queue.
        event.Type                            = DeviceEventType::kPlatformBflbBleC1WriteEvent;
        event.Platform.BleC1WriteEvent.BtConn = bt_conn_ref(conId);
        event.Platform.BleC1WriteEvent.Data   = std::move(packetBuf).UnsafeRelease();
    }

    // If we failed to allocate a buffer, post a kPlatformBflbBleOutOfBuffersEvent event.
    else
    {
        event.Type = DeviceEventType::kPlatformBflbBleOutOfBuffersEvent;
    }

    PlatformMgr().PostEventOrDie(&event);

    return len;
}

bool BLEManagerImpl::HandleTXCCCWrite(struct bt_conn * conId, const struct bt_gatt_attr * attr, uint16_t value)
{
    ChipDeviceEvent event;

    if (value != BT_GATT_CCC_INDICATE && value != 0)
    {
        return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }

    event.Type                             = DeviceEventType::kPlatformBflbBleCCCWrite;
    event.Platform.BleCCCWriteEvent.BtConn = bt_conn_ref(conId);
    event.Platform.BleCCCWriteEvent.Value  = value;

    PlatformMgr().PostEventOrDie(&event);

    return sizeof(value);
}

void BLEManagerImpl::HandleTXIndicated(struct bt_conn * conId, IndicationAttrType, uint8_t err)
{
    ChipDeviceEvent event;

    event.Type                              = DeviceEventType::kPlatformBflbBleC2IndDoneEvent;
    event.Platform.BleC2IndDoneEvent.BtConn = bt_conn_ref(conId);
    event.Platform.BleC2IndDoneEvent.Result = err;

    PlatformMgr().PostEventOrDie(&event);
}

void BLEManagerImpl::HandleConnect(struct bt_conn * conId, uint8_t err)
{
    ChipDeviceEvent event;

    PlatformMgr().LockChipStack();

#if CHIP_DEVICE_CONFIG_ENABLE_BOUFFALO_BLE_CENTRAL
    if (sInstance.mCentralState == CentralState::kConnecting && sInstance.mCentralConnection == conId)
    {
        event.Type                                             = DeviceEventType::kPlatformBflbBleCentralConnected;
        event.Platform.BleCentralConnectionEvent.BtConn       = bt_conn_ref(conId);
        event.Platform.BleCentralConnectionEvent.HciResult    = err;
        PlatformMgr().PostEventOrDie(&event);
        PlatformMgr().UnlockChipStack();
        return;
    }
#endif

    // Don't handle BLE connecting events when it is not related to CHIPoBLE
    VerifyOrExit(sInstance.mFlags.Has(Flags::kChipoBleGattServiceRegister), );

    event.Type                            = DeviceEventType::kPlatformBflbBleConnected;
    event.Platform.BleConnEvent.BtConn    = bt_conn_ref(conId);
    event.Platform.BleConnEvent.HciResult = err;

    PlatformMgr().PostEventOrDie(&event);

exit:
    PlatformMgr().UnlockChipStack();
}

void BLEManagerImpl::HandleDisconnect(struct bt_conn * conId, uint8_t reason)
{
    ChipDeviceEvent event;

    PlatformMgr().LockChipStack();

#if CHIP_DEVICE_CONFIG_ENABLE_BOUFFALO_BLE_CENTRAL
    if (sInstance.mCentralState != CentralState::kIdle && sInstance.mCentralConnection == conId)
    {
        event.Type                                          = DeviceEventType::kPlatformBflbBleCentralDisconnected;
        event.Platform.BleCentralConnectionEvent.BtConn    = bt_conn_ref(conId);
        event.Platform.BleCentralConnectionEvent.HciResult = reason;
        PlatformMgr().PostEventOrDie(&event);
        PlatformMgr().UnlockChipStack();
        return;
    }
#endif

    // Don't handle BLE disconnecting events when it is not related to CHIPoBLE
    VerifyOrExit(sInstance.mFlags.Has(Flags::kChipoBleGattServiceRegister), );

    event.Type                            = DeviceEventType::kPlatformBflbBleDisconnected;
    event.Platform.BleConnEvent.BtConn    = bt_conn_ref(conId);
    event.Platform.BleConnEvent.HciResult = reason;

    PlatformMgr().PostEventOrDie(&event);

exit:
    PlatformMgr().UnlockChipStack();
}

#if CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
ssize_t BLEManagerImpl::HandleC3Read(struct bt_conn * conId, const struct bt_gatt_attr * attr, void * buf, uint16_t len,
                                     uint16_t offset)
{
    ChipLogDetail(DeviceLayer, "Read request received for CHIPoBLE C3 (ConnId 0x%02x)", bt_conn_index(conId));

    if (sInstance.c3CharDataBufferHandle.IsNull())
    {
        return 0;
    }

    // For BLE, the max payload size is limited to UINT16_MAX since the length
    // field is 2 bytes long. So, the cast to uint16_t should be fine.
    return bt_gatt_attr_read(conId, attr, buf, len, offset, sInstance.c3CharDataBufferHandle->Start(),
                             static_cast<uint16_t>(sInstance.c3CharDataBufferHandle->DataLength()));
}
#endif

} // namespace Internal
} // namespace DeviceLayer
} // namespace chip

#endif // CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE
