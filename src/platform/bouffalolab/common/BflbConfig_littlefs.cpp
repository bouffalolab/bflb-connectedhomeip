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

/**
 * BflbConfig backend on the SDK lfs_kv store (littlefs over the PSM
 * partition).
 *
 * The mount and the namespace directory are owned by the Bouffalo SDK
 * SYSINIT flow (components/fs/littlefs, lfs_kv record). For this build the
 * LFS_KV_NAMESPACE is redefined to the legacy "/_blcfg_" (bflb_matter.cmake)
 * and every config key is pre-mapped ('/' -> '_') before it reaches the
 * lfs_kv API, so the on-flash names stay byte-identical to previous firmware
 * releases (the lfs_kv '/' escaping alone would orphan the provisioned
 * state after an OTA). One known boundary: a key containing a literal '}'
 * cannot round-trip (lfs_kv always escapes '}'), which in practice only
 * affects the optional g/nim/@<base85> key family.
 *
 * The factory-reset pending flag lives at the PSM root, outside the wiped
 * namespace, and is written/removed with raw littlefs calls (the lfs_kv API
 * only reaches its own namespace): an interrupted wipe resumes from Init()
 * on the next boot.
 */

#include <stdio.h>
#include <stdio.h>
#include <string.h>

#include <platform/internal/CHIPDeviceLayerInternal.h>

#include <platform/bouffalolab/common/BflbConfig.h>

extern "C" {
#include <lfs.h>
#include <lfs_kv.h>
}

namespace chip {
namespace DeviceLayer {
namespace Internal {

namespace {

CHIP_ERROR blcfg_kv_err(lfs_kv_err_t ret)
{
    return (ret == LFS_KV_OK) ? CHIP_NO_ERROR : CHIP_ERROR_PERSISTED_STORAGE_FAILED;
}

// Map a config key into the lfs_kv key space: '/' maps to '_' so the
// resulting file name matches the legacy BflbConfig construction exactly
// (lfs_kv would escape '/' differently). Returns false when the key does
// not fit the SDK key buffer.
bool blcfg_kv_key(const char * key, char(&buf)[LFS_KV_KEY_MAX])
{
    const size_t len = strlen(key);
    if (len >= sizeof(buf))
    {
        return false;
    }
    memcpy(buf, key, len + 1);
    for (size_t i = 0; i < len; i++)
    {
        if (buf[i] == '/')
        {
            buf[i] = '_';
        }
    }
    return true;
}

// Root path of the factory-reset pending flag (outside the KV namespace, so
// a namespace wipe cannot consume it mid-wipe).
void blcfg_flag_path(char(&buf)[LFS_KV_KEY_MAX])
{
    snprintf(buf, sizeof(buf), "/%s", BflbConfig::kBLKey_factoryResetFlag);
}

// Wipe every key in the KV namespace, then remove the root-level pending
// flag. The flag is written before the wipe, so an interrupted reset
// resumes from BflbConfig::Init on the next boot.
CHIP_ERROR blcfg_do_factory_reset(lfs_t * fs)
{
    ReturnLogErrorOnFailure(blcfg_kv_err(lfs_kv_clear()));

    char flag_path[LFS_KV_KEY_MAX];
    blcfg_flag_path(flag_path);
    const int ret = lfs_remove(fs, flag_path);
    VerifyOrReturnError(ret == LFS_ERR_OK || ret == LFS_ERR_NOENT, CHIP_ERROR_PERSISTED_STORAGE_FAILED);

    return CHIP_NO_ERROR;
}

} // namespace

void BflbConfig::Init(void)
{
    // The mount and the namespace directory are owned by SDK SYSINIT (the
    // lfs_kv record; LFS_KV_NAMESPACE is "/_blcfg_" for the Matter build).
    // Resume an interrupted factory reset from the root-level flag here.
    CHIP_ERROR err = CHIP_NO_ERROR;
    lfs_t * fs     = lfs_kv_fs();
    VerifyOrExit(fs != nullptr, err = CHIP_ERROR_PERSISTED_STORAGE_FAILED);

    struct lfs_info stat;
    char flag_path[LFS_KV_KEY_MAX];
    blcfg_flag_path(flag_path);

    if (lfs_stat(fs, flag_path, &stat) == LFS_ERR_OK)
    {
        ChipLogProgress(DeviceLayer, "Resuming interrupted factory reset");
        err = blcfg_do_factory_reset(fs);
    }

exit:
    configASSERT(err == CHIP_NO_ERROR);
}

CHIP_ERROR BflbConfig::ReadConfigValue(const char * key, uint8_t * val, size_t size, size_t & readsize)
{
    char kv_key[LFS_KV_KEY_MAX];
    VerifyOrReturnError(blcfg_kv_key(key, kv_key), CHIP_ERROR_INVALID_ARGUMENT);

    size_t savedLen = 0;
    readsize        = lfs_kv_get_blob(kv_key, val, size, &savedLen);
    if (readsize == 0 && savedLen == 0)
    {
        // Key not present (or store unavailable).
        return CHIP_DEVICE_ERROR_CONFIG_NOT_FOUND;
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR BflbConfig::ReadConfigValue(const char * key, bool & val)
{
    size_t readlen = 0;
    return ReadConfigValue(key, (uint8_t *) &val, 1, readlen);
}

CHIP_ERROR BflbConfig::ReadConfigValue(const char * key, uint32_t & val)
{
    size_t readlen = 0;
    return ReadConfigValue(key, (uint8_t *) &val, sizeof(val), readlen);
}

CHIP_ERROR BflbConfig::ReadConfigValue(const char * key, uint64_t & val)
{
    size_t readlen = 0;
    return ReadConfigValue(key, (uint8_t *) &val, sizeof(val), readlen);
}

CHIP_ERROR BflbConfig::ReadConfigValueStr(const char * key, char * buf, size_t bufSize, size_t & outLen)
{
    size_t readlen = 0;
    if (CHIP_NO_ERROR == ReadConfigValue(key, (uint8_t *) buf, bufSize, readlen))
    {
        outLen = readlen;
        if (readlen && readlen < bufSize)
        {
            buf[readlen] = '\0';
        }

        return CHIP_NO_ERROR;
    }

    return CHIP_DEVICE_ERROR_CONFIG_NOT_FOUND;
}

CHIP_ERROR BflbConfig::ReadConfigValueBin(const char * key, uint8_t * buf, size_t bufSize, size_t & outLen)
{
    size_t readlen = 0;
    if (CHIP_NO_ERROR == ReadConfigValue(key, (uint8_t *) buf, bufSize, readlen))
    {
        outLen = readlen;
        return CHIP_NO_ERROR;
    }

    return CHIP_DEVICE_ERROR_CONFIG_NOT_FOUND;
}

CHIP_ERROR BflbConfig::WriteConfigValue(const char * key, uint8_t * val, size_t size)
{
    // The SDK lfs_kv API rejects empty values.
    VerifyOrReturnError(val != nullptr && size != 0, CHIP_ERROR_INVALID_ARGUMENT);

    char kv_key[LFS_KV_KEY_MAX];
    VerifyOrReturnError(blcfg_kv_key(key, kv_key), CHIP_ERROR_INVALID_ARGUMENT);

    return blcfg_kv_err(lfs_kv_set_blob(kv_key, val, size));
}

CHIP_ERROR BflbConfig::WriteConfigValue(const char * key, bool val)
{
    return WriteConfigValue(key, (uint8_t *) &val, sizeof(val));
}

CHIP_ERROR BflbConfig::WriteConfigValue(const char * key, uint32_t val)
{
    return WriteConfigValue(key, (uint8_t *) &val, sizeof(val));
}

CHIP_ERROR BflbConfig::WriteConfigValue(const char * key, uint64_t val)
{
    return WriteConfigValue(key, (uint8_t *) &val, sizeof(val));
}

CHIP_ERROR BflbConfig::WriteConfigValueStr(const char * key, const char * str)
{
    return WriteConfigValue(key, (uint8_t *) str, strlen(str) + 1);
}

CHIP_ERROR BflbConfig::WriteConfigValueStr(const char * key, const char * str, size_t strLen)
{
    return WriteConfigValue(key, (uint8_t *) str, strLen);
}

CHIP_ERROR BflbConfig::WriteConfigValueBin(const char * key, const uint8_t * data, size_t dataLen)
{
    return WriteConfigValue(key, (uint8_t *) data, dataLen);
}

CHIP_ERROR BflbConfig::ClearConfigValue(const char * key)
{
    // lfs_kv_del is idempotent (missing key: LFS_KV_OK).
    char kv_key[LFS_KV_KEY_MAX];
    VerifyOrReturnError(blcfg_kv_key(key, kv_key), CHIP_ERROR_INVALID_ARGUMENT);

    return blcfg_kv_err(lfs_kv_del(kv_key));
}

CHIP_ERROR BflbConfig::FactoryResetConfig(void)
{
    // Mark the reset as pending first (root-level file, outside the wiped
    // namespace) so an interrupted wipe resumes on the next boot (see Init),
    // then clear the whole KV namespace.
    static const char reset_key_value[] = "pending";

    lfs_t * fs = lfs_kv_fs();
    VerifyOrReturnError(fs != nullptr, CHIP_ERROR_INCORRECT_STATE);

    char flag_path[LFS_KV_KEY_MAX];
    blcfg_flag_path(flag_path);

    CHIP_ERROR err = CHIP_NO_ERROR;
    lfs_file_t file;
    int ret = LFS_ERR_OK;

    fs->cfg->lock(fs->cfg);

    ret = lfs_file_open(fs, &file, flag_path, LFS_O_CREAT | LFS_O_RDWR | LFS_O_TRUNC);
    if (ret == LFS_ERR_OK)
    {
        lfs_file_write(fs, &file, reset_key_value, sizeof(reset_key_value));
        ret = lfs_file_close(fs, &file);
    }
    if (ret != LFS_ERR_OK)
    {
        err = CHIP_ERROR_PERSISTED_STORAGE_FAILED;
    }

    fs->cfg->unlock(fs->cfg);
    SuccessOrExit(err);

    return blcfg_do_factory_reset(fs);

exit:
    return err;
}

void BflbConfig::RunConfigUnitTest() {}

bool BflbConfig::ConfigValueExists(const char * key)
{
    char kv_key[LFS_KV_KEY_MAX];
    if (!blcfg_kv_key(key, kv_key))
    {
        return false;
    }

    size_t savedLen = 0;
    TEMPORARY_RETURN_IGNORED lfs_kv_get_blob(kv_key, nullptr, 0, &savedLen);
    return savedLen != 0;
}

CHIP_ERROR BflbConfig::ReadKVS(const char * key, void * value, size_t value_size, size_t * read_bytes_size, size_t offset_bytes)
{
    char kv_key[LFS_KV_KEY_MAX];
    VerifyOrReturnError(blcfg_kv_key(key, kv_key), CHIP_ERROR_INVALID_ARGUMENT);

    if (read_bytes_size)
    {
        *read_bytes_size = 0;
    }

    size_t savedLen = 0;
    const size_t readLen = lfs_kv_get_blob_offset(kv_key, value, value_size, &savedLen, static_cast<int>(offset_bytes));
    if (readLen == 0 && savedLen == 0)
    {
        return CHIP_DEVICE_ERROR_CONFIG_NOT_FOUND;
    }

    if (read_bytes_size)
    {
        *read_bytes_size = readLen;
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR BflbConfig::WriteKVS(const char * key, const void * value, size_t value_size)
{
    return WriteConfigValueBin(key, (const uint8_t *) value, value_size);
}

CHIP_ERROR BflbConfig::ClearKVS(const char * key)
{
    return ClearConfigValue(key);
}

} // namespace Internal
} // namespace DeviceLayer
} // namespace chip
