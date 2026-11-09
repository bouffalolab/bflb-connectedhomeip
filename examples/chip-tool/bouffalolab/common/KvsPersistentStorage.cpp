/*
 * Copyright (c) 2026 Project CHIP Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#include "KvsPersistentStorage.h"

#include <lib/support/CodeUtils.h>
#include <platform/bouffalolab/common/BflbConfig.h>
#include <platform/internal/CHIPDeviceLayerInternal.h>
#include <lib/core/CHIPEncoding.h>

#include <cstdio>
#include <cstring>

CHIP_ERROR KvsPersistentStorage::Init(const char * name, const char *)
{
    const char * storageName = name == nullptr ? "default" : name;
    uint32_t hash = 2166136261u;
    for (const char * current = storageName; *current != '\0'; ++current)
    {
        hash = (hash ^ static_cast<uint8_t>(*current)) * 16777619u;
    }
    int written = std::snprintf(mNamespace, sizeof(mNamespace), "chip-tool-%08x", static_cast<unsigned int>(hash));
    VerifyOrReturnError(written > 0 && static_cast<size_t>(written) < sizeof(mNamespace), CHIP_ERROR_INTERNAL);
    return CHIP_NO_ERROR;
}

CHIP_ERROR KvsPersistentStorage::MakeKey(const char * key, char * buffer, size_t bufferSize) const
{
    VerifyOrReturnError(key != nullptr && buffer != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    int written = std::snprintf(buffer, bufferSize, "%s-%s", mNamespace, key);
    VerifyOrReturnError(written > 0 && static_cast<size_t>(written) < bufferSize, CHIP_ERROR_INVALID_ARGUMENT);
    return CHIP_NO_ERROR;
}

CHIP_ERROR KvsPersistentStorage::SyncGetKeyValue(const char * key, void * buffer, uint16_t & size)
{
    VerifyOrReturnError(key != nullptr && (buffer != nullptr || size == 0), CHIP_ERROR_INVALID_ARGUMENT);

    char storageKey[128];
    ReturnErrorOnFailure(MakeKey(key, storageKey, sizeof(storageKey)));
    size_t readSize = 0;
    CHIP_ERROR err = chip::DeviceLayer::Internal::BflbConfig::ReadKVS(storageKey, buffer, size, &readSize, 0);
    if (err == CHIP_DEVICE_ERROR_CONFIG_NOT_FOUND)
    {
        return CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND;
    }
    if (err != CHIP_NO_ERROR)
    {
        return err;
    }

    if (readSize > UINT16_MAX)
    {
        return CHIP_ERROR_BUFFER_TOO_SMALL;
    }
    size = static_cast<uint16_t>(readSize);
    return CHIP_NO_ERROR;
}

CHIP_ERROR KvsPersistentStorage::SyncSetKeyValue(const char * key, const void * value, uint16_t size)
{
    VerifyOrReturnError(key != nullptr && (value != nullptr || size == 0), CHIP_ERROR_INVALID_ARGUMENT);
    char storageKey[128];
    ReturnErrorOnFailure(MakeKey(key, storageKey, sizeof(storageKey)));
    return chip::DeviceLayer::Internal::BflbConfig::WriteKVS(storageKey, value, size);
}

CHIP_ERROR KvsPersistentStorage::SyncDeleteKeyValue(const char * key)
{
    VerifyOrReturnError(key != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    char storageKey[128];
    ReturnErrorOnFailure(MakeKey(key, storageKey, sizeof(storageKey)));
    CHIP_ERROR err = chip::DeviceLayer::Internal::BflbConfig::ClearKVS(storageKey);
    return err == CHIP_DEVICE_ERROR_CONFIG_NOT_FOUND ? CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND : err;
}

bool KvsPersistentStorage::SyncDoesKeyExist(const char * key)
{
    uint8_t value = 0;
    uint16_t size = sizeof(value);
    return SyncGetKeyValue(key, &value, size) == CHIP_NO_ERROR;
}

chip::NodeId KvsPersistentStorage::GetLocalNodeId()
{
    uint64_t nodeId = 0;
    uint16_t size = sizeof(nodeId);
    CHIP_ERROR err = SyncGetKeyValue("LocalNodeId", &nodeId, size);
    return err == CHIP_NO_ERROR ? chip::Encoding::LittleEndian::HostSwap64(nodeId) : 112233;
}

chip::CATValues KvsPersistentStorage::GetCommissionerCATs()
{
    chip::CATValues cats;
    chip::CATValues::Serialized serialized;
    uint16_t size = chip::CATValues::kSerializedLength;
    if (SyncGetKeyValue("CommissionerCATs", serialized, size) == CHIP_NO_ERROR && size == chip::CATValues::kSerializedLength)
    {
        cats.Deserialize(serialized);
        return cats;
    }
    return chip::kUndefinedCATs;
}
