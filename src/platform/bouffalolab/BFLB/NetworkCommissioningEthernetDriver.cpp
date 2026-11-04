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

#include <lwip/dhcp6.h>
#include <lwip/ethip6.h>
#include <lwip/netifapi.h>

#include <lib/support/SafePointerCast.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/bouffalolab/common/NetworkCommissioningDriver.h>

using namespace chip::DeviceLayer::Internal;

static netif_ext_callback_t netifExtCallback;

extern "C" struct netif * deviceInterface_getNetif(void)
{
    // PlatformManager starts the SDK's EMAC interface before Matter init.
    return netif_default;
}

namespace chip {
namespace DeviceLayer {
namespace NetworkCommissioning {

void network_netif_ext_callback(struct netif * nif, netif_nsc_reason_t reason, const netif_ext_callback_args_t * args)
{
    if (nif != deviceInterface_getNetif())
    {
        return;
    }

    if (reason & (LWIP_NSC_LINK_CHANGED | LWIP_NSC_STATUS_CHANGED | LWIP_NSC_IPV4_SETTINGS_CHANGED | LWIP_NSC_IPV6_SET |
                  LWIP_NSC_IPV6_ADDR_STATE_CHANGED))
    {
        ConnectivityMgrImpl().OnConnectivityChanged(nif);
    }
}

CHIP_ERROR BflbEthernetDriver::Init(BaseDriver::NetworkStatusChangeCallback * networkStatusChangeCallback)
{
    struct netif * netif = deviceInterface_getNetif();
    VerifyOrReturnError(netif != nullptr, CHIP_ERROR_INCORRECT_STATE);

    LOCK_TCPIP_CORE();
    netif_add_ext_callback(&netifExtCallback, network_netif_ext_callback);
    netif_create_ip6_linklocal_address(netif, 1);
    netif_set_ip6_autoconfig_enabled(netif, 1);
    // DHCP may have completed before the Matter driver registered its callback.
    ConnectivityMgrImpl().OnConnectivityChanged(netif);
    UNLOCK_TCPIP_CORE();

    return CHIP_NO_ERROR;
}

NetworkIterator * BflbEthernetDriver::GetNetworks()
{
    auto ret = new EthernetNetworkIterator();
    memset(ret->interfaceName, 0, sizeof(ret->interfaceName));
    LOCK_TCPIP_CORE();
    struct netif * netif = deviceInterface_getNetif();
    if (netif != nullptr && netif_index_to_name(netif_get_index(netif), SafePointerCast<char *>(ret->interfaceName)))
    {
        ret->interfaceNameLen = NETIF_NAMESIZE;
    }
    else
    {
        ret->interfaceNameLen = 0;
    }
    UNLOCK_TCPIP_CORE();
    return ret;
}

void BflbEthernetDriver::Shutdown()
{
    LOCK_TCPIP_CORE();
    netif_remove_ext_callback(&netifExtCallback);
    UNLOCK_TCPIP_CORE();
}

} // namespace NetworkCommissioning
} // namespace DeviceLayer
} // namespace chip
