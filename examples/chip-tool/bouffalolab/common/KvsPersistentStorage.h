/*
 * Copyright (c) 2026 Project CHIP Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#pragma once

#include <lib/core/CHIPPersistentStorageDelegate.h>
#include <lib/core/CASEAuthTag.h>
#include <lib/core/NodeId.h>

class KvsPersistentStorage final : public chip::PersistentStorageDelegate
{
public:
    CHIP_ERROR Init(const char * name = nullptr, const char * directory = nullptr);
    CHIP_ERROR SyncGetKeyValue(const char * key, void * buffer, uint16_t & size) override;
    CHIP_ERROR SyncSetKeyValue(const char * key, const void * value, uint16_t size) override;
    CHIP_ERROR SyncDeleteKeyValue(const char * key) override;
    bool SyncDoesKeyExist(const char * key) override;

    uint16_t GetListenPort() const { return 0; }
    chip::NodeId GetLocalNodeId();
    chip::CATValues GetCommissionerCATs();

private:
    CHIP_ERROR MakeKey(const char * key, char * buffer, size_t bufferSize) const;
    char mNamespace[20] = "chip-tool-default";
};
