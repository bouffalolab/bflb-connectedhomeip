/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
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
#include <stdint.h>
#include <string.h>

#include <lwip/ip4_addr.h>
#include <lwip/ip6_addr.h>
#include <lwip/netif.h>
#include <lwip/tcpip.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/bouffalolab/common/NetworkCommissioningDriver.h>
// Wi-Fi glue merged from wifi_mgmr_portable.c (see the definitions below the
// includes): the STA netif accessor shared with the SDK Matter examples'
// backbone/route hooks and the platform managers, plus the SDK scan wrapper.
// The Wi-Fi backend is selected by the SDK build (CONFIG_FHOST /
// CONFIG_WL80211, mutually exclusive); both backends ship the manager
// headers below with the same API and event codes.
#if defined(CONFIG_FHOST) && defined(CONFIG_WL80211)
#error "CONFIG_FHOST and CONFIG_WL80211 are mutually exclusive"
#endif

extern "C" {
#undef IS_ENABLED
#include <wifi_mgmr.h>
#include <wifi_mgmr_ext.h>
#include <async_event.h>
}

extern "C" struct netif * deviceInterface_getNetif(void)
{
    // Public lwIP lookup by interface name: the SDK registers the STA as
    // "wl1" on both Wi-Fi backends. Callers must not hold the TCP/IP core
    // lock here (this getter takes it, and lwIP core locks do not recurse).
    LOCK_TCPIP_CORE();
    struct netif * net_if = netif_find("wl1");
    UNLOCK_TCPIP_CORE();

    return net_if;
}

// SDK scan wrapper for the Wi-Fi commissioning scan (file-local).
static int wifi_start_scan(const uint8_t * ssid, uint32_t length)
{
    wifi_mgmr_scan_params_t config;

    memset(&config, 0, sizeof(wifi_mgmr_scan_params_t));
    if (length && length <= MGMR_SSID_LEN)
    {
#if defined(CONFIG_WL80211)
        config.ssid_length = (uint8_t) length;
        memcpy(config.ssid_array, ssid, length);
#else
        memcpy(config.ssid_array, ssid, length);
        config.ssid_length = length;
#endif
    }

    return wifi_mgmr_sta_scan(&config);
}

using namespace ::chip;
using namespace ::chip::DeviceLayer::Internal;
using namespace ::chip::Platform;

namespace chip {
namespace DeviceLayer {
namespace NetworkCommissioning {

auto converter = [](const wifi_mgmr_scan_item_t & raw) -> WiFiScanResponse {
    WiFiScanResponse item;

    item.security.SetRaw(raw.auth);
    item.ssidLen         = (uint32_t) (raw.ssid_len) < chip::DeviceLayer::Internal::kMaxWiFiSSIDLength
                ? raw.ssid_len
                : chip::DeviceLayer::Internal::kMaxWiFiSSIDLength;
    item.channel         = raw.channel;
    item.wiFiBand        = chip::DeviceLayer::NetworkCommissioning::WiFiBand::k2g4;
    item.signal.type     = WirelessSignalType::kdBm;
    item.signal.strength = raw.rssi;
    memcpy(item.ssid, raw.ssid, item.ssidLen);
    memcpy(item.bssid, raw.bssid, 6);

    return item;
};

// lwIP-side IP notification safety net (kGotIpv6Address / kGotIpAddress
// ChipDeviceEvents); defined at the bottom of this file, registered below.
extern "C" void matter_netif_ext_callback(struct netif * nif, netif_nsc_reason_t reason, const netif_ext_callback_args_t * args);

// SDK-async-event -> ChipDeviceEvent adapter; defined at the bottom of this
// file, subscribed on the EV_WIFI filter during driver init.
extern "C" void wifi_event_handler(async_input_event_t ev, void * priv);

static netif_ext_callback_t sMatterNetifExtCallback;

CHIP_ERROR BflbWiFiDriver::Init(NetworkStatusChangeCallback * networkStatusChangeCallback)
{
    mpScanCallback         = nullptr;
    mpConnectCallback      = nullptr;
    mpStatusChangeCallback = networkStatusChangeCallback;

    // The platform owns the registration of its own SDK-event adapter: the
    // EV_WIFI filter feeds wifi_event_handler below. The examples'
    // matter_wifi.c adds the manager auto-reconnect enabler on the same
    // filter.
    async_register_event_filter(EV_WIFI, wifi_event_handler, NULL);

    // The lwIP core is up at this point (SDK SYSINIT). Register the netif
    // notification so address changes that bypass the SDK async events are
    // still reported to the Matter event loop.
    LOCK_TCPIP_CORE();
    netif_add_ext_callback(&sMatterNetifExtCallback, matter_netif_ext_callback);

    // The station may have completed DHCP/RA before this registration (SDK
    // SYSINIT with saved credentials): replay the current address state so
    // the connectivity state machine starts from reality.
    struct netif * staNetif = netif_find("wl1");
    if (staNetif != NULL)
    {
        if (!ip4_addr_isany(netif_ip4_addr(staNetif)) && !ip4_addr_isany(netif_ip4_gw(staNetif)))
        {
            ChipDeviceEvent event;
            memset(&event, 0, sizeof(event));
            event.Type = DeviceEventType::kGotIpAddress;
            PlatformMgr().PostEventOrDie(&event);
        }

        for (int i = 0; i < LWIP_IPV6_NUM_ADDRESSES; i++)
        {
            if (!ip6_addr_islinklocal(netif_ip6_addr(staNetif, i)) && ip6_addr_ispreferred(netif_ip6_addr_state(staNetif, i)))
            {
                ChipDeviceEvent event;
                memset(&event, 0, sizeof(event));
                event.Type = DeviceEventType::kGotIpv6Address;
                PlatformMgr().PostEventOrDie(&event);
                break;
            }
        }
    }
    UNLOCK_TCPIP_CORE();

    // Saved-network storage is a product decision (see
    // bflb_matter_wifi_saved_network_load in NetworkCommissioningDriver.h).
    if (bflb_matter_wifi_saved_network_load(mSavedNetwork.ssid, &mSavedNetwork.ssidLen, mSavedNetwork.credentials,
                                            &mSavedNetwork.credentialsLen) != 0)
    {
        /** Not commissioned, SSID and Network Password not available. */
        return CHIP_NO_ERROR;
    }

    mStagingNetwork = mSavedNetwork;
    return CHIP_NO_ERROR;
}

void BflbWiFiDriver::Shutdown()
{
    mpStatusChangeCallback = nullptr;
}

CHIP_ERROR BflbWiFiDriver::CommitConfiguration()
{
    ChipLogProgress(NetworkProvisioning, "BflbWiFiDriver::CommitConfiguration");

    // Storage backend is a product decision (see the
    // bflb_matter_wifi_saved_network_* hooks in NetworkCommissioningDriver.h):
    // committing an empty SSID removes the saved network.
    if (mStagingNetwork.ssidLen == 0)
    {
        VerifyOrReturnError(bflb_matter_wifi_saved_network_delete() == 0, CHIP_ERROR_PERSISTED_STORAGE_FAILED);
    }
    else
    {
        VerifyOrReturnError(bflb_matter_wifi_saved_network_store(mStagingNetwork.ssid, mStagingNetwork.ssidLen,
                                                                 mStagingNetwork.credentials, mStagingNetwork.credentialsLen) == 0,
                            CHIP_ERROR_PERSISTED_STORAGE_FAILED);
    }

    mSavedNetwork = mStagingNetwork;
    return CHIP_NO_ERROR;
}

CHIP_ERROR BflbWiFiDriver::RevertConfiguration()
{
    mStagingNetwork = mSavedNetwork;
    return CHIP_NO_ERROR;
}

bool BflbWiFiDriver::NetworkMatch(const WiFiNetwork & network, ByteSpan networkId)
{
    return networkId.size() == network.ssidLen && memcmp(networkId.data(), network.ssid, network.ssidLen) == 0;
}

Status BflbWiFiDriver::AddOrUpdateNetwork(ByteSpan ssid, ByteSpan credentials, MutableCharSpan & outDebugText,
                                          uint8_t & outNetworkIndex)
{
    outDebugText.reduce_size(0);
    outNetworkIndex = 0;

    VerifyOrReturnError(mStagingNetwork.ssidLen == 0 || NetworkMatch(mStagingNetwork, ssid), Status::kBoundsExceeded);
    VerifyOrReturnError(credentials.size() <= sizeof(mStagingNetwork.credentials), Status::kOutOfRange);
    VerifyOrReturnError(ssid.size() <= sizeof(mStagingNetwork.ssid), Status::kOutOfRange);

    memcpy(mStagingNetwork.credentials, credentials.data(), credentials.size());
    mStagingNetwork.credentialsLen = static_cast<decltype(mStagingNetwork.credentialsLen)>(credentials.size());

    memcpy(mStagingNetwork.ssid, ssid.data(), ssid.size());
    mStagingNetwork.ssidLen = static_cast<decltype(mStagingNetwork.ssidLen)>(ssid.size());

    return Status::kSuccess;
}

Status BflbWiFiDriver::RemoveNetwork(ByteSpan networkId, MutableCharSpan & outDebugText, uint8_t & outNetworkIndex)
{
    outDebugText.reduce_size(0);
    outNetworkIndex = 0;

    VerifyOrReturnError(NetworkMatch(mStagingNetwork, networkId), Status::kNetworkIDNotFound);

    // Use empty ssid for representing invalid network
    mStagingNetwork.ssidLen = 0;
    return Status::kSuccess;
}

Status BflbWiFiDriver::ReorderNetwork(ByteSpan networkId, uint8_t index, MutableCharSpan & outDebugText)
{
    outDebugText.reduce_size(0);

    // Only one network is supported now
    VerifyOrReturnError(index == 0, Status::kOutOfRange);
    VerifyOrReturnError(NetworkMatch(mStagingNetwork, networkId), Status::kNetworkIDNotFound);
    return Status::kSuccess;
}

CHIP_ERROR BflbWiFiDriver::ConnectWiFiNetwork(const char * ssid, uint8_t ssidLen, const char * key, uint8_t keyLen)
{
    // Called both by ConnectNetwork (commissioning) and
    // ConnectivityManagerImpl::ConnectProvisionedWiFiNetwork
    // (auto-reconnect): skip the connect request when the station is
    // already associated with this network, the SDK auto-reconnect owns
    // such a link and a new attempt would only bounce it.
    if (IsConnectedTo(ssid, ssidLen))
    {
        ChipLogProgress(NetworkProvisioning, "Already connected to SSID, skipping connect");
        return CHIP_NO_ERROR;
    }

    wifi_mgmr_sta_connect_params_t conn_param = { 0 };

    ConnectivityMgrImpl().ChangeWiFiStationState(ConnectivityManager::kWiFiStationState_Connecting);

    // Bounds check added to prevent buffer overflow vulnerabilities.
#if defined(CONFIG_WL80211)
    VerifyOrReturnError(ssidLen < sizeof(conn_param.ssid), CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(keyLen < sizeof(conn_param.key), CHIP_ERROR_BUFFER_TOO_SMALL);
    memcpy(conn_param.ssid, ssid, ssidLen);
    conn_param.ssid[ssidLen] = '\0';
    if (keyLen != 0)
    {
        memcpy(conn_param.key, key, keyLen);
        conn_param.key[keyLen] = '\0';
    }
#else
    VerifyOrReturnError(ssidLen <= sizeof(conn_param.ssid), CHIP_ERROR_BUFFER_TOO_SMALL);
    memcpy(conn_param.ssid, ssid, ssidLen);
    conn_param.ssid_len = ssidLen;
    if (keyLen != 0)
    {
        VerifyOrReturnError(keyLen <= sizeof(conn_param.key), CHIP_ERROR_BUFFER_TOO_SMALL);
        memcpy(conn_param.key, key, keyLen);
        conn_param.key_len = keyLen;
    }
#endif

    conn_param.freq1         = 0;
    conn_param.freq2         = 0;
    conn_param.use_dhcp      = 1;
    conn_param.pmf_cfg       = 1;
    conn_param.quick_connect = 1;
    conn_param.timeout_ms    = -1;

    VerifyOrReturnError(wifi_mgmr_sta_connect(&conn_param) == 0, CHIP_ERROR_INTERNAL);
    return CHIP_NO_ERROR;
}

void BflbWiFiDriver::OnConnectWiFiNetwork(bool isConnected)
{
    if (mpConnectCallback)
    {
        if (isConnected)
        {
            mpConnectCallback->OnResult(Status::kSuccess, CharSpan(), 0);
        }
        else
        {
            mpConnectCallback->OnResult(Status::kUnknownError, CharSpan(), 0);
        }
        mpConnectCallback = nullptr;
    }
}

void BflbWiFiDriver::ConnectNetwork(ByteSpan networkId, ConnectCallback * callback)
{
    CHIP_ERROR err          = CHIP_NO_ERROR;
    Status networkingStatus = Status::kSuccess;

    VerifyOrExit(NetworkMatch(mStagingNetwork, networkId), networkingStatus = Status::kNetworkIDNotFound);
    VerifyOrExit(mpConnectCallback == nullptr, networkingStatus = Status::kUnknownError);
    ChipLogProgress(NetworkProvisioning, "BL NetworkCommissioningDelegate: SSID: %s", NullTerminated(networkId).c_str());

    // Skip the connect request when the station is already associated with
    // this network: the SDK auto-reconnect owns such a link and a new
    // connect attempt would only bounce it. The callback is resolved here
    // (ConnectWiFiNetwork cannot: no ConnectCallback flows through it).
    if (IsConnectedTo(mStagingNetwork.ssid, mStagingNetwork.ssidLen))
    {
        ChipLogProgress(NetworkProvisioning, "Already connected to SSID, skipping connect");
        callback->OnResult(Status::kSuccess, CharSpan(), 0);
        return;
    }

    err               = ConnectWiFiNetwork(reinterpret_cast<const char *>(mStagingNetwork.ssid), mStagingNetwork.ssidLen,
                                           reinterpret_cast<const char *>(mStagingNetwork.credentials), mStagingNetwork.credentialsLen);
    mpConnectCallback = callback;

exit:
    if (err != CHIP_NO_ERROR)
    {
        networkingStatus = Status::kUnknownError;
    }
    if (networkingStatus != Status::kSuccess)
    {
        ChipLogError(NetworkProvisioning, "Failed to connect to WiFi network: %" CHIP_ERROR_FORMAT, err.Format());
        mpConnectCallback = nullptr;
        callback->OnResult(networkingStatus, CharSpan(), 0);
    }
}

void BflbWiFiDriver::ScanNetworks(ByteSpan ssid, WiFiDriver::ScanCallback * callback)
{
    if (callback != nullptr)
    {
        mpScanCallback  = nullptr;
        mScanSSIDlength = 0;

        if (ssid.size() <= sizeof(mScanSSID) && 0 == wifi_start_scan(ssid.data(), ssid.size()))
        {
            memcpy(mScanSSID, ssid.data(), ssid.size());
            mScanSSIDlength = ssid.size();
            mpScanCallback  = callback;
        }
        else
        {
            callback->OnFinished(Status::kUnknownError, CharSpan(), nullptr);
        }
    }
}

void BflbWiFiDriver::OnScanWiFiNetworkDone()
{
    uint32_t nums                          = wifi_mgmr_sta_scanlist_nums_get();
    struct wifi_mgmr_scan_item * pScanList = nullptr;
    wifi_mgmr_scan_item_t * pScanResult    = nullptr;
    uint32_t scanResultNum                 = 0;

    if (nums)
    {
        pScanList = (struct wifi_mgmr_scan_item *) MemoryAlloc(nums * sizeof(wifi_mgmr_scan_item_t));
        if (pScanList && wifi_mgmr_sta_scanlist_dump(pScanList, nums))
        {
            if (mScanSSIDlength)
            {
                for (uint32_t i = 0; i < nums; i++)
                {
                    if (mScanSSIDlength == pScanList[i].ssid_len && memcmp(pScanList[i].ssid, mScanSSID, mScanSSIDlength) == 0)
                    {
                        pScanResult   = &pScanList[i];
                        scanResultNum = 1;
                        break;
                    }
                }
            }
            else
            {
                pScanResult   = pScanList;
                scanResultNum = nums;
            }
        }
    }

    CHIP_ERROR err = DeviceLayer::SystemLayer().ScheduleLambda([scanResultNum, pScanResult, pScanList]() {
        if (GetInstance().mpScanCallback)
        {
            BflbScanResponseIterator iter(scanResultNum, pScanResult, converter);
            GetInstance().mpScanCallback->OnFinished(Status::kSuccess, CharSpan(), &iter);
            GetInstance().mpScanCallback = nullptr;
        }
        else
        {
            ChipLogError(DeviceLayer, "can't find the ScanCallback function");
        }

        if (pScanList)
        {
            MemoryFree(pScanList);
        }
    });
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "can't schedule the scan result processing: %" CHIP_ERROR_FORMAT, err.Format());
        if (mpScanCallback)
        {
            mpScanCallback->OnFinished(Status::kUnknownError, CharSpan(), nullptr);
            mpScanCallback = nullptr;
        }
        if (pScanList)
        {
            MemoryFree(pScanList);
        }
    }
    mScanSSIDlength = 0;
}

CHIP_ERROR GetConfiguredNetwork(Network & network)
{
    wifi_mgmr_connect_ind_stat_info_t statInfo;

    memset(&statInfo, 0, sizeof(wifi_mgmr_connect_ind_stat_info_t));
    if (0 == wifi_mgmr_sta_connect_ind_stat_get(&statInfo))
    {
        memcpy(network.networkID, statInfo.ssid, strlen(statInfo.ssid));
        network.networkIDLen = strlen(statInfo.ssid);
        return CHIP_NO_ERROR;
    }

    return CHIP_ERROR_INTERNAL;
}

bool BflbWiFiDriver::IsConnectedTo(const char * ssid, uint8_t ssidLen)
{
    if (ConnectivityManagerImpl().GetWiFiStationState() != ConnectivityManager::kWiFiStationState_Connected)
    {
        return false;
    }

    Network connectedNetwork;
    return GetConfiguredNetwork(connectedNetwork) == CHIP_NO_ERROR && connectedNetwork.networkIDLen == ssidLen &&
        memcmp(connectedNetwork.networkID, ssid, ssidLen) == 0;
}

void BflbWiFiDriver::OnNetworkStatusChange()
{
    Network configuredNetwork;

    VerifyOrReturn(mpStatusChangeCallback != nullptr);
    if (GetConfiguredNetwork(configuredNetwork) != CHIP_NO_ERROR)
    {
        return;
    }

    if (ConnectivityManagerImpl().GetWiFiStationState() == ConnectivityManager::kWiFiStationState_Connected)
    {
        mpStatusChangeCallback->OnNetworkingStatusChange(
            Status::kSuccess, MakeOptional(ByteSpan(configuredNetwork.networkID, configuredNetwork.networkIDLen)), NullOptional);
    }
    else
    {
        mpStatusChangeCallback->OnNetworkingStatusChange(
            Status::kUnknownError, MakeOptional(ByteSpan(configuredNetwork.networkID, configuredNetwork.networkIDLen)),
            MakeOptional(GetLastDisconnectReason()));
    }
}

CHIP_ERROR BflbWiFiDriver::SetLastDisconnectReason(const ChipDeviceEvent * event)
{
#if defined(CONFIG_WL80211)
    mLastDisconnectedReason = wifi_mgmr_sta_info_reason_code_get();
#else
    mLastDisconnectedReason = wifi_mgmr_sta_info_status_code_get();
#endif

    return CHIP_NO_ERROR;
}

int32_t BflbWiFiDriver::GetLastDisconnectReason()
{
    return mLastDisconnectedReason;
}

size_t BflbWiFiDriver::WiFiNetworkIterator::Count()
{
    return mDriver->mStagingNetwork.ssidLen == 0 ? 0 : 1;
}

bool BflbWiFiDriver::WiFiNetworkIterator::Next(Network & item)
{
    if (mExhausted || mDriver->mStagingNetwork.ssidLen == 0)
    {
        return false;
    }
    memcpy(item.networkID, mDriver->mStagingNetwork.ssid, mDriver->mStagingNetwork.ssidLen);
    item.networkIDLen = mDriver->mStagingNetwork.ssidLen;
    item.connected    = false;
    mExhausted        = true;

    Network connectedNetwork;
    CHIP_ERROR err = GetConfiguredNetwork(connectedNetwork);
    if (err == CHIP_NO_ERROR)
    {
        if (connectedNetwork.networkIDLen == item.networkIDLen &&
            memcmp(connectedNetwork.networkID, item.networkID, item.networkIDLen) == 0)
        {
            item.connected = true;
        }
    }
    return true;
}

void NetworkEventHandler(const ChipDeviceEvent * event, intptr_t arg)
{
    if (!(DeviceEventType::IsPlatformSpecific(event->Type) && DeviceEventType::IsPublic(event->Type)))
    {
        return;
    }

    switch (event->Type)
    {
    case DeviceEventType::kWiFiOnScanDone:
        BflbWiFiDriver::GetInstance().OnScanWiFiNetworkDone();
        break;
    case DeviceEventType::kWiFiOnConnecting:
        ConnectivityMgrImpl().ChangeWiFiStationState(ConnectivityManager::kWiFiStationState_Connecting);
        ConnectivityMgrImpl().OnConnectivityChanged(deviceInterface_getNetif());
        break;
    case DeviceEventType::kWiFiOnConnected:
        BflbWiFiDriver::GetInstance().OnNetworkStatusChange();
        break;
    case DeviceEventType::kGotIpAddress:
        ConnectivityMgrImpl().ChangeWiFiStationState(ConnectivityManagerImpl::kWiFiStationState_Connected);
        ConnectivityMgrImpl().OnConnectivityChanged(deviceInterface_getNetif());
        break;
    case DeviceEventType::kGotIpv6Address:
        ConnectivityMgrImpl().ChangeWiFiStationState(ConnectivityManagerImpl::kWiFiStationState_Connected);
        ConnectivityMgrImpl().OnConnectivityChanged(deviceInterface_getNetif());
        break;
    case DeviceEventType::kWiFiOnDisconnected:
        if (ConnectivityManager::kWiFiStationState_Connecting == ConnectivityMgrImpl().GetWiFiStationState())
        {
            ConnectivityMgrImpl().ChangeWiFiStationState(ConnectivityManager::kWiFiStationState_Connecting_Failed);
        }
        break;
    case DeviceEventType::kPlatformBflbRebootRequest:
        // Handled by the application layer, which owns the reboot decision.
        break;
    default:
        ChipLogProgress(DeviceLayer, "Undefined network commission event type %x.\r\n", event->Type);
        break;
    }
}

// Matter notification netif callback: watches IP address changes and
// posts kGotIpv6Address / kGotIpAddress ChipDeviceEvents. Registered by
// the SDK Matter examples' Wi-Fi backbone (examples/matter/common/platform/matter_wifi.c).
extern "C" void matter_netif_ext_callback(struct netif * nif, netif_nsc_reason_t reason,
                                          const netif_ext_callback_args_t * args)
{
    ChipDeviceEvent event;

    memset(&event, 0, sizeof(ChipDeviceEvent));

    if (((LWIP_NSC_IPV6_ADDR_STATE_CHANGED | LWIP_NSC_IPV6_SET) & reason) && args)
    {
        const uint8_t addrIndex = args->ipv6_addr_state_changed.addr_index;
        if (addrIndex >= LWIP_IPV6_NUM_ADDRESSES)
        {
            return;
        }

        const uint8_t addressState = netif_ip6_addr_state(nif, addrIndex);
        if (ip6_addr_islinklocal(netif_ip6_addr(nif, addrIndex)))
        {
            return;
        }

        if (addressState != args->ipv6_addr_state_changed.old_state && ip6_addr_ispreferred(addressState))
        {
            event.Type = DeviceEventType::kGotIpv6Address;
            PlatformMgr().PostEventOrDie(&event);
        }
    }

    if ((LWIP_NSC_IPV4_SETTINGS_CHANGED & reason) && args)
    {
        if (!ip4_addr_isany(netif_ip4_addr(nif)) && !ip4_addr_isany(netif_ip4_gw(nif)))
        {
            event.Type = DeviceEventType::kGotIpAddress;
            PlatformMgr().PostEventOrDie(&event);
        }
    }
}

extern "C" void wifi_event_handler(async_input_event_t ev, void * priv)
{
    // Bridge only the SDK Wi-Fi events Matter consumes into the Matter
    // event loop; registered on the EV_WIFI filter by BflbWiFiDriver::Init.
    // IP-address notifications arrive via the lwIP netif callback
    // (matter_netif_ext_callback, which reflects the actual netif state and
    // also covers static addressing), and the SDK-flow steps (manager
    // auto-reconnect enable) live in the examples' matter_wifi.c.
    ChipDeviceEvent event;
    (void) priv;

    memset(&event, 0, sizeof(ChipDeviceEvent));

    switch (ev->code)
    {
    case CODE_WIFI_ON_SCAN_DONE:
        event.Type = DeviceEventType::kWiFiOnScanDone;
        PlatformMgr().PostEventOrDie(&event);
        break;
    case CODE_WIFI_ON_CONNECTING:
        event.Type = DeviceEventType::kWiFiOnConnecting;
        PlatformMgr().PostEventOrDie(&event);
        break;
    case CODE_WIFI_ON_CONNECTED:
        event.Type = DeviceEventType::kWiFiOnConnected;
        PlatformMgr().PostEventOrDie(&event);
        break;
    case CODE_WIFI_ON_DISCONNECT:
        event.Type = DeviceEventType::kWiFiOnDisconnected;
        PlatformMgr().PostEventOrDie(&event);
        break;
    case DeviceEventType::kPlatformBflbRebootRequest:
        // Handled by the application layer, which owns the reboot decision.
        break;
    default:
        break;
    }
}

} // namespace NetworkCommissioning
} // namespace DeviceLayer
} // namespace chip
// #endif // CHIP_DEVICE_CONFIG_ENABLE_WIFI
