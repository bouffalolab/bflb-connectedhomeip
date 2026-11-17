/*
 * Copyright (c) 2026 Project CHIP Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#include "ControllerCommands.h"
#include "SntpTimeSync.h"

#include <commands/common/Commands.h>
#include <commands/example/ExampleCredentialIssuerCommands.h>
#include <commands/pairing/Commands.h>
#include <commands/session-management/Commands.h>
#include <lib/core/CHIPError.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/logging/CHIPLogging.h>
#include <lib/shell/Engine.h>

#include <cstdlib>
#include <cstring>
#include <ctime>

namespace {

Commands sChipToolCommands;
ExampleCredentialIssuerCommands sCredentialIssuerCommands;

CHIP_ERROR PrintTimeStatus(int argc, char * argv[])
{
    time_t currentTime = time(nullptr);
    struct tm utcTime;
    char formattedTime[32] = "unavailable";

    if (gmtime_r(&currentTime, &utcTime) != nullptr)
    {
        strftime(formattedTime, sizeof(formattedTime), "%Y-%m-%dT%H:%M:%SZ", &utcTime);
    }

    ChipLogProgress(NotSpecified, "System time: %s (%lld Unix seconds)", formattedTime,
                    static_cast<long long>(currentTime));
    ChipLogProgress(NotSpecified, "SNTP synchronization: %s",
                    SntpTimeSyncIsSynchronized() ? "synchronized" : "not synchronized");
    ChipLogProgress(NotSpecified, "NTP server: %s", SntpTimeSyncServer());
    return CHIP_NO_ERROR;
}

CHIP_ERROR RunChipTool(int argc, char * argv[])
{
    constexpr int kMaxArguments = 32;
    VerifyOrReturnError(argc < kMaxArguments, CHIP_ERROR_INVALID_ARGUMENT);
    if (argc > 0 && strcmp(argv[0], "pairing") == 0 && !SntpTimeSyncIsSynchronized())
    {
        ChipLogError(NotSpecified, "Cannot start pairing before SNTP synchronizes the system clock");
        return CHIP_ERROR_BUSY;
    }
    char * commandArgv[kMaxArguments + 1] = { const_cast<char *>("chip-tool") };
    for (int index = 0; index < argc; ++index)
    {
        commandArgv[index + 1] = argv[index];
    }
    return sChipToolCommands.Run(argc + 1, commandArgv) == EXIT_SUCCESS ? CHIP_NO_ERROR : CHIP_ERROR_INTERNAL;
}

chip::Shell::shell_command_t sControllerShellCommands[] = {
    { RunChipTool, "chip-tool", "Run a Matter controller command" },
    { PrintTimeStatus, "time", "Show system time and SNTP synchronization status" },
};

} // namespace

void ControllerCommands::Init()
{
    chip::Shell::Engine::Root().RegisterCommands(sControllerShellCommands, MATTER_ARRAY_SIZE(sControllerShellCommands));
    registerCommandsPairing(sChipToolCommands, &sCredentialIssuerCommands);
    registerCommandsSessionManagement(sChipToolCommands, &sCredentialIssuerCommands);
}
