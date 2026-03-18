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

#include <lib/core/CHIPError.h>
#include <platform/CHIPDeviceLayer.h>

#include <openthread/udp.h>

#include <stddef.h>
#include <stdint.h>

class ThreadUdpBandwidthService
{
public:
    static constexpr uint16_t kUdpPort = 33333;

    static ThreadUdpBandwidthService & Instance();

    CHIP_ERROR Init();

private:
    struct Counters
    {
        uint64_t bytesRx      = 0;
        uint64_t pktsRx       = 0;
        uint64_t bytesTx      = 0;
        uint64_t pktsTx       = 0;
        uint64_t seqGapCount  = 0;
        uint32_t lastError    = 0;
        uint32_t activeTx     = 0;
        uint32_t txPayload    = 0;
        uint32_t txChunkCount = 0;
    };

    ThreadUdpBandwidthService() = default;

    static void StartWork(intptr_t arg);
    static void TxWork(intptr_t arg);
    static void HandleUdpReceive(void * aContext, otMessage * aMessage, const otMessageInfo * aMessageInfo);

    void StartOnChipStack();
    void OnUdpReceive(otMessage * aMessage, const otMessageInfo * aMessageInfo);
    void HandleDataPacket(const uint8_t * data, size_t len);
    void HandleControlPacket(const otMessageInfo * aMessageInfo, const uint8_t * data, size_t len);

    void ResetCounters();
    void StopTx();
    void ScheduleTxWork();
    void RunTxChunk();

    bool SendControlReply(const otIp6Address & destAddr, uint16_t destPort, const char * fmt, ...);
    bool ParseUintField(const char * line, const char * key, uint32_t & value) const;
    bool StartsWithControlPrefix(const uint8_t * data, size_t len) const;

    otUdpSocket mSocket;
    bool mInitScheduled  = false;
    bool mStarted        = false;
    bool mTxWorkScheduled = false;

    bool mHaveRxSeq      = false;
    uint32_t mNextRxSeq  = 0;
    uint32_t mNextTxSeq  = 0;
    uint64_t mTxEndMs    = 0;
    uint16_t mTxDestPort = 0;
    otIp6Address mTxDestAddr;

    Counters mCounters;
};
