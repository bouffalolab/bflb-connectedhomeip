/*
 * Copyright (c) 2026 Project CHIP Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#include "SntpTimeSync.h"

#include <FreeRTOS.h>
#include <task.h>

#include <lwip/ip4_addr.h>
#include <lwip/netdb.h>
#include <lwip/netif.h>
#include <lwip/sockets.h>

#include <sys/time.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char kNtpServer[]             = "pool.ntp.org";
static const uint32_t kNtpToUnixEpoch      = 2208988800UL;
static const uint32_t kMinimumUnixTime     = 1577836800UL; // 2020-01-01
static const uint32_t kMaximumUnixTime     = 4102444800UL; // 2100-01-01
static const uint32_t kSyncRetryMs         = 5000;
static const uint32_t kSyncIntervalMs      = 3600000;
static const uint32_t kSocketTimeoutMs     = 5000;
static const uint32_t kSntpTaskStackWords  = 2048;
enum { kNtpPacketSize = 48 };
static volatile bool sTimeSynchronized = false;

static uint32_t ReadBigEndian32(const uint8_t * data)
{
    return ((uint32_t) data[0] << 24) | ((uint32_t) data[1] << 16) | ((uint32_t) data[2] << 8) | (uint32_t) data[3];
}

static bool HasUsableIPv4Address(void)
{
    const struct netif * netif = netif_default;
    return netif != NULL && netif_is_up(netif) && netif_is_link_up(netif) && !ip4_addr_isany(netif_ip4_addr(netif));
}

static bool SynchronizeTime(void)
{
    struct addrinfo hints;
    struct addrinfo * server = NULL;
    int lookupResult;
    int socketFd;
    int result;
    uint8_t request[kNtpPacketSize] = { 0 };
    uint8_t response[kNtpPacketSize] = { 0 };
    struct timeval timeout;
    uint8_t mode;
    uint8_t version;
    uint8_t stratum;
    uint32_t ntpSeconds;
    uint32_t unixSeconds;
    uint32_t ntpFraction;
    struct timeval synchronizedTime;
    time_t currentTime;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    lookupResult = getaddrinfo(kNtpServer, "123", &hints, &server);
    if (lookupResult != 0 || server == NULL) {
        printf("SNTP: DNS lookup for %s failed (%d)\r\n", kNtpServer, lookupResult);
        if (server != NULL) {
            freeaddrinfo(server);
        }
        return false;
    }

    socketFd = socket(server->ai_family, server->ai_socktype, server->ai_protocol);
    if (socketFd < 0) {
        printf("SNTP: socket creation failed (%d)\r\n", errno);
        freeaddrinfo(server);
        return false;
    }

    timeout.tv_sec  = (long) (kSocketTimeoutMs / 1000);
    timeout.tv_usec = 0;
    result = setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (result == 0) {
        result = connect(socketFd, server->ai_addr, server->ai_addrlen);
    }
    freeaddrinfo(server);

    request[0] = 0x23; // LI=0, VN=4, client mode.
    if (result == 0) {
        result = (int) send(socketFd, request, sizeof(request), 0);
    }
    if (result >= 0) {
        result = (int) recv(socketFd, response, sizeof(response), 0);
    }
    closesocket(socketFd);

    if (result != (int) sizeof(response)) {
        printf("SNTP: request/response failed (%d, errno %d)\r\n", result, errno);
        return false;
    }

    mode    = response[0] & 0x07;
    version = (response[0] >> 3) & 0x07;
    stratum = response[1];
    if ((mode != 4 && mode != 5) || version < 3 || version > 4 || stratum == 0 || stratum > 15) {
        printf("SNTP: invalid response (mode %u, version %u, stratum %u)\r\n", mode, version, stratum);
        return false;
    }

    ntpSeconds = ReadBigEndian32(&response[40]);
    ntpFraction = ReadBigEndian32(&response[44]);
    if (ntpSeconds < kNtpToUnixEpoch) {
        printf("SNTP: timestamp precedes Unix epoch\r\n");
        return false;
    }

    unixSeconds = ntpSeconds - kNtpToUnixEpoch;
    if (unixSeconds < kMinimumUnixTime || unixSeconds >= kMaximumUnixTime) {
        printf("SNTP: unreasonable Unix timestamp %lu\r\n", (unsigned long) unixSeconds);
        return false;
    }

    synchronizedTime.tv_sec  = (time_t) unixSeconds;
    synchronizedTime.tv_usec = (suseconds_t) (((uint64_t) ntpFraction * 1000000ULL) >> 32);
    if (settimeofday(&synchronizedTime, NULL) != 0) {
        printf("SNTP: settimeofday failed (%d)\r\n", errno);
        return false;
    }
    if (!SntpTimeSyncSetMatterTime(unixSeconds, (uint32_t) synchronizedTime.tv_usec)) {
        printf("SNTP: failed to update Matter system clock\r\n");
        return false;
    }

    currentTime = time(NULL);
    printf("SNTP synchronized system time: %lu (Unix seconds)\r\n", (unsigned long) currentTime);
    sTimeSynchronized = currentTime >= (time_t) kMinimumUnixTime;
    return sTimeSynchronized;
}

static void SntpTimeSyncTask(void * arg)
{
    (void) arg;
    for (;;) {
        while (!HasUsableIPv4Address()) {
            vTaskDelay(pdMS_TO_TICKS(kSyncRetryMs));
        }

        if (SynchronizeTime()) {
            vTaskDelay(pdMS_TO_TICKS(kSyncIntervalMs));
        } else {
            vTaskDelay(pdMS_TO_TICKS(kSyncRetryMs));
        }
    }
}

void SntpTimeSyncStart(void)
{
    static TaskHandle_t taskHandle = NULL;
    BaseType_t result;

    if (taskHandle != NULL) {
        return;
    }

    result = xTaskCreate(SntpTimeSyncTask, "sntp_time", kSntpTaskStackWords, NULL, tskIDLE_PRIORITY + 1, &taskHandle);
    if (result != pdPASS) {
        taskHandle = NULL;
        printf("SNTP: failed to create time synchronization task\r\n");
    }
}

bool SntpTimeSyncIsSynchronized(void)
{
    return sTimeSynchronized;
}

const char * SntpTimeSyncServer(void)
{
    return kNtpServer;
}
