/*
 *    Copyright (c) 2026 Project CHIP Authors
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

/**
 * @file
 *   Declarations shared across the BFLB network commissioning drivers.
 *
 *   Both functions are defined in NetworkCommissioningDriver.cpp (the
 *   Wi-Fi glue merged from the former wifi_mgmr_portable.c):
 *
 *   - deviceInterface_getNetif(): STA netif accessor, shared with the SDK
 *     Matter examples' Wi-Fi backbone, route hooks and the platform
 *     diagnostics/configuration managers (the latter declare it locally).
 *   - wifi_event_handler(): SDK-async-event -> ChipDeviceEvent adapter,
 *     subscribed on the EV_WIFI filter during driver init.
 */

#pragma once

#include <lwip/netif.h>

#ifdef __cplusplus
extern "C" {
#endif

struct netif * deviceInterface_getNetif(void);

#include <async_event.h>
void wifi_event_handler(async_input_event_t ev, void * priv);

#ifdef __cplusplus
}
#endif
