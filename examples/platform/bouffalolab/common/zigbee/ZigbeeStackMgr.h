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

#pragma once

#if CONFIG_ZIGBEE_APP

#include <lib/core/CHIPError.h>

namespace chip {
namespace DeviceLayer {

/**
 * @brief Bring up the external bouffalo_zigbee stack as a Zigbee Router on the
 *        BL616's shared 2.4 GHz radio (driven by the SDK's lmac154 802.15.4 MAC),
 *        coexisting with WiFi-Matter through the SDK's hardware PTA / coex layer.
 *
 *        Must be called after RF calibration (rfparam_init, done in board_init)
 *        and after the WiFi firmware task is running, so both sides of the radio
 *        arbiter are up. The stack self-initializes lmac154 + 802.15.4 coex via
 *        its macphy154 adapter during zb_stackInit(); this function only drives
 *        the upper stack (register an On/Off endpoint, set role = Router, start)
 *        and spawns a dedicated FreeRTOS task that pumps the stack.
 *
 *        The WiFi side of the coex arbiter is activated separately, after the
 *        station associates, via wifi_mgmr_sta_coex_enable() (see platform.cpp).
 */
CHIP_ERROR InitZigbeePlatform();

/**
 * @brief Called from the Matter event loop when Matter commissioning completes.
 *        Marks the device as Matter-owned so the Zigbee stack disallows joining.
 */
void OnMatterCommissioned();

/**
 * @brief Called from platform.cpp on WiFi IP-up. Enables the WiFi/Zigbee coex
 *        duty-cycling only while neither side owns the device yet (the dual
 *        commissioning window). Once Matter commissions or Zigbee joins, the
 *        coordination code disables coex (single radio user).
 */
void EnableWifiCoexIfNeeded();

} // namespace DeviceLayer
} // namespace chip

#endif // CONFIG_ZIGBEE_APP
