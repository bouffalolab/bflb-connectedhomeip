/*
 *
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
 *    @file
 *          Project configuration for the chip-tool controller example on
 *          Bouffalo Lab BL618DG.
 *
 */

#pragma once

// CSR validation during controller commissioning needs more stack than the
// default Bouffalo Matter event-loop task allocation.
#ifndef CHIP_DEVICE_CONFIG_CHIP_TASK_STACK_SIZE
#define CHIP_DEVICE_CONFIG_CHIP_TASK_STACK_SIZE (16 * 1024)
#endif

// Only the chip-tool controller initiates BLE connections to commissionees.
#define CHIP_DEVICE_CONFIG_ENABLE_BOUFFALO_BLE_CENTRAL 1

// The controller commissions other devices; it never advertises itself.
#ifndef CHIP_DEVICE_CONFIG_CHIPOBLE_ENABLE_ADVERTISING_AUTOSTART
#define CHIP_DEVICE_CONFIG_CHIPOBLE_ENABLE_ADVERTISING_AUTOSTART 0
#endif

// Test vendor id, same as Linux chip-tool.
#ifndef CHIP_DEVICE_CONFIG_DEVICE_VENDOR_ID
#define CHIP_DEVICE_CONFIG_DEVICE_VENDOR_ID 0xFFF1
#endif

#ifndef CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_ID
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_ID 0x8010
#endif

#ifndef CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION_STRING
#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION_STRING "1.0"
#endif

#ifndef CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION
#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION 0x0001
#endif

// Security test mode stays OFF for the controller.
#define CHIP_CONFIG_SECURITY_TEST_MODE 0

// Command timeout: devices on busy networks may take longer than the
// default; keep the chip-tool default unless testing shows otherwise.
#ifndef CHIP_DEVICE_CONFIG_FILTERED_DISCOVERY_TIMEOUT
#define CHIP_DEVICE_CONFIG_FILTERED_DISCOVERY_TIMEOUT (60 * chip::System::kSecondsPerMinute)
#endif
