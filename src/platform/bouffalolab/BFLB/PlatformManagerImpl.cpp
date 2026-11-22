/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
 *    Copyright (c) 2019 Nest Labs, Inc.
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

#include <crypto/CHIPCryptoPAL.h>
#include <platform/internal/CHIPDeviceLayerInternal.h>

#include <platform/FreeRTOS/SystemTimeSupport.h>
#include <platform/PlatformManager.h>
#include <platform/bouffalolab/common/BflbConfig.h>
#include <platform/internal/GenericPlatformManagerImpl_FreeRTOS.ipp>



// Board entropy source. The remaining platform services are owned elsewhere:
// the partition table, RF calibration and lwIP/Wi-Fi bring-up run under the
// SDK SYSINIT flow, and the examples wire their network backbones in main.c
// before the stack init.
#include <bflb_sec_trng.h>

namespace chip {
namespace DeviceLayer {

static int app_entropy_source(void * data, unsigned char * output, size_t len, size_t * olen)
{
    bflb_trng_readlen(reinterpret_cast<uint8_t *>(output), static_cast<int>(len));
    *olen = len;
    return 0;
}

CHIP_ERROR PlatformManagerImpl::_InitChipStack(void)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    // Config-store bootstrap (littlefs namespace + interrupted
    // factory-reset resume), before anything reads or writes config keys.
    Internal::BflbConfig::Init();

    // The Thread radio/platform bring-up runs under SDK SYSINIT (the
    // openthread_port ot_port_sysinit record), like the Wi-Fi/RF flow.

    ReturnErrorOnFailure(System::Clock::InitClock_RealTime());

    err = chip::Crypto::add_entropy_source(app_entropy_source, NULL, 16);
    SuccessOrExit(err);

    // Call _InitChipStack() on the generic implementation base class
    // to finish the initialization process.
    err = Internal::GenericPlatformManagerImpl_FreeRTOS<PlatformManagerImpl>::_InitChipStack();
    SuccessOrExit(err);

exit:
    return err;
}

} // namespace DeviceLayer
} // namespace chip
