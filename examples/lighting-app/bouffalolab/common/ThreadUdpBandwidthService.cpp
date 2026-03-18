/*
 * Copyright (c) 2026 Project CHIP Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#include "ThreadUdpBandwidthService.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/PlatformManager.h>
#include <platform/ThreadStackManager.h>
#include <system/SystemClock.h>

#include <openthread/instance.h>
#include <openthread/message.h>
#include <openthread/udp.h>

using namespace chip;
using namespace chip::DeviceLayer;

namespace {
constexpr size_t kMaxControlPacketLen = 256;
constexpr size_t kMaxRxBuf            = 1300;
constexpr uint32_t kMinPayloadBytes   = 4;
constexpr uint32_t kMaxPayloadBytes   = 1200;
constexpr uint32_t kMinDurationMs     = 1;
constexpr uint32_t kMaxDurationMs     = 600000;
constexpr uint32_t kMinChunkCount     = 1;
constexpr uint32_t kMaxChunkCount     = 256;
constexpr const char kProtocolPrefix[] = "BWT1";

uint64_t GetMonotonicMs()
{
    return System::SystemClock().GetMonotonicMilliseconds64().count();
}

uint32_t ReadBigEndianU32(const uint8_t * data)
{
    return (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
        (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
}

void WriteBigEndianU32(uint8_t * data, uint32_t value)
{
    data[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
    data[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    data[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[3] = static_cast<uint8_t>(value & 0xFF);
}

otInstance * GetOtInstance()
{
    return ThreadStackMgrImpl().OTInstance();
}
} // namespace

ThreadUdpBandwidthService & ThreadUdpBandwidthService::Instance()
{
    static ThreadUdpBandwidthService sInstance;
    return sInstance;
}

CHIP_ERROR ThreadUdpBandwidthService::Init()
{
    if (mStarted || mInitScheduled)
    {
        return CHIP_NO_ERROR;
    }

    mInitScheduled = true;
    CHIP_ERROR err = PlatformMgr().ScheduleWork(StartWork, 0);
    if (err != CHIP_NO_ERROR)
    {
        mInitScheduled = false;
        mCounters.lastError = static_cast<uint32_t>(err.AsInteger());
    }
    return err;
}

void ThreadUdpBandwidthService::StartWork(intptr_t)
{
    Instance().StartOnChipStack();
}

void ThreadUdpBandwidthService::StartOnChipStack()
{
    if (mStarted)
    {
        return;
    }

    otInstance * instance = GetOtInstance();
    if (instance == nullptr)
    {
        mInitScheduled = false;
        ChipLogError(NotSpecified, "BW: otInstance is null");
        return;
    }

    memset(&mSocket, 0, sizeof(mSocket));

    otError otErr = otUdpOpen(instance, &mSocket, HandleUdpReceive, this);
    if (otErr != OT_ERROR_NONE)
    {
        mInitScheduled = false;
        mCounters.lastError = static_cast<uint32_t>(otErr);
        ChipLogError(NotSpecified, "BW: otUdpOpen failed: %d", otErr);
        return;
    }

    otSockAddr bindAddr;
    memset(&bindAddr, 0, sizeof(bindAddr));
    bindAddr.mPort = kUdpPort;

    otErr = otUdpBind(instance, &mSocket, &bindAddr, OT_NETIF_THREAD);
    if (otErr != OT_ERROR_NONE)
    {
        otUdpClose(instance, &mSocket);
        mInitScheduled = false;
        mCounters.lastError = static_cast<uint32_t>(otErr);
        ChipLogError(NotSpecified, "BW: otUdpBind failed: %d", otErr);
        return;
    }

    ResetCounters();
    mStarted       = true;
    mInitScheduled = false;
    ChipLogProgress(NotSpecified, "ThreadUdpBandwidthService listening on UDP %" PRIu16, kUdpPort);
}

void ThreadUdpBandwidthService::HandleUdpReceive(void * aContext, otMessage * aMessage, const otMessageInfo * aMessageInfo)
{
    auto * self = static_cast<ThreadUdpBandwidthService *>(aContext);
    self->OnUdpReceive(aMessage, aMessageInfo);
}

void ThreadUdpBandwidthService::OnUdpReceive(otMessage * aMessage, const otMessageInfo * aMessageInfo)
{
    VerifyOrReturn(aMessage != nullptr);
    VerifyOrReturn(aMessageInfo != nullptr);

    uint16_t len = otMessageGetLength(aMessage) - otMessageGetOffset(aMessage);
    if (len == 0 || len > kMaxRxBuf)
    {
        return;
    }

    uint8_t buf[kMaxRxBuf];
    uint16_t read = otMessageRead(aMessage, otMessageGetOffset(aMessage), buf, len);
    if (read == 0)
    {
        return;
    }

    if (StartsWithControlPrefix(buf, read))
    {
        HandleControlPacket(aMessageInfo, buf, read);
        return;
    }

    HandleDataPacket(buf, read);
}

bool ThreadUdpBandwidthService::StartsWithControlPrefix(const uint8_t * data, size_t len) const
{
    constexpr size_t kPrefixLen = sizeof(kProtocolPrefix) - 1;
    if (len < (kPrefixLen + 1))
    {
        return false;
    }
    return memcmp(data, kProtocolPrefix, kPrefixLen) == 0 && data[kPrefixLen] == ' ';
}

void ThreadUdpBandwidthService::HandleDataPacket(const uint8_t * data, size_t len)
{
    mCounters.bytesRx += len;
    mCounters.pktsRx++;

    if (len < 4)
    {
        return;
    }

    const uint32_t seq = ReadBigEndianU32(data);
    if (!mHaveRxSeq)
    {
        mHaveRxSeq = true;
        mNextRxSeq = seq + 1;
        return;
    }

    if (seq > mNextRxSeq)
    {
        mCounters.seqGapCount += static_cast<uint64_t>(seq - mNextRxSeq);
    }
    if (seq >= mNextRxSeq)
    {
        mNextRxSeq = seq + 1;
    }
}

bool ThreadUdpBandwidthService::ParseUintField(const char * line, const char * key, uint32_t & value) const
{
    char pattern[48] = { 0 };
    const int written = snprintf(pattern, sizeof(pattern), "%s=", key);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(pattern))
    {
        return false;
    }

    const char * p = strstr(line, pattern);
    if (p == nullptr)
    {
        return false;
    }
    p += strlen(pattern);

    char * end         = nullptr;
    unsigned long temp = strtoul(p, &end, 10);
    if (end == p)
    {
        return false;
    }

    value = static_cast<uint32_t>(temp);
    return true;
}

bool ThreadUdpBandwidthService::SendControlReply(const otIp6Address & destAddr, uint16_t destPort, const char * fmt, ...)
{
    otInstance * instance = GetOtInstance();
    if (instance == nullptr)
    {
        return false;
    }

    char response[256];
    va_list args;
    va_start(args, fmt);
    const int used = vsnprintf(response, sizeof(response), fmt, args);
    va_end(args);

    if (used <= 0)
    {
        return false;
    }
    size_t len = static_cast<size_t>(used);
    if (len >= sizeof(response))
    {
        len = sizeof(response) - 1;
    }

    otMessage * msg = otUdpNewMessage(instance, nullptr);
    if (msg == nullptr)
    {
        return false;
    }

    if (otMessageAppend(msg, response, static_cast<uint16_t>(len)) != OT_ERROR_NONE)
    {
        otMessageFree(msg);
        return false;
    }

    otMessageInfo msgInfo;
    memset(&msgInfo, 0, sizeof(msgInfo));
    msgInfo.mPeerAddr = destAddr;
    msgInfo.mPeerPort = destPort;

    otError err = otUdpSend(instance, &mSocket, msg, &msgInfo);
    if (err != OT_ERROR_NONE)
    {
        mCounters.lastError = static_cast<uint32_t>(err);
        return false;
    }
    return true;
}

void ThreadUdpBandwidthService::ResetCounters()
{
    mCounters = Counters{};
    mHaveRxSeq       = false;
    mNextRxSeq       = 0;
    mNextTxSeq       = 0;
    mTxEndMs         = 0;
    mTxDestPort      = 0;
    memset(&mTxDestAddr, 0, sizeof(mTxDestAddr));
    mTxWorkScheduled = false;
}

void ThreadUdpBandwidthService::StopTx()
{
    mCounters.activeTx = 0;
    mTxEndMs           = 0;
    mTxDestPort        = 0;
}

void ThreadUdpBandwidthService::ScheduleTxWork()
{
    if (mCounters.activeTx == 0 || mTxWorkScheduled)
    {
        return;
    }

    CHIP_ERROR err = PlatformMgr().ScheduleWork(TxWork, 0);
    if (err != CHIP_NO_ERROR)
    {
        mCounters.lastError = static_cast<uint32_t>(err.AsInteger());
        StopTx();
        return;
    }

    mTxWorkScheduled = true;
}

void ThreadUdpBandwidthService::TxWork(intptr_t)
{
    ThreadUdpBandwidthService & self = Instance();
    self.mTxWorkScheduled            = false;
    self.RunTxChunk();
}

void ThreadUdpBandwidthService::RunTxChunk()
{
    VerifyOrReturn(mCounters.activeTx != 0);

    otInstance * instance = GetOtInstance();
    if (instance == nullptr)
    {
        StopTx();
        return;
    }

    if (GetMonotonicMs() >= mTxEndMs)
    {
        StopTx();
        return;
    }

    const uint32_t maxPackets = (mCounters.txChunkCount == 0) ? 1 : mCounters.txChunkCount;

    for (uint32_t i = 0; i < maxPackets; ++i)
    {
        if (GetMonotonicMs() >= mTxEndMs)
        {
            StopTx();
            break;
        }

        const uint32_t payloadLen = (mCounters.txPayload == 0) ? kMinPayloadBytes : mCounters.txPayload;

        otMessage * msg = otUdpNewMessage(instance, nullptr);
        if (msg == nullptr)
        {
            mCounters.lastError = 0xFF;
            StopTx();
            break;
        }

        uint8_t header[4];
        WriteBigEndianU32(header, mNextTxSeq++);
        if (otMessageAppend(msg, header, sizeof(header)) != OT_ERROR_NONE)
        {
            otMessageFree(msg);
            StopTx();
            break;
        }

        if (payloadLen > 4)
        {
            uint8_t fill[64];
            memset(fill, 0xA5, sizeof(fill));
            uint32_t remaining = payloadLen - 4;
            while (remaining > 0)
            {
                uint16_t chunk = (remaining > sizeof(fill)) ? sizeof(fill) : static_cast<uint16_t>(remaining);
                if (otMessageAppend(msg, fill, chunk) != OT_ERROR_NONE)
                {
                    otMessageFree(msg);
                    StopTx();
                    return;
                }
                remaining -= chunk;
            }
        }

        otMessageInfo msgInfo;
        memset(&msgInfo, 0, sizeof(msgInfo));
        msgInfo.mPeerAddr = mTxDestAddr;
        msgInfo.mPeerPort = mTxDestPort;

        otError err = otUdpSend(instance, &mSocket, msg, &msgInfo);
        if (err != OT_ERROR_NONE)
        {
            mCounters.lastError = static_cast<uint32_t>(err);
            StopTx();
            break;
        }

        mCounters.bytesTx += payloadLen;
        mCounters.pktsTx++;
    }

    if (mCounters.activeTx != 0)
    {
        ScheduleTxWork();
    }
}

void ThreadUdpBandwidthService::HandleControlPacket(const otMessageInfo * aMessageInfo, const uint8_t * data, size_t len)
{
    char line[kMaxControlPacketLen];
    const size_t copyLen = (len < (sizeof(line) - 1)) ? len : (sizeof(line) - 1);
    memcpy(line, data, copyLen);
    line[copyLen] = '\0';

    for (size_t i = 0; i < copyLen; ++i)
    {
        if (line[i] == '\r' || line[i] == '\n')
        {
            line[i] = '\0';
            break;
        }
    }

    const otIp6Address & srcAddr = aMessageInfo->mPeerAddr;
    const uint16_t srcPort       = aMessageInfo->mPeerPort;

    if (strncmp(line, "BWT1 RESET", strlen("BWT1 RESET")) == 0)
    {
        ResetCounters();
        SendControlReply(srcAddr, srcPort, "BWT1 OK verb=RESET\n");
        return;
    }

    if (strncmp(line, "BWT1 GET_STATS", strlen("BWT1 GET_STATS")) == 0)
    {
        SendControlReply(srcAddr, srcPort,
                         "BWT1 STATS bytes_rx=%" PRIu64 " pkts_rx=%" PRIu64 " bytes_tx=%" PRIu64
                         " pkts_tx=%" PRIu64 " seq_gap_count=%" PRIu64 " active_tx=%u last_error=%u tx_payload=%u tx_chunk_count=%u\n",
                         mCounters.bytesRx, mCounters.pktsRx, mCounters.bytesTx, mCounters.pktsTx, mCounters.seqGapCount,
                         mCounters.activeTx, mCounters.lastError, mCounters.txPayload, mCounters.txChunkCount);
        return;
    }

    if (strncmp(line, "BWT1 STOP_TX", strlen("BWT1 STOP_TX")) == 0)
    {
        StopTx();
        SendControlReply(srcAddr, srcPort, "BWT1 OK verb=STOP_TX\n");
        return;
    }

    if (strncmp(line, "BWT1 START_TX", strlen("BWT1 START_TX")) == 0)
    {
        uint32_t payload = 0;
        uint32_t durationMs = 0;
        uint32_t destPort = 0;
        uint32_t chunkCount = 0;

        if (!ParseUintField(line, "payload", payload) || !ParseUintField(line, "duration_ms", durationMs) ||
            !ParseUintField(line, "dest_port", destPort))
        {
            SendControlReply(srcAddr, srcPort, "BWT1 ERR code=1 reason=missing_required_fields\n");
            return;
        }

        if (!ParseUintField(line, "chunk_count", chunkCount))
        {
            chunkCount = 8;
        }

        if (payload < kMinPayloadBytes || payload > kMaxPayloadBytes || durationMs < kMinDurationMs || durationMs > kMaxDurationMs ||
            destPort == 0 || destPort > 65535 || chunkCount < kMinChunkCount || chunkCount > kMaxChunkCount)
        {
            SendControlReply(srcAddr, srcPort, "BWT1 ERR code=2 reason=invalid_range\n");
            return;
        }

        StopTx();
        mCounters.bytesTx      = 0;
        mCounters.pktsTx       = 0;
        mCounters.lastError    = 0;
        mCounters.txPayload    = payload;
        mCounters.txChunkCount = chunkCount;
        mCounters.activeTx     = 1;
        mTxDestAddr            = srcAddr;
        mTxDestPort            = static_cast<uint16_t>(destPort);
        mTxEndMs               = GetMonotonicMs() + durationMs;
        mNextTxSeq             = 0;

        ScheduleTxWork();
        if (mCounters.activeTx == 0)
        {
            SendControlReply(srcAddr, srcPort,
                             "BWT1 ERR code=4 reason=schedule_failed last_error=%u\n", mCounters.lastError);
            return;
        }

        SendControlReply(srcAddr, srcPort,
                         "BWT1 OK verb=START_TX payload=%u duration_ms=%u dest_port=%u chunk_count=%u\n", payload, durationMs,
                         static_cast<unsigned>(mTxDestPort), chunkCount);
        return;
    }

    SendControlReply(srcAddr, srcPort, "BWT1 ERR code=3 reason=unknown_command\n");
}
